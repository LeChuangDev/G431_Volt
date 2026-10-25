# =====================================================================
#  build.cmake —— 配置 / 编译 / 烧录的统一入口（VS Code 任务与命令行共用）
#
#  为什么需要它：
#    CMake 会把【绝对路径】写进 build/ 下的 CMakeCache.txt 和 build.ninja。
#    整个工程文件夹被复制、移动或改名后，缓存仍指向旧路径，直接
#    cmake --build 会报：
#        The current CMakeCache.txt directory ... is different than
#        the directory ... where CMakeCache.txt was created
#    从而卡死编译。本脚本每次执行前先核对缓存里的工程路径：
#      · 缓存不存在 / 不完整 / 来自其它路径 → 自动删掉并重新配置
#      · 路径一致                          → 直接增量编译
#    脚本内所有路径都由脚本自身位置推导，不含任何本机绝对路径，
#    因此整个文件夹可以随便移动，VSCode 任务与命令行都不用改。
#
#  用法（在工程根目录执行）：
#    cmake -P tools/build.cmake                     # 配置(按需) + 编译
#    cmake -DTARGET=flash -P tools/build.cmake      # 配置 + 编译 + 烧录
#    cmake -DTARGET=debug_server -P tools/build.cmake
#    cmake -DTARGET=reset -P tools/build.cmake
#    cmake -DPRESET=release -P tools/build.cmake    # 使用其它预设（如有）
# =====================================================================

cmake_minimum_required(VERSION 3.20)

# 第 1 步：路径与参数（全部由脚本位置推导，与当前工作目录无关）
get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

set(_PRESET "debug")
if(DEFINED PRESET AND NOT "${PRESET}" STREQUAL "")
    set(_PRESET "${PRESET}")
endif()
set(_BUILD_DIR "${_PROJECT_ROOT}/build/${_PRESET}")

if(NOT DEFINED TARGET)
    set(TARGET "")
endif()

# 小工具：把路径统一成 CMake 风格并去掉末尾斜杠，Windows 下再转小写，
# 保证 "C:/Foo" 与 "c:\foo\" 被判为同一路径
function(_normalize_path out_var path)
    file(TO_CMAKE_PATH "${path}" _p)
    string(REGEX REPLACE "/+$" "" _p "${_p}")
    if(WIN32)
        string(TOLOWER "${_p}" _p)
    endif()
    set(${out_var} "${_p}" PARENT_SCOPE)
endfunction()

# 第 2 步：判断现有构建缓存是否属于当前工程目录
set(_need_configure FALSE)
set(_old_home "")
set(_cache_file "${_BUILD_DIR}/CMakeCache.txt")

if(NOT EXISTS "${_cache_file}" OR NOT EXISTS "${_BUILD_DIR}/build.ninja")
    set(_need_configure TRUE)          # 首次编译，或缓存不完整
else()
    # ★ 这里必须用 file(READ) 而不是 file(STRINGS) ★
    # file(STRINGS) 在 Windows 上按本地编码逐行解析，遇到含中文的行会把
    # 非 ASCII 字符当成列表分隔符，路径在第一个中文处被截断，导致“已移动”
    # 误判、每次编译都全量重建。file(READ) 原样读入，再用正则整行提取。
    file(READ "${_cache_file}" _cache_text)

    # 2.1 工程根路径：文件夹被移动/复制后这里会与当前目录不一致
    string(REGEX MATCH "CMAKE_HOME_DIRECTORY:INTERNAL=([^\r\n]*)" _m "${_cache_text}")
    if(_m)
        set(_old_home "${CMAKE_MATCH_1}")
        _normalize_path(_old_home_norm "${_old_home}")
        _normalize_path(_root_norm     "${_PROJECT_ROOT}")
        if(NOT _old_home_norm STREQUAL _root_norm)
            set(_need_configure TRUE)
        endif()
    else()
        set(_need_configure TRUE)      # 缓存损坏，没有源码目录信息
    endif()

    # 2.2 构建目录自身路径：防止把别的工程的 build/ 整个拷进来
    string(REGEX MATCH "CMAKE_CACHEFILE_DIR:INTERNAL=([^\r\n]*)" _m "${_cache_text}")
    if(_m)
        set(_old_cache "${CMAKE_MATCH_1}")
        _normalize_path(_old_cache_norm "${_old_cache}")
        _normalize_path(_build_norm     "${_BUILD_DIR}")
        if(NOT _old_cache_norm STREQUAL _build_norm)
            set(_need_configure TRUE)
        endif()
    endif()
endif()

# 第 3 步：需要时清理并重新配置
#   只删构建产物目录，源码目录不受影响；必须整体删除，因为 build.ninja
#   与 CMakeFiles/ 里散落着旧的绝对路径，只删 CMakeCache.txt 不够。
if(_need_configure)
    if(EXISTS "${_cache_file}")
        message(STATUS "[build] 检测到构建缓存来自其它路径，自动重建：")
        if(NOT "${_old_home}" STREQUAL "")
            message(STATUS "[build]   旧源码目录: ${_old_home}")
        endif()
        message(STATUS "[build]   当前目录  : ${_PROJECT_ROOT}")
    else()
        message(STATUS "[build] 首次构建，正在配置预设 ${_PRESET} ...")
    endif()

    file(REMOVE_RECURSE "${_BUILD_DIR}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" --preset "${_PRESET}"
        WORKING_DIRECTORY "${_PROJECT_ROOT}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "[build] CMake 配置失败（退出码 ${_rc}）")
    endif()
endif()

# 第 4 步：增量编译（TARGET 为空时编译全部；flash 目标会自动先构建固件）
#   不设置 OUTPUT_VARIABLE：子进程输出直接透传到 VSCode 终端，
#   编译进度、gcc 报错和 OpenOCD 烧录日志都能实时看到。
set(_build_cmd "${CMAKE_COMMAND}" --build "${_BUILD_DIR}")
if(NOT "${TARGET}" STREQUAL "")
    list(APPEND _build_cmd --target "${TARGET}")
endif()

execute_process(
    COMMAND ${_build_cmd}
    WORKING_DIRECTORY "${_PROJECT_ROOT}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "[build] 编译/烧录失败（退出码 ${_rc}）")
endif()
