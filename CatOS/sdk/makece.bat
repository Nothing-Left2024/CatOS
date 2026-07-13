@echo off
REM ====================================================================
REM  makece.bat - CatOS CE Compiler (Windows Batch)
REM
REM  Usage:
REM    makece.bat <source> [output_name]
REM
REM  Examples:
REM    makece.bat hello.c           -> hello.ce
REM    makece.bat hello.c myprog    -> myprog.ce
REM    makece.bat prog.asm          -> prog.ce
REM ====================================================================

setlocal enabledelayedexpansion

REM ===== Switch to script directory (sdk\) =====
cd /d "%~dp0"

REM ===== Argument check =====
if "%~1"=="" (
    echo Usage: makece.bat ^<source^> [output_name]
    echo.
    echo Examples:
    echo   makece.bat hello.c           -^> hello.ce
    echo   makece.bat hello.c myprog    -^> myprog.ce
    echo   makece.bat prog.asm          -^> prog.ce
    goto :eof
)

set "SRC=%~1"
set "SRC_BASE=%~nx1"
set "SRC_NAME=%~n1"
set "SRC_EXT=%~x1"
set "OUTNAME=%SRC_NAME%"
if not "%~2"=="" set "OUTNAME=%~2"

set "ELF=!OUTNAME!.elf"
set "OBJ=!OUTNAME!.o"
set "CE=!OUTNAME!.ce"
set "CRT0_OBJ=crt0.o"

if not exist "%SRC%" (
    echo [x] Source not found: %SRC%
    goto :eof
)

echo [*] Detecting toolchain...

REM ===== Detect NASM =====
set "NASM="
for %%P in (nasm nasm.exe) do (
    if not defined NASM where %%P >nul 2>&1 && set "NASM=%%P"
)
if not defined NASM (
    for %%D in ("C:\Program Files\NASM\nasm.exe" "C:\NASM\nasm.exe") do (
        if not defined NASM if exist %%D set "NASM=%%~D"
    )
)
if not defined NASM (
    echo [x] nasm not found, please install or add to PATH
    goto :eof
)
echo [+] NASM    = !NASM!

REM ===== Detect GCC =====
set "CC="
for %%P in (i686-w64-mingw32-gcc x86_64-w64-mingw32-gcc gcc) do (
    if not defined CC where %%P >nul 2>&1 && set "CC=%%P"
)
if not defined CC (
    for %%D in ("C:\MinGW\bin\gcc.exe") do (
        if not defined CC if exist %%D set "CC=%%~D"
    )
)
if not defined CC (
    echo [x] gcc not found, please install or add to PATH
    goto :eof
)
echo [+] CC      = !CC!

REM ===== Detect Python =====
set "PYTHON="
for %%P in (python3 python python.exe) do (
    if not defined PYTHON where %%P >nul 2>&1 && set "PYTHON=%%P"
)
if not defined PYTHON (
    echo [x] python not found, please install or add to PATH
    goto :eof
)
echo [+] PYTHON  = !PYTHON!

REM ===== Compiler flags =====
set "CFLAGS=-m32 -ffreestanding -fno-pic -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-merge-constants -Wall -mno-sse -mno-sse2 -mno-mmx -march=i386 -c"
set "NASMFLAGS=-f win32"
set "LDFLAGS=-m32 -nostdlib -Wl,-T,user.ld -Wl,--image-base=0 -Wl,--section-alignment=1 -Wl,--file-alignment=1 -Wl,-N"

REM ===== Step 1: Build crt0.o if needed =====
echo [*] Checking C runtime (crt0.o)...
if not exist "%CRT0_OBJ%" (
    call "!NASM!" !NASMFLAGS! -o "%CRT0_OBJ%" crt0.asm
    if errorlevel 1 (
        echo [x] Failed to build crt0.o
        goto :eof
    )
    echo [+] Built %CRT0_OBJ%
) else (
    echo [+] crt0.o is up to date
)

REM ===== Step 2: Compile source =====
echo [*] Compiling %SRC_BASE% ...

if /I "%SRC_EXT%"==".c" goto :compile_c
if /I "%SRC_EXT%"==".asm" goto :compile_asm
echo [x] Unsupported file type: %SRC_EXT% (only .c / .asm)
goto :eof

:compile_c
call "!CC!" !CFLAGS! -o "!OBJ!" "!SRC!"
if errorlevel 1 (
    echo [x] Compilation failed
    goto :eof
)
goto :link

:compile_asm
call "!NASM!" !NASMFLAGS! -o "!OBJ!" "!SRC!"
if errorlevel 1 (
    echo [x] Assembly failed
    goto :eof
)

:link
echo [+] Compiled: !OBJ!

REM ===== Step 3: Link =====
echo [*] Linking -^> !ELF! ...
call "!CC!" !LDFLAGS! -o "!ELF!" "!CRT0_OBJ!" "!OBJ!" 2>nul
if not exist "!ELF!" (
    call "!CC!" !LDFLAGS! -o "!ELF!" "!CRT0_OBJ!" "!OBJ!"
)
if not exist "!ELF!" (
    echo [x] Linking failed
    goto :eof
)
echo [+] Linked: !ELF!

REM ===== Step 4: Pack to CE =====
echo [*] Packing CE v1 -^> !CE! ...
call "!PYTHON!" mkcate.py "!ELF!" "!CE!" "!OUTNAME!"
if not exist "!CE!" (
    echo [x] Packing failed: !CE! not generated
    goto :eof
)

REM ===== Step 5: Report =====
for %%F in ("!CE!") do set "CE_SIZE=%%~zF"
echo.
echo [+] Build successful!
echo     Output: !CE! (!CE_SIZE! bytes)
echo.

REM ===== Clean intermediate files =====
if exist "!OBJ!" del "!OBJ!" >nul 2>&1
if exist "!ELF!" del "!ELF!" >nul 2>&1
if exist "!CE!.bin" del "!CE!.bin" >nul 2>&1
echo [*] Cleaned intermediate files

endlocal
exit /b 0
