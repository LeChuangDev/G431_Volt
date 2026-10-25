# =====================================================================
#  arm-none-eabi.cmake —— 交叉编译工具链定义
#  CMake 天生不认识 ARM 裸机，必须用这个文件告诉它"编译器是谁、目标是谁"
#  用法(由 CMakePresets.json 自动传入，或手动):
#    cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
# =====================================================================

# 第 1 步：声明目标是"裸机"，不是 Windows/Linux
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 第 2 步：跳过 CMake 的"编译器能否链接"测试
# 裸机程序没有 Linux 的启动环境，让 CMake 真去链接测试程序必然失败，
# 所以只让它测试"能编译成静态库"就放行
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 第 3 步：指定编译器
# 写不带 .exe 的名字即可，CMake 在 Windows 上会自己补后缀
set(CMAKE_C_COMPILER    arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER  arm-none-eabi-gcc)   # 汇编也用 gcc 驱动(.s 支持预处理)
set(CMAKE_OBJCOPY       arm-none-eabi-objcopy)
set(CMAKE_SIZE          arm-none-eabi-size)
# Release 用了 -flto：静态库必须用 gcc-ar/gcc-ranlib 生成符号索引，
# 否则普通 ar 不加载 LTO 插件，链接器会报 "undefined reference to bsp_xxx"
set(CMAKE_AR            arm-none-eabi-gcc-ar)
set(CMAKE_RANLIB        arm-none-eabi-gcc-ranlib)

# 第 4 步：内核与 FPU 标志 ★ 换内核才改这里（同系列换型号不用动）★
#   G431 = Cortex-M4F + 单精度 FPU
#   无 FPU 的系列(F1/F0/G0)要把下面两行 -mfpu/-mfloat-abi 整行删掉
#   F7/H7 双精度要改成 -mfpu=fpv5-d16
#
# ★这里必须用【引号包成一个空格分隔的字符串】，不能写成多行无引号的列表！
#   CMAKE_*_FLAGS 是字符串变量，列表(分号分隔)插值后会变成
#   "-mcpu=cortex-m4;-mthumb;..."，gcc 把整串当成一个 -mcpu 参数，
#   配置阶段编译器测试直接报 "unrecognized -mcpu target: cortex-m4;-mthumb;..."
set(CPU_FLAGS
    "-mcpu=cortex-m4 \
     -mthumb \
     -mfpu=fpv4-sp-d16 \
     -mfloat-abi=hard")

# 第 5 步：把 CPU 标志注入所有编译/汇编阶段
set(CMAKE_C_FLAGS_INIT        "${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT      "${CPU_FLAGS}")
# 第 6 步：链接阶段也必须带同样标志——否则链接器会去挑软浮点库，
# 报 "uses VFP register arguments ... does not" 错误
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS}")

# 第 7 步：Windows 下编译器输出 GBK→UTF-8 转码启动器
#   arm-none-eabi-gcc 在中文 Windows 对含中文的路径输出 GBK 字节，
#   Ninja/VSCode 按 UTF-8 显示就会乱码（chcp/LANG 均无效，已实测）。
#   sccache.bat -> Python 包装器夹在编译命令最前面做转码；
#   clangd 能识别 sccache 这个名字并自动跳过，不影响静态分析。
#   其他平台或不需要时：cmake -DENABLE_GCC_UTF8_WRAPPER=OFF
set(ENABLE_GCC_UTF8_WRAPPER ON CACHE BOOL "用包装器把 gcc 的 GBK 诊断流转成 UTF-8")
if(CMAKE_HOST_WIN32 AND ENABLE_GCC_UTF8_WRAPPER)
    set(_UTF8_LAUNCHER "${CMAKE_CURRENT_LIST_DIR}/sccache.bat")
    if(EXISTS "${_UTF8_LAUNCHER}")
        # 注意：CMake 的 CMAKE_<LANG>_COMPILER_LAUNCHER 不支持 ASM 语言，
        # 这里只设 C；汇编（含大写 .S）的转码由顶层 CMakeLists 的
        # CMAKE_ASM_COMPILE_OBJECT 规则补上。
        set(CMAKE_C_COMPILER_LAUNCHER "${_UTF8_LAUNCHER}")
    endif()
endif()