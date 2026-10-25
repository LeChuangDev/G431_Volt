/**
 * @file  bsp_adc.c —— ADC1 / ADC2 注入组（快环 10 kHz）
 *
 * 设计要点：
 *  1) ADC1 注入组 4 个 rank：VOUT / IOUT / VIN / IIN；
 *     ADC2 注入组 3 个 rank：NTC / OCP_SENSE / OVP_SENSE。
 *     两个 ADC 都配成"软件触发"，由 TIM6 中断里 bsp_adc_start() 启动。
 *  2) 转换结果在【下一拍】读取，所以保护动作的固件延迟是 100 µs + ISR 时间，
 *     满足 < 100 µs 等级（硬件过压/过流比较器是 L2，这里是 L3 后备）。
 *  3) 时钟用 HCLK/4 = 42.5 MHz（ADC12_CCR.CKMODE=11），采样时间 12.5 周期；
 *     NTC 用 47.5 周期，降低高阻源误差。
 */
#include "bsp.h"
#include "stm32g4xx.h"
#include "bsp_pins.h"
#include "math.h"

static volatile bool s_valid;

/* ------------------------------------------------------------------ */
/* 标量换算                                                           */
/* ------------------------------------------------------------------ */
static inline uint32_t scale_q16(uint16_t raw, uint32_t q16)
{
    return ((uint32_t)raw * q16) >> 16;
}

uint32_t bsp_vout_mv(uint16_t raw) { return scale_q16(raw, BSP_VSENSE_Q16); }
uint32_t bsp_vin_mv(uint16_t raw)  { return scale_q16(raw, BSP_VSENSE_Q16); }
uint32_t bsp_iout_ma(uint16_t raw) { return scale_q16(raw, BSP_IOUT_Q16); }
uint32_t bsp_iin_ma(uint16_t raw)  { return scale_q16(raw, BSP_IIN_Q16); }
uint32_t bsp_ntc_mv(uint16_t raw)  { return scale_q16(raw, BSP_NTC_MV_Q16); }

bool bsp_ntc_celsius_x10(uint16_t raw, int16_t *t_x10)
{
    if (raw == 0u || raw >= BSP_ADC_FULL_SCALE) {
        return false;
    }
    if (raw > BSP_NTC_OPEN_RAW || raw < BSP_NTC_SHORT_RAW) {
        return false;
    }

    /* NTC 接地、10 kΩ 上拉到 Vref：
       Rntc = Rfixed * raw / (4095 - raw)
       T    = 1 / (1/T25 + ln(R/R25)/B) − 273.15                    */
    const float r_fix  = (float)BSP_NTC_R_FIXED_OHM;
    const float r25    = (float)BSP_NTC_R25_OHM;
    const float beta   = (float)BSP_NTC_BETA;
    const float t25_k  = 298.15f;

    /* NTC 接地、10 kΩ 上拉到 Vref 时的分压：
       Rntc = Rfixed * raw / (4095 - raw) */
    float r_ntc = r_fix * (float)raw / (4095.0f - (float)raw);

    float inv_t = (1.0f / t25_k) + (logf(r_ntc / r25) / beta);
    float t_c   = (1.0f / inv_t) - 273.15f;

    /* 限幅到合理范围，防止 NTC 参数异常时出现离谱值 */
    if (t_c < -40.0f) t_c = -40.0f;
    if (t_c > 150.0f) t_c = 150.0f;

    *t_x10 = (int16_t)(t_c * 10.0f);
    return true;
}

/* ------------------------------------------------------------------ */
/* 初始化                                                             */
/* ------------------------------------------------------------------ */
static void adc_common_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    (void)RCC->AHB2ENR;

    /* 同步时钟 = HCLK/4 = 42.5 MHz（G4 ADC 最高 60 MHz） */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE) |
                        ADC_CCR_CKMODE_0 | ADC_CCR_CKMODE_1;
}

static void adc_enable_and_calibrate(ADC_TypeDef *adc)
{
    adc->CR &= ~ADC_CR_DEEPPWD;
    adc->CR |= ADC_CR_ADVREGEN;
    bsp_delay_us(20);

    adc->CR |= ADC_CR_ADEN;
    while ((adc->ISR & ADC_ISR_ADRDY) == 0u) { }

    adc->CR |= ADC_CR_ADCAL;
    while ((adc->CR & ADC_CR_ADCAL) != 0u) { }
}

static void adc_config(ADC_TypeDef *adc, const uint8_t *ch, uint32_t n,
                       uint32_t smpr1, uint32_t smpr2)
{
    /* 12-bit、右对齐、单次、无外部触发（JEXTEN=0 → 软件 JADSTART） */
    adc->CFGR = (adc->CFGR & ~(ADC_CFGR_RES | ADC_CFGR_CONT |
                               ADC_CFGR_EXTEN | ADC_CFGR_JAUTO |
                               ADC_CFGR_JDISCEN)) | ADC_CFGR_JQDIS;

    adc->SMPR1 = smpr1;
    adc->SMPR2 = smpr2;

    uint32_t jsqr = (uint32_t)(n - 1u);      /* JL = n-1 */
    for (uint32_t i = 0; i < n && i < 4u; i++) {
        jsqr |= ((uint32_t)ch[i] & 0x1Fu) << (9u + i * 6u);
    }
    adc->JSQR = jsqr;
}

void bsp_adc_init(void)
{
    adc_common_init();

    adc_enable_and_calibrate(ADC1);
    adc_enable_and_calibrate(ADC2);

    /* ADC1：IN1/IN2/IN3/IN4，全部 12.5 周期
       SMPR2 每个通道 3 bit：SMP1@3, SMP2@6, SMP3@9, SMP4@12 */
    adc_config(ADC1, (const uint8_t[]){1u, 2u, 3u, 4u}, 4u,
               0u,
               (2u << 3) | (2u << 6) | (2u << 9) | (2u << 12));

    /* ADC2：IN3(PA6,NTC 47.5cyc) / IN4(PA7) / IN15(PB0)
       SMPR2: SMP3@9 =5(47.5cyc), SMP4@12 =2(12.5cyc)
       SMPR1: SMP15@15 =2 */
    adc_config(ADC2, (const uint8_t[]){3u, 4u, 15u}, 3u,
               (2u << 15),
               (5u << 9) | (2u << 12));

    /* 清一次残留标志 */
    ADC1->ISR = ADC_ISR_JEOS | ADC_ISR_JQOVF;
    ADC2->ISR = ADC_ISR_JEOS | ADC_ISR_JQOVF;
    s_valid = false;
}

void bsp_adc_start(void)
{
    ADC1->CR |= ADC_CR_JADSTART;
    ADC2->CR |= ADC_CR_JADSTART;
}

void bsp_adc_read(bsp_adc_raw_t *out)
{
    out->vout = (uint16_t)(ADC1->JDR1 & 0x0FFFu);
    out->iout = (uint16_t)(ADC1->JDR2 & 0x0FFFu);
    out->vin  = (uint16_t)(ADC1->JDR3 & 0x0FFFu);
    out->iin  = (uint16_t)(ADC1->JDR4 & 0x0FFFu);

    out->ntc  = (uint16_t)(ADC2->JDR1 & 0x0FFFu);
    out->ocp  = (uint16_t)(ADC2->JDR2 & 0x0FFFu);
    out->ovp  = (uint16_t)(ADC2->JDR3 & 0x0FFFu);

    s_valid = true;
}

bool bsp_adc_valid(void)
{
    return s_valid;
}
