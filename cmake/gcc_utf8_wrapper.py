# -*- coding: utf-8 -*-
"""
gcc_utf8_wrapper.py —— arm-none-eabi-gcc 输出编码转码包装器
=======================================================================
【为什么需要它】
  Windows 中文版下，arm-none-eabi-gcc（MinGW 运行库）拿到的源码路径里
  若含中文（如 C:/.../项目/...），诊断信息（警告/错误/行号）会按系统
  ANSI 代码页（GBK/936）输出原始字节；而 Ninja / VSCode 终端默认按
  UTF-8 解码 -> 路径显示成 “椤圭洰 / ��Ŀ” 之类乱码。
  已实测：chcp 65001、LANG/LC_ALL=C.UTF-8 对该 gcc 均无效。

【做法】
  本脚本夹在 CMake/Ninja 与 gcc 之间（CMAKE_C_COMPILER_LAUNCHER）：
    ninja -> sccache.bat -> python gcc_utf8_wrapper.py <gcc> <编译参数...>
  用两个后台线程把 gcc 的 stdout/stderr 原始字节按 GB18030（GBK 超集，
  对任意字节都能解码，不会抛异常）增量解码后以 UTF-8 原样转发，
  不改动任何编译参数、不改变退出码，只做编码转换。

【顺带修正 .d 依赖文件的编码】
  gcc 生成的依赖文件（-MF 指定的 CMakeFiles/xxx.obj.d）同样按 ANSI(GBK)
  写路径。Ninja 按 UTF-8 读取，缓存里会变成乱码路径，于是每次都认为
  源文件“脏”，工程放在中文目录时每次编译都会全量重建。
  编译成功后本脚本把该文件从 GB18030 转成 UTF-8（纯 ASCII 时内容不变，
  仍是合法的 UTF-8，直接原样保留），编译失败则不动。

【为什么外层 .bat 叫 sccache.bat】
  clangd 从 compile_commands.json 看到编译命令最前面是本包装器时，
  必须把它识别为“编译启动器”并跳过，否则会把 python/.bat 当成编译器。
  clangd 内置的启动器白名单包括 sccache/ccache/distcc，因此借用名字。
  （与真正的 sccache 编译缓存无任何关系）

【关闭方式】
  配置时加 -DENABLE_GCC_UTF8_WRAPPER=OFF 即可完全不使用本包装器。
=======================================================================
"""

import subprocess
import sys
import threading


def _pump(raw_stream, out_binary, encoding):
    """把一条子进程字节流增量转码转发出去。

    用 IncrementalDecoder 是因为一个汉字占 2 字节，read() 的切块边界
    可能恰好切在汉字中间，整块 decode 会出替换符。
    """
    decoder = __import__('codecs').getincrementaldecoder(encoding)(errors='replace')
    try:
        while True:
            chunk = raw_stream.read(4096)
            if not chunk:
                tail = decoder.decode(b'', final=True)
                if tail:
                    out_binary.write(tail.encode('utf-8'))
                    out_binary.flush()
                break
            text = decoder.decode(chunk)
            if text:
                out_binary.write(text.encode('utf-8'))
                out_binary.flush()
    except Exception:
        # 转发失败也绝不能影响编译本身：静默结束该路转发
        pass


def _fix_depfile_encoding(args):
    """把 gcc 用 GBK 写出的 -MF 依赖文件转成 UTF-8（失败不报错）。

    Ninja 解析依赖文件时按 UTF-8 处理；中文路径若保持 GBK 字节，
    依赖记录就会变成乱码，导致源文件每次都被判定为已修改。
    """
    depfile = None
    for i, arg in enumerate(args):
        if arg == '-MF' and i + 1 < len(args):
            depfile = args[i + 1]
        elif arg.startswith('-MF') and len(arg) > 3:
            depfile = arg[3:]

    if not depfile:
        return

    try:
        with open(depfile, 'rb') as f:
            data = f.read()
        try:
            data.decode('utf-8')       # 纯 ASCII 或本来就是 UTF-8
            return
        except UnicodeDecodeError:
            pass
        text = data.decode('gb18030', errors='replace')
        with open(depfile, 'wb') as f:
            f.write(text.encode('utf-8'))
    except OSError:
        # 依赖文件不存在或无权限：保持 gcc 的原始结果，不影响编译
        pass


def main():
    if len(sys.argv) < 2:
        sys.stderr.write('gcc_utf8_wrapper: missing compiler path\n')
        return 2

    gcc = sys.argv[1]
    args = sys.argv[2:]

    # CREATE_NO_WINDOW：防止 Python 拉起 gcc 时闪黑窗
    creationflags = 0
    if sys.platform == 'win32':
        creationflags = getattr(subprocess, 'CREATE_NO_WINDOW', 0x08000000)

    proc = subprocess.Popen(
        [gcc] + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=creationflags,
    )

    t_out = threading.Thread(
        target=_pump, args=(proc.stdout, sys.stdout.buffer, 'gb18030'),
        daemon=True)
    t_err = threading.Thread(
        target=_pump, args=(proc.stderr, sys.stderr.buffer, 'gb18030'),
        daemon=True)
    t_out.start()
    t_err.start()

    try:
        returncode = proc.wait()
    except KeyboardInterrupt:
        proc.terminate()
        proc.wait()
        return 130

    if returncode == 0:
        _fix_depfile_encoding(args)
    return returncode


if __name__ == '__main__':
    sys.exit(main())
