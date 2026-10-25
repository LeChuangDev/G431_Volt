@echo off
rem =====================================================================
rem  sccache.bat - compiler launcher shim (ASCII-only on purpose!)
rem
rem  This file MUST stay pure ASCII with no BOM: cmd.exe parses .bat files
rem  using the local OEM codepage (GBK/936 on Chinese Windows), so any
rem  UTF-8 Chinese comment bytes get mis-decoded and split into garbage
rem  commands. All Chinese documentation lives in gcc_utf8_wrapper.py.
rem
rem  Why the name "sccache": clangd treats sccache/ccache/distcc at the
rem  front of a compile command as a launcher and strips it automatically,
rem  so compile_commands.json semantics are unaffected. It has nothing to
rem  do with the real sccache build cache.
rem
rem  Invoked by CMAKE_C_COMPILER_LAUNCHER as:
rem    sccache.bat ^<arm-none-eabi-gcc path^> ^<gcc args...^>
rem  It forwards everything to gcc_utf8_wrapper.py, which converts the
rem  compiler's GBK diagnostic stream to UTF-8 and passes the exit code.
rem
rem  Disable with: cmake -DENABLE_GCC_UTF8_WRAPPER=OFF
rem =====================================================================
setlocal
set "WRAP_PY=%~dp0gcc_utf8_wrapper.py"

rem -B: do not write __pycache__ bytecode (keeps Python install dir read-only)
rem Prefer the official "py" launcher; fall back to python on PATH (error 9009)
py -3 -B "%WRAP_PY%" %*
if not "%ERRORLEVEL%"=="9009" exit /b %ERRORLEVEL%

python -B "%WRAP_PY%" %*
exit /b %ERRORLEVEL%
