# =====================================================================
#  App 层：全部业务逻辑（状态机 / 控制律 / 保护 / 热降额 / 通信 / 校准）
#  依赖方向：App → bsp → Drivers/External → CMSIS
# =====================================================================
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/Modules/*/Src/*.c")

add_library(app STATIC ${APP_SOURCES})

target_include_directories(app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/power/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/comms/Inc")

# bsp 会继续传播 cmsis 头文件与器件宏
target_link_libraries(app PUBLIC bsp dac80502 ch224q)
