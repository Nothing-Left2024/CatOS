@echo off
REM Run CatOS in QEMU
REM Requires: qemu-system-i386 in PATH
REM
REM 用法:
REM   run.bat              - 默认: 软盘引导 + FAT32 硬盘
REM   run.bat fat16        - 软盘引导 + FAT16 硬盘 (10MB)
REM   run.bat fat32        - 软盘引导 + FAT32 硬盘 (256MB)

set HD_IMG=catos_hd32.img
if /I "%1"=="fat16" set HD_IMG=catos_hd.img
if /I "%1"=="fat32" set HD_IMG=catos_hd32.img

REM 检查镜像是否存在
if not exist catos.img (
    echo [ERROR] catos.img not found. Run: build.sh image
    exit /b 1
)
if not exist %HD_IMG% (
    echo [WARN] %HD_IMG% not found, starting without hard disk.
    qemu-system-i386 -drive file=catos.img,if=floppy,format=raw
) else (
    echo [*] Boot disk : catos.img ^(FAT12 floppy^)
    echo [*] Hard disk : %HD_IMG%
    qemu-system-i386 -drive file=catos.img,if=floppy,format=raw -drive file=%HD_IMG%,format=raw
)
