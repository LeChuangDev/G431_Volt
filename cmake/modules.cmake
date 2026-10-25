# =====================================================================
#  modules.cmake —— 两个小工具函数
# =====================================================================

# ---- stm32_add_lib(库名 [SOURCES ...] [INCLUDE ...]) ----
# 建一个静态库；不写 SOURCES 时自动收集本目录 Src/*.c；
# 存在 Inc/ 目录时自动作为公共头文件路径暴露给使用者
function(stm32_add_lib name)
    cmake_parse_arguments(A "" "" "SOURCES;INCLUDE" ${ARGN})

    if(NOT A_SOURCES)
        file(GLOB A_SOURCES CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c")
    endif()

    add_library(${name} STATIC ${A_SOURCES})

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
        target_include_directories(${name} PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
    endif()
    if(A_INCLUDE)
        target_include_directories(${name} SYSTEM PUBLIC ${A_INCLUDE})
    endif()
endfunction()

# ---- print_size(目标) ----
# 编译完成后自动打印 Flash/RAM 占用（text=代码, data=已初始化变量, bss=零初始化变量）
# WORKING_DIRECTORY + 纯文件名：同顶层 CMakeLists 第 11 步，
# 规避 Windows 中文绝对路径经 cmd.exe 传参被 ANSI 码页转成乱码的问题
function(print_size target)
    add_custom_command(TARGET ${target} POST_BUILD
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_SIZE} "$<TARGET_FILE_NAME:${target}>"
        COMMENT "Firmware size:")
endfunction()