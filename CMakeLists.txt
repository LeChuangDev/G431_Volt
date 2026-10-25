# =====================================================================
#  STM32G431 寄存器版工程总装脚本
#  职责：把 BSP / Core / CMSIS 装进一个可执行文件，并产出 hex/bin
# =====================================================================

# 第 1 步：CMake 最低版本
cmake_minimum_required(VERSION 3.20)

# 第 2 步：注册工具函数所在目录（include(modules) 能找得到）
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(modules)

# 第 3 步：读取芯片参数（CHIP 唯一真源在这里面）
include(chip)

# 第 4 步：工程定义
#   LANGUAGES C ASM 不能少——ASM 让 .s 启动文件被正确编译
project(STM32G431_Register
    VERSION 1.0.0
    LANGUAGES C ASM)

# 第 4.1 步：汇编文件也套上 GBK→UTF-8 包装器
#   CMake 的 CMAKE_<LANG>_COMPILER_LAUNCHER 只对 C/CXX 等语言生效，
#   ASM 语言会被忽略；而带预处理的大写 .S 启动文件同样会生成 .d 依赖文件，
#   中文目录下 GBK 路径会让 Ninja 每次都判定它“脏”并重建。
#   这里把包装器直接写进 ASM 编译规则，效果与 C 源文件一致。
if(CMAKE_HOST_WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/cmake/sccache.bat")
    set(CMAKE_ASM_COMPILE_OBJECT
        "\"${CMAKE_SOURCE_DIR}/cmake/sccache.bat\" <CMAKE_ASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
endif()

# 第 4.2 步：固定 C 语言标准
#   不同 GCC 版本的默认标准会变（GCC 15 已默认 gnu23），显式钉在 gnu11，
#   避免换工具链后语法/隐式声明规则悄悄改变。
set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        ON)

# 第 5 步：没指定构建类型时默认 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# 第 6 步：全局编译选项
# ---- 路径前缀重映射：把工程根(可能含中文，如"项目")替换成相对名 "." ----
#   作用（实测注意：gcc 主文件名诊断【不受】此选项影响，终端里警告/错误
#   路径的 GBK 乱码由 cmake/sccache.bat + gcc_utf8_wrapper.py 转码解决）：
#   1) __FILE__ 宏、assert/日志里的文件名变成相对路径，不暴露本机用户名目录；
#   2) DWARF 调试信息与构建目录解耦，换机器/换目录路径仍一致（可复现构建）。
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" _PROJECT_ROOT_CANON)
add_compile_options(-ffile-prefix-map=${_PROJECT_ROOT_CANON}=.)

add_compile_options(
    -Wall -Wextra                       # 打开警告（自己的代码建议全清掉）
    -fdata-sections -ffunction-sections # 每个函数/变量单独成段
    $<$<CONFIG:Debug>:-Og>               # -Og: 专门为调试优化的等级
    $<$<CONFIG:Debug>:-g3>               # -g3: 连宏定义都能在调试器里展开
    $<$<CONFIG:Release>:-Os>             # 发布版以体积最小为目标
    $<$<CONFIG:Release>:-flto>           # Release 链接时优化：跨文件内联+死代码消除
)

# 第 7 步：引入 BSP / 外部芯片驱动 / App 子目录
add_subdirectory(Drivers/External)        # dac80502、ch224q（被 bsp 依赖，必须先定义）
add_subdirectory(BSP)                     # bsp、cmsis
add_subdirectory(App)                     # app（业务逻辑静态库）

# 第 8 步：可执行文件 = main.c + 启动文件 + system 文件
#   注意：启动文件和 system 文件是"芯片层"的，直接编进最终固件
#   init_stubs.c 补 _init/_fini：官方 G4 启动文件会调 __libc_init_array，
#   而 -nostartfiles 不会链入 crti.o，缺少 _init 会链接失败
add_executable(${PROJECT_NAME}
    App/main.c
    ${STARTUP_FILE}                                          # 向量表
    ${CMAKE_SOURCE_DIR}/Core/System/system_${STM32_FAMILY_LC}xx.c   # SystemInit
    ${CMAKE_SOURCE_DIR}/Core/System/init_stubs.c)            # _init/_fini

# 第 9 步：链接关系
#   链接 bsp 会自动带入 cmsis 的头文件路径/器件宏/HSE_VALUE
#   -T 指定链接脚本（由 chip.cmake 自动选好）
#   ★ 必须用【相对构建目录】的路径：链接命令的工作目录就是 build/debug，
#     写成绝对路径时，一旦工程放在带空格的目录（如 "My Projects"）里，
#     ninja 传给 cmd 的 -T 参数不会加引号，gcc 会把路径从空格处拆成两个
#     参数，报 "cannot open linker script file ...: Invalid argument"。
#     相对路径 ../../Core/... 永远是纯 ASCII、无空格，彻底躲开这个坑。
file(RELATIVE_PATH LDSCRIPT_REL "${CMAKE_BINARY_DIR}" "${LDSCRIPT}")
target_link_libraries(${PROJECT_NAME} PRIVATE
    app
    -T${LDSCRIPT_REL}
    -lc -lm -lnosys          # C 库 + 数学库 + 裸机空桩库
)

# 第 10 步：链接选项
target_link_options(${PROJECT_NAME} PRIVATE
    -nostartfiles             # 不用 GCC 默认启动代码，用 ST 的 startup_xxx.s
    -Wl,--gc-sections         # 垃圾回收：删掉没被引用的函数/变量，省 Flash
    -Wl,--print-memory-usage  # 链接完打印 RAM/Flash 占用百分比
    -Wl,-Map=${PROJECT_NAME}.map  # 生成 map 文件（相对构建目录，同第 9 步）
    -specs=nano.specs         # newlib-nano：以后用 printf/sprintf 可省大量 Flash
    $<$<CONFIG:Release>:-flto>    # 与编译阶段的 -flto 配套
)

# 第 11 步：编译后自动生成 .hex / .bin（烧录和量产要用）
# ★ Windows + 中文路径注意★ 不能把含中文的绝对路径直接当参数传给 objcopy：
#   Ninja 经 cmd.exe 传参时按系统 ANSI 码页编码，objcopy 的 CRT 会把
#   "项目"解析成乱码而报 "No such file or directory"。
#   解决：把工作目录切到产物目录，参数里只出现【纯文件名】。
#   WORKING_DIRECTORY 由 Ninja 用 CreateProcessW 的 Unicode 接口设置，不受码页影响。
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_OBJCOPY} -O ihex   "$<TARGET_FILE_NAME:${PROJECT_NAME}>" "${PROJECT_NAME}.hex"
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE_NAME:${PROJECT_NAME}>" "${PROJECT_NAME}.bin"
    COMMENT "Generating hex / bin")
print_size(${PROJECT_NAME})   # 打印体积

# 第 12 步：OpenOCD 烧录/调试目标（DAP 配置，见 openocd.cmake）
include(openocd)