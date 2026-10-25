# =====================================================================
#  openocd.cmake —— 把 OpenOCD 包装成 CMake 目标
#  接口固定为 DAP（cmsis-dap），target 由 chip.cmake 的芯片自动对应
#
#  ★Windows 中文路径对策（与顶层 objcopy 同一个坑）★
#  Ninja 经 cmd.exe 把命令行按 ANSI 码页转出去，openocd/objcopy 的
#  运行库会把中文路径解析成乱码。因此本文件保证 openocd 的【所有命令行
#  参数都不含中文】：
#    1) WORKING_DIRECTORY 设到 build 目录 —— 由 CreateProcessW 的 Unicode
#       接口设置工作目录，不走命令行；
#    2) 配置目录用相对路径 -s ../../cmake/openocd（纯 ASCII）；
#    3) 固件只给文件名，不给绝对路径。
# =====================================================================

# 第 1 步：找 openocd 可执行文件
find_program(OPENOCD_EXECUTABLE openocd)
if(NOT OPENOCD_EXECUTABLE)
    message(WARNING "未找到 openocd，flash/debug 目标不可用")
    return()
endif()

# 第 2 步：从 build 目录到 cmake/openocd 的相对路径（纯 ASCII）
#   build/debug 下即为 ../../cmake/openocd
file(RELATIVE_PATH _ocd_search_rel "${CMAKE_BINARY_DIR}"
                   "${CMAKE_SOURCE_DIR}/cmake/openocd")

# 第 3 步：公共参数
#   -s 追加配置搜索目录；-f 顺序必须先接口后芯片
set(_OCD_ARGS
    -s "${_ocd_search_rel}"
    -f "interface/${OPENOCD_INTERFACE_CFG}"
    -f "target/${OPENOCD_TARGET_CFG}")

# 第 4 步：烧录目标: cmake --build build/debug --target flash
#   program <固件> verify reset exit = 烧录 + 回读校验 + 复位运行 + 退出
add_custom_target(flash
    COMMAND ${OPENOCD_EXECUTABLE} ${_OCD_ARGS}
            -c "program $<TARGET_FILE_NAME:${PROJECT_NAME}> verify reset exit"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "OpenOCD(DAP) 烧录并校验复位"
    USES_TERMINAL)      # 输出实时刷到终端

# 第 5 步：调试服务器目标: cmake --build build/debug --target debug_server
#   起 GDB Server 监听 localhost:3333，之后用 arm-none-eabi-gdb 或
#   VSCode 的 Cortex-Debug 插件连接
add_custom_target(debug_server
    COMMAND ${OPENOCD_EXECUTABLE} ${_OCD_ARGS}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "GDB Server 监听 :3333 (Ctrl+C 退出)"
    USES_TERMINAL)

# 第 6 步：复位目标
add_custom_target(reset
    COMMAND ${OPENOCD_EXECUTABLE} ${_OCD_ARGS} -c "init; reset; exit"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "复位目标芯片"
    USES_TERMINAL)
