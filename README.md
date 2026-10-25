# LeVolt G431 数控电源固件

STM32G431CBT6（128 KB Flash / 32 KB SRAM）上的可调直流电源固件：
**CV/CC 双环 + 全链路保护 + USB-PD 输入协商 + ESP32 遥测**，全部裸机实现，
基于 **arm-none-eabi-gcc + CMake + Ninja + OpenOCD**，无 HAL / 无 RTOS。

> 实现细节、时序预算、帧格式与保护表见 **[Doc/POWER_FIRMWARE.md](Doc/POWER_FIRMWARE.md)**。
> 本文只讲工程怎么用、代码怎么分层。

基本规格：

| 项目 | 值 |
|---|---|
| 输入 | USB-PD（CH224Q），默认 12 V，按需升到 20 / 28 V |
| 输出电压 | 0 – 36 V（受 PD 源电压 − 3 V 限制） |
| 输出电流 | 0 – 3.00 A（硬件上限） |
| 输出功率 | 随壳温/屏亮动态降额，25 °C 时 42.8 W（屏亮）/ 52.3 W（屏灭） |
| 电压斜率 | ≤ 500 V/s（每 100 µs 最多 50 mV），全量程 72 ms |
| 快环 | 10 kHz（TIM6，ADC 注入组 → 控制律 → DAC） |
| 中环 | 1 kHz（限流仲裁、CV/CC 判定、遥测装帧） |
| 保护响应 | 硬件 OVP 立即，OCP/IOCP 10 ms，NTC 100 ms |
| 校准 | 电压/电流五点 + 仿射系数 K1–K7，存 Flash 最后一页 |

---

## ★ 三条安全约束（改代码前必读）

1. **PB10 OUT_EN 上电必须是高（输出禁止）。** 复位到 GPIO 初始化之间它是高阻，
   依赖板上外部上拉保证关断；`main()` 第一件事就是把它写成高。
2. **外部 DAC80502 复位输出码 = 0 → VDAC = 0 V → 按 `VOUT = 45.000 − 10.000 × VDAC`
   设定出 45 V。** 所以任何使能动作之前都必须先写并确认 0 V 对应码 **58982**。
   `bsp_vdac_init()` 和 `app_control_init()` 各做一次，`s_vset_valid` 是最后一道门。
3. **所有故障默认锁存**（`APP_AUTO_RETRY_MAX = 0`），只能由 `CLEAR_FAULT` 命令清除，
   且清除前会检查 VOUT < 1.5 V、NTC 正常。

---

## 目录结构

```
G431_Volt/
│
├── CMakeLists.txt                # 总装：全局选项 + add_subdirectory(External → BSP → App)
├── CMakePresets.json             # debug / release 预设（CHIP=STM32G431CB）
│
├── cmake/                        # ── 构建系统（与业务代码零耦合）──
│   ├── arm-none-eabi.cmake       #   工具链 + 全局编译标志
│   ├── chip.cmake                #   ★ 芯片参数唯一真源
│   ├── modules.cmake             #   stm32_add_lib / stm32_add_exe / print_size
│   ├── openocd.cmake             #   flash / debug_server / reset
│   └── openocd/{interface,target}/
│
├── Core/                         # ── 芯片层：换芯片才动 ──
│   ├── Startup/startup_stm32g431xx.s
│   ├── System/{system_stm32g4xx.c, init_stubs.c}
│   └── Linker/STM32G431CBTx_FLASH.ld
│
├── Drivers/
│   ├── CMSIS/                    #   CMSIS Core + STM32G4 Device（官方原样）
│   └── External/                 # ★ 片外芯片驱动：只讲协议，不管接在哪个脚
│       ├── dac80502/             #   16-bit 双路 DAC：VSET / ISET
│       └── ch224q/               #   USB-PD 受电控制器
│
├── BSP/                          # ★ 板级支持包：引脚/外设/板级常数
│   ├── Inc/
│   │   ├── bsp.h                 #   ★ App 唯一可见的接口
│   │   ├── bsp_board.h           #   ★ 板级唯一真源：引脚总览、前端比例、Flash 布局
│   │   └── bsp_bitband.h
│   └── Src/
│       ├── bsp_pins.h            #   引脚 → GPIOx / AF 号（与 bsp_board.h 一一对应）
│       ├── bsp_clock.c           #   170 MHz：Boost + PLL，失败回退 HSI
│       ├── bsp_delay.c           #   SysTick 毫秒（WFI）+ DWT 微秒 + App 钩子
│       ├── bsp_gpio.c            #   OUT_EN / SINK_EN / 心跳 / DATA_RDY / PROT_TRIP EXTI
│       ├── bsp_adc.c             #   ADC1+ADC2 注入组，7 通道，软件触发
│       ├── bsp_dac.c             #   内部 DAC1（PA4 = ICTRL 输入限流）
│       ├── bsp_vdac.c            #   外部 DAC80502 输出设定（上电先写 0 V 码）
│       ├── bsp_spi2.c            #   SPI2 从机 + DMA 双缓冲（ESP32 遥测）
│       ├── bsp_i2c.c             #   I2C1 主（CH224Q 0x23）/ 从（ESP32 命令 0x24）
│       ├── bsp_pd.c              #   CH224Q 的板级包装
│       ├── bsp_tim.c             #   TIM6 10 kHz 快环
│       ├── bsp_iwdg.c            #   IWDG 200 ms
│       ├── bsp_flash.c           #   校准页擦写（第 63 页）
│       ├── bsp_uart.c            #   USART1 115200 调试日志
│       └── bsp_led.c             #   板载 LED（PB2）
│
├── App/                          # ── 业务层：不含 stm32g4xx.h，不碰寄存器 ──
│   ├── main.c                    #   初始化顺序（安全相关，勿随意调整）
│   ├── Common/Inc/
│   │   ├── app_config.h          #   所有可调参数：控制/保护/热/通信/校准
│   │   └── app_types.h           #   g_status 状态结构 + 故障位
│   └── Modules/
│       ├── power/
│       │   ├── app_control.c     #   状态机 / 斜率 / CV-CC / 有源下拉
│       │   ├── app_protect.c     #   L3 保护：快环 + 慢环
│       │   ├── app_signal.c      #   采样校准 / DAC 码换算 / Flash 存取
│       │   ├── app_thermal.c     #   功率包络查表与降额仲裁
│       │   └── app_power.c       #   实时调度 + 喂狗门控
│       └── comms/
│           ├── app_comms.c       #   SPI2 遥测/波形 + I2C 命令 + 心跳
│           └── app_pd.c          #   CH224Q 输入协商状态机
│
├── Config/                       # 编译期配置（HAL 裁剪等；本固件不使用 HAL）
├── Middlewares/                  # 预留中间件目录（当前未使用）
├── Doc/POWER_FIRMWARE.md         # 固件设计说明书
├── tools/build.cmake             # 统一编译入口（自动处理路径变化）
└── build/{debug,release}/        # 编译产物
```

### 依赖方向（只能向下）

```
App  ──►  BSP(Inc/bsp.h)  ──►  Drivers/External  ──►  CMSIS / 寄存器
```

- `Drivers/External` 只讲“这颗芯片怎么操作”（寄存器/时序），**不写接在哪个引脚**；
  引脚绑定全部在 `BSP`。换板子只改 `BSP/Inc/bsp_board.h` + `BSP/Src/bsp_pins.h`。
- App 层只 include `app_*.h` 和 `bsp.h`，不直接操作寄存器，便于移植与单元测试。

---

## 实时节拍

| 节拍 | 载体 / 优先级 | 内容 |
|---|---|---|
| 10 kHz（100 µs） | TIM6 / 0 | 启动 ADC 注入组 → 读上一拍 → 换算 → 快保护 → 控制律 → 写 DAC → 波形采样 |
| 1 kHz（1 ms） | SysTick / 15 | 限流仲裁、目标电压、CV/CC 判定、遥测装帧、心跳 |
| 100 ms | 主循环 | NTC 换算、热降额、慢保护、PD 协商、指示灯 |
| 事件 | 主循环 | I2C 命令解析、SPI2 从机恢复、喂 IWDG |

喂狗条件：只有 `bsp_fast_tick_count()` 前进才喂 IWDG（200 ms）。快环停摆即复位。

---

## 引脚总览

| 引脚 | 功能 | 说明 |
|---|---|---|
| PA0 / PA1 | VOUT / IOUT | 输出采样，15:1 分压 / 0.825 V/A |
| PA2 / PA3 | VIN / IIN | 输入采样，0.750 V/A |
| PA6 | NTC | 10 kΩ NTC，壳温/环境温度同源 |
| PA7 / PB0 | OCP_SENSE / OVP_SENSE | 硬件比较器模拟量 |
| PA4 | ICTRL | DAC1_OUT1，输入限流基准 |
| PA8 / PB3 / PB4 / PB5 | VDAC_CS(SPI1) | 外部 DAC80502 |
| PB6 / PB7 | I2C1 SCL/SDA | CH224Q（主 0x23）+ ESP32 命令（从 0x24） |
| PB13 / PB14 / PB15 / PB9 | SPI2 | 从机，NSS=PB9（PB12 留给 nFAULT） |
| PA9 / PA10 | USART1 | 调试日志 115200 |
| PB10 | OUT_EN | **高 = 禁止输出**，上电默认高 |
| PB11 | SINK_EN | 高 = 47 Ω 有源下拉导通 |
| PA15 | PROT_TRIP | 下降沿 EXTI（硬件故障） |
| PB12 | nFAULT | 输入上拉，低有效 |
| PC6 / PC7 | HB_OUT / HB_IN | 双向 1 kHz 心跳 |
| PC8 | DATA_RDY | 遥测帧就绪 |
| PB2 | LED | 板载指示灯 |

> PC6 已被心跳占用。若使用 WeAct Core Board（LED 在 PC6），
> 需改灯脚或换心跳脚，见 [BSP/Src/bsp_led.c](BSP/Src/bsp_led.c) 头部说明。

---

## 通信一览

- **SPI2 从机 → ESP32**：32 字节定长帧，≤10 MHz，1 ms 一帧遥测，
  每 20 ms 用一帧波形（50 Hz）代替；CRC16-CCITT。
- **I2C1 从机 0x24 ← ESP32**：`[0x5A][opcode][len][payload][CRC16]`，
  命令包括设定电压/电流、开关输出、清故障、K1–K7 与五点校准。
- **心跳**：PC6/PC7 各 1 kHz；ESP32 失联 > 1 s 只置 `wireless_alive = false`，
  **输出状态完全不变**。

帧布局与 opcode 表见 [Doc/POWER_FIRMWARE.md](Doc/POWER_FIRMWARE.md) 第 5 节。

---

## 工具链

| 工具 | 本机路径 |
|---|---|
| arm-none-eabi-gcc 15.x | `C:\EmbeddedTools\arm-gcc\bin` |
| CMake ≥ 3.28 | `C:\EmbeddedTools\cmake\bin` |
| Ninja | `C:\EmbeddedTools\ninja` |
| OpenOCD 0.12 | `C:\EmbeddedTools\openocd-general\bin` |

> 工程文件里不写死任何工具链绝对路径：换电脑时只要把上表四个工具加进 `PATH`，
> 就能直接编译，`cmake/arm-none-eabi.cmake` 和 `CMakePresets.json` 都不用改。

## 编译 / 烧录

```bash
# Debug（默认）
cmake --preset debug
cmake --build build/debug

# Release（-Os + LTO + newlib-nano）
cmake --preset release
cmake --build build/release

# 烧录 / 复位 / 起 GDB Server
cmake --build build/debug --target flash
cmake --build build/debug --target reset
cmake --build build/debug --target debug_server
```

统一入口（预设名由 `-DPRESET` 指定，缺省 `debug`）：

```bash
cmake -P tools/build.cmake                    # 编译
cmake -DTARGET=flash -P tools/build.cmake     # 编译并烧录
cmake -DPRESET=release -P tools/build.cmake
```

VSCode 里 `Ctrl+Shift+B` 编译，“烧录 Flash”任务会自动先编译再烧录。

当前固件占用约 **15 KB Flash / 2.2 KB RAM**。

## 移动 / 复制整个工程

把整个文件夹复制或移动到任何位置（路径可以含空格、中文），**不需要修改任何配置**。
`tools/build.cmake` 每次构建前都会核对 `build/debug/CMakeCache.txt` 里记录的工程路径，
目录变了就自动删除旧缓存、重新 `cmake --preset debug`，因此 `build/` 可以跟着一起拷贝。

> 只有绕开该脚本、直接执行 `cmake --build build/debug` 时，移动目录后才需要手动删除 `build/`。

## 时钟 / 延时实现要点

- **时钟**：`bsp_clock_init()` 返回 `int`。G4 的 PLL 为 `M 分频 → N 倍频 → R 分频`，
  170 MHz 必须开 Boost 模式（`PWR_CR5.R1MODE=0` + `PWR_CR1.VOS=Range1`）。
  按 8 MHz HSE：`8 ÷ 2 × 85 ÷ 2 = 170 MHz`，Flash 4WS。
  失败时回退 HSI 并更新 `SystemCoreClock`（返回非 0，程序仍可运行）。
- **毫秒延时**：`bsp_delay_ms()` 等待期间执行 `__WFI()`，CPU 睡眠而不是空转。
- **微秒延时**：`bsp_delay_us()` 按 DWT 周期计数，超过 25 s 也不会溢出。
- **LED**：App 只调用 `bsp_led_init/on/off/toggle()`；换板子改
  [BSP/Src/bsp_led.c](BSP/Src/bsp_led.c) 顶部的 `LED_GPIO / LED_PIN / LED_RCC_EN / LED_ACTIVE_LEVEL`。
  G4 的 GPIO 在 `0x4800_0000`，不在 M4 外设位带区，所以用 `GPIOx->BSRR` 而不是位带。

---

## 上板前必读

固件**尚未在真实硬件上验证**。以下假设必须逐条核对（详见
[Doc/POWER_FIRMWARE.md](Doc/POWER_FIRMWARE.md) 第 8 节）：

1. DAC80502 寄存器地址与 24 位帧格式（SPI mode 1，CONFIG 0x04 / GAIN 0x05 / DAC0 0x08 / DAC1 0x09）。
2. CH224Q 寄存器与电压/电流档位编码（0x23）。
3. 功率级方程 `VOUT = 45.000 − 10.000 × VDAC`、ISET 3.3 V ↔ 3.15 A。
4. 前端比例：VSENSE ÷15、IOUT 825 mV/A、IIN 750 mV/A、ICTRL 3.3 V ↔ 4.5 A。
5. 只有一个 NTC（PA6），壳温与环境温度同源；若有第二路需拆通道。
6. **PB10 必须有硬件上拉**，保证复位到 GPIO 初始化之间输出关断。
7. 热降额表（`app_thermal.c`）当前为估算值，必须用实测风道数据替换。
8. 链接脚本把 FLASH 定义为完整 128 KB，**固件必须 < 126 KB**，否则校准页擦除会擦掉代码。

上板测试建议顺序见 [Doc/POWER_FIRMWARE.md](Doc/POWER_FIRMWARE.md) 第 9 节
（空载上电 → 测 VDAC → PD 协商 → 斜坡 → CC → 热降额 → 拔 ESP32 → 拔 PD 源 → 触发保护 → 校准持久化）。
