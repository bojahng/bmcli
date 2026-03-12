@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0..
set PY=%ROOT%\.venv\Scripts\python.exe

for /f "usebackq delims=" %%I in (`"%PY%" -c "import ziglang, pathlib; p=pathlib.Path(ziglang.__file__).resolve().parent/'zig'; pe=p.parent/'zig.exe'; print(pe if pe.exists() else p)"`) do set ZIG=%%I

"%ZIG%" c++ %*

