@echo off
setlocal enabledelayedexpansion

pushd "%~dp0"

if not exist Makefile (
    echo [ERROR] Makefile not found. This script must run in the CatOS project root.
    popd
    exit /b 1
)

:menu
cls
echo ========================================
echo   CatOS Project Cleanup Script
echo ========================================
echo.
echo   Current directory: %cd%
echo.
echo   Please select cleanup level:
echo.
echo   [1] Light  - Clean object files only (*.o, *.fixed.o)
echo   [2] Normal - Clean build artifacts (objects + bins + elf + img + log)
echo   [3] Full   - Clean everything including temp files
echo   [0] Exit
echo.
set /p choice="Enter your choice (0-3): "

if "%choice%"=="0" goto :eof
if "%choice%"=="1" goto light
if "%choice%"=="2" goto normal
if "%choice%"=="3" goto full

echo Invalid choice, please try again.
pause
goto menu

:light
set level_name=Light
goto confirm

:normal
set level_name=Normal
goto confirm

:full
set level_name=Full
goto confirm

:confirm
echo.
set /p confirm="You selected [%level_name%] cleanup. Continue? (Y/N): "
if /i not "%confirm%"=="Y" (
    echo Cleanup cancelled.
    popd
    exit /b 0
)

echo.
echo Cleaning...
echo.

set count=0
set step=0

call :clean_objects
if "%choice%"=="1" goto done

call :clean_binaries
call :clean_elf
call :clean_img
call :clean_log
if "%choice%"=="2" goto done

call :clean_temp
call :clean_dasm
goto done

:clean_objects
set /a step+=1
echo [%step%] Deleting object files (*.o, *.fixed.o)...
for /r %%f in (*.o *.fixed.o) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
goto :eof

:clean_binaries
set /a step+=1
echo [%step%] Deleting binary files (*.bin)...
for /r %%f in (*.bin) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
goto :eof

:clean_elf
set /a step+=1
echo [%step%] Deleting ELF files (*.elf)...
for %%f in (*.elf) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
goto :eof

:clean_img
set /a step+=1
echo [%step%] Deleting disk images (*.img)...
for %%f in (*.img) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
goto :eof

:clean_log
set /a step+=1
echo [%step%] Deleting log files (*.log)...
for %%f in (*.log) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
goto :eof

:clean_temp
set /a step+=1
echo [%step%] Deleting temporary files...
for %%f in (.tmp_* *.tmp *.bak *~) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
goto :eof

:clean_dasm
set /a step+=1
echo [%step%] Deleting disassembly output...
if exist kernel.bin.asm (
    del /f /q kernel.bin.asm 2>nul
    set /a count+=1
)
goto :eof

:done
echo.
echo ========================================
echo   Cleanup complete!
echo   Level: %level_name%
echo   Files removed: %count%
echo ========================================
echo.
pause

popd
endlocal
