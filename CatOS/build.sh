#!/bin/bash
######################################################################
#  CatOS Build Script - 保护模式命令行编译脚本
#
#  用法: ./build.sh [all|clean|rebuild|image]
#
#  编译策略（适配 MinGW/Git Bash）：
#    NASM  -> -f win32 (COFF格式，符号无_前缀)
#    GCC   -> -m32     (COFF格式，符号有_前缀)
#    objcopy -> --remove-leading-char  (去除C符号的_前缀，与ASM匹配)
#    gcc 链接所有COFF对象 -> kernel.elf (PE) -> kernel.bin (纯二进制)
#    Loader的InitKernel自动检测: ELF→按PH加载 / 非ELF→直接拷贝到0x30400
#    image 目标 -> 构建 catos.img (1.44MB 软盘镜像)
######################################################################

set -e

##################### 确保工具链在PATH中 #####################
export PATH="/c/Program Files/NASM:$PATH"
[ -x "/c/Program Files/NASM/nasm.exe" ] && NASM="/c/Program Files/NASM/nasm.exe"
command -v nasm >/dev/null 2>&1 || NASM="nasm"

##################### 切换到项目根目录 #####################
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

##################### 配置 #####################
ASM=""
for candidate in \
	"/mnt/c/Program Files/NASM/nasm.exe" \
	"/mnt/c/NASM/nasm.exe" \
	"/c/Program Files/NASM/nasm.exe" \
	"/c/NASM/nasm.exe" \
	/usr/bin/nasm \
	"nasm"; do
	if [ -f "$candidate" ] 2>/dev/null; then
		ASM="$candidate"
		break
	fi
done
if [ -z "$ASM" ]; then
	echo "Warning: nasm not found, hoping it's in PATH..."
	ASM="nasm"
fi
echo "[DEBUG] ASM=$ASM"

# 自动检测 MinGW 工具链 (兼容 Git Bash / MINGW64 / WSL)
# 注意: 必须实际测试编译, 某些环境下交叉gcc会静默崩溃
if [ -z "$CC" ]; then
	_test_src=".tmp_catos_test_$$.c"
	_test_obj=".tmp_catos_test_$$.o"
	echo 'int x;' > "$_test_src"
	for _candidate_gcc in \
		"i686-w64-mingw32-gcc" \
		"x86_64-w64-mingw32-gcc" \
		"gcc"; do
		if command -v "$_candidate_gcc" >/dev/null 2>&1; then
			_cgc="$(command -v "$_candidate_gcc")"
			# 测试能否真正编译
			if "$_cgc" -c "$_test_src" -o "$_test_obj" -m32 2>/dev/null; then
				CC="$_cgc"
				break
			fi
		fi
	done
	# 回退: 直接探测常见安装路径
	if [ -z "$CC" ]; then
		for _gcc in \
			"/mnt/f/Tools/MSY32/mingw32/bin/i686-w64-mingw32-gcc.exe" \
			"/f/Tools/MSY32/mingw32/bin/i686-w64-mingw32-gcc.exe" \
			"/c/MinGW/bin/gcc.exe" \
			"/mingw32/bin/gcc.exe"; do
			if [ -f "$_gcc" ]; then
				if "$_gcc" -c "$_test_src" -o "$_test_obj" -m32 2>/dev/null; then
					CC="$_gcc"
					_dir="$(dirname "$_gcc")"
					_oc="$_dir/objcopy.exe"
					[ -f "$_oc" ] && OBJCOPY="$_oc"
					break
				fi
			fi
		done
	fi
	rm -f "$_test_src" "$_test_obj" 2>/dev/null
fi
# 找 objcopy
if [ -z "$OBJCOPY" ]; then
	for _oc in \
		"$(dirname "$CC")/objcopy.exe" \
		"objcopy" "objcopy.exe"; do
		command -v "$_oc" >/dev/null 2>&1 && OBJCOPY="$(command -v "$_oc")" && break
	done
fi
echo "[DEBUG] CC=$CC OBJCOPY=$OBJCOPY"

ASMBFLAGS="-I boot/include"
ASMKFLAGS="-I include -f win32"
CFLAGS="-I include -c -fno-builtin -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-merge-constants -fno-pie -fno-asynchronous-unwind-tables"
LDFLAGS="-m32 -nostdlib -Wl,-T,kernel.ld,-e,cstart"

BOOT_BIN="boot/boot.bin"
HDBOOT_BIN="boot/hdboot.bin"
LOADER_BIN="boot/loader.bin"
MBR_BIN="boot/mbr.bin"
KERNEL_BIN="kernel.bin"
KERNEL_ELF="kernel.elf"
IMG_FILE="catos.img"

ASM_SOURCES=(
	"kernel/kernel.asm"
	"kernel/syscall.asm"
	"lib/klib.asm"
	"lib/string.asm"
)

C_SOURCES=(
	"kernel/start.c"
	"kernel/main.c"
	"kernel/clock.c"
	"kernel/i8259.c"
	"kernel/global.c"
	"kernel/protect.c"
	"kernel/proc.c"
	"kernel/exec.c"
	"kernel/syscall_c.c"
	"kernel/keyboard.c"
	"kernel/mouse.c"
	"kernel/tty.c"
	"kernel/console.c"
	"kernel/shell.c"
	"kernel/taskmgr.c"
	"kernel/installer.c"
	"kernel/hd.c"
	"kernel/fat.c"
	"kernel/editor.c"
	"kernel/gfx.c"
	"kernel/wm.c"
	"kernel/printf.c"
	"kernel/vsprintf.c"
	"lib/klibc.c"
	"kernel/test.c"
)

ALL_OBJS=()

##################### 函数 #####################

print_banner() {
	echo ""
	echo "========================================="
	echo "       CatOS Protected Mode Shell"
	echo "           Build System v1.0"
	echo "========================================="
	echo ""
}

check_tools() {
	echo "[*] Checking toolchain..."
	if [ -x "$ASM" ] || command -v "$ASM" >/dev/null 2>&1; then
		echo "    ASM     : $ASM"
	else
		echo "Error: '$ASM' not found."; exit 1; fi
	command -v "$CC" >/dev/null 2>&1 || { echo "Error: '$CC' not found."; exit 1; }
	command -v "$OBJCOPY" >/dev/null 2>&1 || { echo "Error: '$OBJCOPY' not found."; exit 1; }
	echo "    CC      : $(command -v $CC)"
	echo "    OBJCOPY : $(command -v $OBJCOPY)"
	echo ""
}

compile_boot() {
	echo "[*] Compiling boot sector..."
	"$ASM" $ASMBFLAGS -o "$BOOT_BIN" boot/boot.asm
	echo "    OK: boot/boot.bin"
	echo "[*] Compiling HD boot sector..."
	"$ASM" $ASMBFLAGS -o "$HDBOOT_BIN" boot/hdboot.asm
	echo "    OK: boot/hdboot.bin"
	echo "[*] Compiling MBR..."
	"$ASM" $ASMBFLAGS -o "$MBR_BIN" boot/mbr.asm
	echo "    OK: boot/mbr.bin"
	echo "[*] Compiling loader..."
	"$ASM" $ASMBFLAGS -o "$LOADER_BIN" boot/loader.asm
	echo "    OK: boot/loader.bin"
}

compile_kernel_asm() {
	echo "[*] Compiling assembly sources (win32 COFF)..."
	for src in "${ASM_SOURCES[@]}"; do
		obj="${src%.*}.o"
		echo "    ASM -> $obj"
		"$ASM" $ASMKFLAGS -o "$obj" "$src"
		ALL_OBJS+=("$obj")
	done
}

compile_kernel_c() {
	local extra_def=""
	if [ -n "$1" ]; then
		extra_def="-DKERNEL_FILE_SIZE=$1"
		echo "[*] Compiling C sources (KERNEL_FILE_SIZE=$1)..."
	else
		echo "[*] Compiling C sources..."
	fi
	for src in "${C_SOURCES[@]}"; do
		obj="${src%.*}.o"
		obj_fixed="${src%.*}.fixed.o"
		echo "    CC  -> $obj (fixup symbols)"
		"$CC" $CFLAGS $extra_def -o "$obj" "$src"
		"$OBJCOPY" --remove-leading-char "$obj" "$obj_fixed" 2>/dev/null || cp "$obj" "$obj_fixed"
		ALL_OBJS+=("$obj_fixed")
	done
}

link_kernel() {
	echo "[*] Linking kernel (PE)..."
	$CC $LDFLAGS -o "$KERNEL_ELF" "${ALL_OBJS[@]}"
	echo "    OK: $KERNEL_ELF ($(wc -c < "$KERNEL_ELF") bytes)"

	echo "[*] Converting to flat binary..."
	"$OBJCOPY" -O binary -j .text -j .data -j .rdata \
		"$KERNEL_ELF" "$KERNEL_BIN"
	echo "    OK: $KERNEL_BIN ($(wc -c < "$KERNEL_BIN") bytes)"
}

#======================================================================
#  构建软盘镜像 — 直接扇区布局, 兼容多种环境
#======================================================================
build_image() {
	do_build

	echo ""
	echo "[*] Building floppy image: $IMG_FILE"

	local IMG_SZ=1474560          # 2880 * 512 = 1.44MB
	local BPS=512

	# ---- 1. 创建空白镜像 (dd回退法最兼容) ----
	dd if="$BOOT_BIN" of="$IMG_FILE" bs=$BPS count=1 2>/dev/null
	dd if="$BOOT_BIN" of="$IMG_FILE" bs=1 seek=$((IMG_SZ-1)) count=1 conv=notrunc 2>/dev/null || true
	echo "    Blank image created ($IMG_SZ bytes)"

	# ---- 2. 写入 Boot Sector (扇区 0) ----
	dd if="$BOOT_BIN" of="$IMG_FILE" bs=$BPS count=1 conv=notrunc 2>/dev/null
	echo "    Boot sector written (sector 0)"

	# ---- 3. 写入 LOADER.BIN (从扇区 2 开始) ----
	dd if="$LOADER_BIN" of="$IMG_FILE" bs=$BPS seek=2 conv=notrunc 2>/dev/null
	local LOADER_SECS=$(( ($(wc -c < "$LOADER_BIN") + 511) / 512 ))
	echo "    LOADER.BIN written @ sector 2 ($LOADER_SECS sectors)"

	# ---- 4. 写入 KERNEL.BIN (从扇区 34 开始, 因为 loader 占扇区 2~33) ----
	dd if="$KERNEL_BIN" of="$IMG_FILE" bs=$BPS seek=34 conv=notrunc 2>/dev/null
	local KERNEL_SECS=$(( ($(wc -c < "$KERNEL_BIN") + 511) / 512 ))
	echo "    KERNEL.BIN written @ sector 34 ($KERNEL_SECS sectors)"

	# ---- 5. 验证并完成 ----
	local FINAL_SZ=$(wc -c < "$IMG_FILE")
	if [ "$FINAL_SZ" -ne "$IMG_SZ" ]; then
		echo "  WARNING: size ${FINAL_SZ} != ${IMG_SZ}, fixing..."
		command -v truncate >/dev/null 2>&1 && truncate -s $IMG_SZ "$IMG_FILE"
		FINAL_SZ=$(wc -c < "$IMG_FILE")
	fi

	echo ""
	echo "========================================="
	echo "  Image created: $IMG_FILE"
	echo "  Size: ${FINAL_SZ} bytes ($(($FINAL_SZ / 1024)) KiB)"
	echo ""
	echo "  Layout:"
	echo "    Sector 0   : Boot sector ($BOOT_BIN)"
	echo "    Sector 2+  : LOADER.BIN ($LOADER_SECS sectors)"
	echo "    Sector 34+ : KERNEL.BIN ($KERNEL_SECS sectors)"
	echo "========================================="
	echo ""
	echo "  Run with Bochs:"
	echo "    bochs -f bochsrc.bxrc"
	echo ""
}

do_clean() {
	echo "[*] Cleaning..."
	rm -f "$BOOT_BIN" "$HDBOOT_BIN" "$MBR_BIN" "$LOADER_BIN" "$KERNEL_BIN" "$KERNEL_ELF" "$IMG_FILE" catos_hd.img catos_hd32.img catos.iso
	rm -f .fat_tmp .fat_full .root_dir_tmp 2>/dev/null || true
	rm -rf .iso_tmp_* 2>/dev/null || true
	find . -name "*.o" -not -path "./boot/*" -delete 2>/dev/null || true
	find . -name "*.fixed.o" -delete 2>/dev/null || true
	find . -name "*.elf.o" -delete 2>/dev/null || true
	echo "    Clean done."
}

#======================================================================
#  构建 FAT16 硬盘镜像 (用于 diskinfo/dir/type 命令测试)
#  创建 10MB FAT16 superfloppy 格式镜像, 包含示例文件
#======================================================================
build_hd_image() {
	local HD_IMG="catos_hd.img"

	echo ""
	echo "[*] Building FAT16 hard disk image: $HD_IMG"

	python3 - "$HD_IMG" <<'PYEOF'
import sys, struct

img_path = sys.argv[1]

# FAT16 参数 (10MB superfloppy)
BPS       = 512
SEC_PER_CLUS = 4
RSVDSEC   = 1
NUMFATS   = 2
ROOTENTS  = 512
TOTSEC    = 20480     # 10MB / 512 = 20480 sectors
MEDIA     = 0xF8

# 计算 FAT 大小
ROOTDIR_SECS = (ROOTENTS * 32 + BPS - 1) // BPS  # 32
DATA_SEC = TOTSEC - RSVDSEC - ROOTDIR_SECS
TOTAL_CLUSTERS = DATA_SEC // SEC_PER_CLUS
FATSEC = (TOTAL_CLUSTERS * 2 + BPS - 1) // BPS   # FAT16: 2 bytes/entry

# 重新计算 (FAT 占用扇区也要算进去)
DATA_SEC = TOTSEC - RSVDSEC - NUMFATS * FATSEC - ROOTDIR_SECS
TOTAL_CLUSTERS = DATA_SEC // SEC_PER_CLUS
FATSEC = ((TOTAL_CLUSTERS + 2) * 2 + BPS - 1) // BPS

DATA_START = RSVDSEC + NUMFATS * FATSEC + ROOTDIR_SECS

# 创建空白镜像
img = bytearray(TOTSEC * BPS)

# ===== BPB (扇区 0) =====
# Jump instruction
img[0] = 0xEB   # jmp short
img[1] = 0x3C   # offset
img[2] = 0x90   # nop

# OEM Name
img[3:11] = b'CATOS   '

# BPB
struct.pack_into('<H', img, 11, BPS)          # BytsPerSec
img[13] = SEC_PER_CLUS                         # SecPerClus
struct.pack_into('<H', img, 14, RSVDSEC)       # RsvdSecCnt
img[16] = NUMFATS                              # NumFATs
struct.pack_into('<H', img, 17, ROOTENTS)      # RootEntCnt
struct.pack_into('<H', img, 19, TOTSEC)        # TotSec16
img[21] = MEDIA                                # Media
struct.pack_into('<H', img, 22, FATSEC)        # FATSz16
struct.pack_into('<H', img, 24, 63)            # SecPerTrk
struct.pack_into('<H', img, 26, 16)            # NumHeads
struct.pack_into('<I', img, 28, 0)             # HiddSec
struct.pack_into('<I', img, 32, 0)             # TotSec32

# Extended BPB
img[36] = 0x80                                 # DrvNum
img[37] = 0                                    # Reserved1
img[38] = 0x29                                 # BootSig
struct.pack_into('<I', img, 39, 0x12345678)   # VolID
img[43:54] = b'CATOS HD   '                   # VolLab (11 bytes)
img[54:62] = b'FAT16   '                      # FilSysType (8 bytes)

# Boot signature
img[510] = 0x55
img[511] = 0xAA

# ===== FAT 表 =====
fat = bytearray(FATSEC * BPS)
# FAT[0] = 0xFFF8 (Media | 0xF00), FAT[1] = 0xFFFF
fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF; fat[3] = 0xFF

def fat16_set(cluster, value):
    offset = cluster * 2
    struct.pack_into('<H', fat, offset, value)

def add_file(name83, content, start_cluster):
    size = len(content)
    clusters = (size + BPS * SEC_PER_CLUS - 1) // (BPS * SEC_PER_CLUS)

    # 写入数据区
    data_offset = (DATA_START + (start_cluster - 2) * SEC_PER_CLUS) * BPS
    img[data_offset:data_offset + size] = content

    # 更新 FAT 链
    for i in range(clusters):
        clust = start_cluster + i
        if i + 1 < clusters:
            fat16_set(clust, clust + 1)
        else:
            fat16_set(clust, 0xFFF8)  # EOF

    # 创建根目录项
    rootdir_offset = (RSVDSEC + NUMFATS * FATSEC) * BPS
    entry_idx = 0
    while entry_idx < ROOTENTS:
        off = rootdir_offset + entry_idx * 32
        if img[off] == 0x00:
            break
        entry_idx += 1

    off = rootdir_offset + entry_idx * 32
    entry = bytearray(32)
    entry[0:11] = name83.encode('ascii')
    entry[11] = 0x20  # Archive
    struct.pack_into('<H', entry, 26, start_cluster)
    struct.pack_into('<I', entry, 28, size)
    img[off:off + 32] = entry

    print(f"    {name83}: {size} bytes, {clusters} clusters @ cluster {start_cluster}")
    return clusters

# 添加示例文件
files = [
    ("README  TXT", b"Welcome to CatOS!\r\nThis is a FAT16 hard disk image.\r\nYou can use 'dir' to list files,\r\n'type <name>' to read files,\r\nand 'diskinfo' to see disk info.\r\n"),
    ("HELLO   C  ", b"/* Hello World in C */\r\n#include <stdio.h>\r\n\r\nint main() {\r\n    printf(\"Hello, CatOS!\\n\");\r\n    return 0;\r\n}\r\n"),
    ("NOTES   TXT", b"CatOS Development Notes\r\n=======================\r\n\r\n- Protected mode x86 OS\r\n- FAT12 boot, FAT16 HDD\r\n- IDE/ATA PIO driver\r\n- Ring 1 process model\r\n- Keyboard & VGA text mode\r\n"),
    ("CONFIG  SYS", b"KERNEL=KERNEL BIN\r\nSHELL=SHELL   BIN\r\nDISPLAY=VGA80x25\r\nMEMORY=128M\r\n"),
]

clust = 2
for name83, content in files:
    n = add_file(name83, content, clust)
    clust += n

# 写入 FAT1 和 FAT2
fat1_off = RSVDSEC * BPS
fat2_off = (RSVDSEC + FATSEC) * BPS
img[fat1_off:fat1_off + len(fat)] = fat
img[fat2_off:fat2_off + len(fat)] = fat

# 写入镜像
with open(img_path, 'wb') as f:
    f.write(img)

print()
print(f"  Created: {img_path} ({TOTSEC * BPS} bytes = {TOTSEC * BPS // 1024} KiB)")
print(f"  Format: FAT16, {TOTAL_CLUSTERS} clusters, {SEC_PER_CLUS} sectors/cluster")
print()
print("  Attach as hard disk in VMware/QEMU to use with 'diskinfo', 'dir', 'type' commands.")
PYEOF

	if [ $? -ne 0 ]; then
		echo "ERROR: Python FAT16 HD image builder failed!"
		return 1
	fi
}

#======================================================================
#  构建 FAT32 硬盘镜像 (用于 diskinfo/dir/type 命令测试 FAT32)
#  创建 256MB 标准 MBR + FAT32 分区镜像, 包含示例文件
#  布局: MBR(扇区0) + 间隙(1~2047) + FAT32分区(2048+)
#  支持 FAT32: 32 位 FAT 项, 簇链根目录, FSInfo 扇区
#======================================================================
build_hd32_image() {
	local HD_IMG="catos_hd32.img"

	echo ""
	echo "[*] Building FAT32 hard disk image (standard MBR layout): $HD_IMG"

	python3 - "$HD_IMG" <<'PYEOF'
import sys, struct

img_path = sys.argv[1]

# ===== 磁盘参数 =====
BPS       = 512
TOTSEC    = 524160          # 520*16*63 = 524160 扇区 (~256MB, 与 bochsrc CHS 匹配)
PART_START = 2048           # 分区起始 LBA (1MB 对齐, 标准 MBR 布局)
PART_SIZE = TOTSEC - PART_START   # 分区扇区数

# ===== FAT32 参数 =====
SEC_PER_CLUS = 1            # 256~512MB: 1扇区/簇 (微软规范)
RSVDSEC   = 32              # FAT32 保留扇区数 (规范要求 >=32)
NUMFATS   = 2
ROOTENTS  = 0               # FAT32 根目录无固定条目
MEDIA     = 0xF8
ROOT_CLUS = 2               # 根目录首簇
FSINFO_SEC = 1              # FSInfo 扇区 (相对分区)
BKBOOT_SEC = 6              # 备份引导扇区 (相对分区)

# 计算 FAT 大小 (FAT32: 4 字节/项) — 基于分区大小
def calc_fat32_size(part_sec, rsvd, num_fats, sec_per_clus):
    fat_sz = 1
    for _ in range(20):
        data_sec = part_sec - rsvd - num_fats * fat_sz
        total_clusters = data_sec // sec_per_clus
        new_fat_sz = (total_clusters * 4 + BPS - 1) // BPS
        if new_fat_sz < 1: new_fat_sz = 1
        if new_fat_sz == fat_sz: break
        fat_sz = new_fat_sz
    return fat_sz

FATSEC = calc_fat32_size(PART_SIZE, RSVDSEC, NUMFATS, SEC_PER_CLUS)
DATA_START = RSVDSEC + NUMFATS * FATSEC   # 相对分区
TOTAL_CLUSTERS = (PART_SIZE - RSVDSEC - NUMFATS * FATSEC) // SEC_PER_CLUS

# 创建空白镜像
img = bytearray(TOTSEC * BPS)

# ===== MBR (扇区 0): 分区表 + 引导占位 =====
# 分区表条目1 (偏移 0x1BE, 16字节)
pte_off = 0x1BE
img[pte_off + 0] = 0x80                          # 引导标志 (活动分区)
img[pte_off + 1] = 0xFE                          # 起始 CHS (LBA模式)
img[pte_off + 2] = 0xFF
img[pte_off + 3] = 0xFF
img[pte_off + 4] = 0x0C                          # 分区类型 (FAT32 LBA)
img[pte_off + 5] = 0xFE                          # 结束 CHS
img[pte_off + 6] = 0xFF
img[pte_off + 7] = 0xFF
struct.pack_into('<I', img, pte_off + 8,  PART_START)   # 起始 LBA
struct.pack_into('<I', img, pte_off + 12, PART_SIZE)    # 扇区数
# MBR 引导签名
img[510] = 0x55
img[511] = 0xAA

# ===== VBR (扇区 PART_START) =====
vbr_off = PART_START * BPS
# Jump instruction
img[vbr_off + 0] = 0xEB
img[vbr_off + 1] = 0x58
img[vbr_off + 2] = 0x90

# OEM Name
img[vbr_off + 3:vbr_off + 11] = b'CATOS   '

# BPB
struct.pack_into('<H', img, vbr_off + 11, BPS)          # BytsPerSec
img[vbr_off + 13] = SEC_PER_CLUS                         # SecPerClus
struct.pack_into('<H', img, vbr_off + 14, RSVDSEC)       # RsvdSecCnt
img[vbr_off + 16] = NUMFATS                              # NumFATs
struct.pack_into('<H', img, vbr_off + 17, ROOTENTS)      # RootEntCnt (FAT32=0)
struct.pack_into('<H', img, vbr_off + 19, 0)             # TotSec16 (FAT32 用 TotSec32)
img[vbr_off + 21] = MEDIA                                # Media
struct.pack_into('<H', img, vbr_off + 22, 0)             # FATSz16 (FAT32=0)
struct.pack_into('<H', img, vbr_off + 24, 63)            # SecPerTrk
struct.pack_into('<H', img, vbr_off + 26, 16)            # NumHeads
struct.pack_into('<I', img, vbr_off + 28, PART_START)    # HiddSec (分区起始 LBA)
struct.pack_into('<I', img, vbr_off + 32, PART_SIZE)     # TotSec32 (分区扇区数)

# FAT32 扩展 BPB
struct.pack_into('<I', img, vbr_off + 36, FATSEC)        # FATSz32
struct.pack_into('<H', img, vbr_off + 40, 0)             # ExtFlags
struct.pack_into('<H', img, vbr_off + 42, 0)             # FSVersion
struct.pack_into('<I', img, vbr_off + 44, ROOT_CLUS)     # RootCluster
struct.pack_into('<H', img, vbr_off + 48, FSINFO_SEC)    # FSInfo
struct.pack_into('<H', img, vbr_off + 50, BKBOOT_SEC)    # BkBootSec
# Reserved (52..63) = 0
img[vbr_off + 64] = 0x80                                 # DrvNum
img[vbr_off + 65] = 0                                    # Reserved1
img[vbr_off + 66] = 0x29                                 # BootSig
struct.pack_into('<I', img, vbr_off + 67, 0x12345678)    # VolID
img[vbr_off + 71:vbr_off + 82] = b'CATOS HD32'           # VolLab (11 bytes)
img[vbr_off + 82:vbr_off + 90] = b'FAT32   '             # FilSysType (8 bytes)

# VBR 引导签名
img[vbr_off + 510] = 0x55
img[vbr_off + 511] = 0xAA

# ===== FSInfo 扇区 (分区 + 1) =====
fsi_off = (PART_START + FSINFO_SEC) * BPS
struct.pack_into('<I', img, fsi_off + 0, 0x41615252)     # FSI_LeadSig
struct.pack_into('<I', img, fsi_off + 484, 0x61417272)   # FSI_StrucSig
struct.pack_into('<I', img, fsi_off + 488, TOTAL_CLUSTERS - 1)  # FSI_Free_Count
struct.pack_into('<I', img, fsi_off + 492, 3)            # FSI_Nxt_Free
struct.pack_into('<I', img, fsi_off + 508, 0xAA550000)   # FSI_TrailSig

# ===== 备份 VBR (分区 + 6) + 备份 FSInfo (分区 + 7) =====
bk_off = (PART_START + BKBOOT_SEC) * BPS
img[bk_off:bk_off + 512] = img[vbr_off:vbr_off + 512]
bk_fsi_off = (PART_START + BKBOOT_SEC + 1) * BPS
img[bk_fsi_off:bk_fsi_off + 512] = img[fsi_off:fsi_off + 512]

# ===== FAT 表 (位于分区 + RSVDSEC) =====
fat = bytearray(FATSEC * BPS)
# FAT[0] = 0x0FFFFFF8, FAT[1] = 0x0FFFFFFF, FAT[2] = 0x0FFFFFFF (根目录占簇2)
struct.pack_into('<I', fat, 0,  0x0FFFFFF8)
struct.pack_into('<I', fat, 4,  0x0FFFFFFF)
struct.pack_into('<I', fat, 8,  0x0FFFFFFF)

def fat32_set(cluster, value):
    """设置 FAT32 项, 保留高 4 位"""
    offset = cluster * 4
    old = struct.unpack_from('<I', fat, offset)[0]
    new = (old & 0xF0000000) | (value & 0x0FFFFFFF)
    struct.pack_into('<I', fat, offset, new)

def add_file(name83, content, start_cluster):
    size = len(content)
    clusters = (size + BPS * SEC_PER_CLUS - 1) // (BPS * SEC_PER_CLUS)

    # 写入数据区 (绝对偏移 = (PART_START + DATA_START + 相对簇偏移) * BPS)
    data_offset = (PART_START + DATA_START + (start_cluster - 2) * SEC_PER_CLUS) * BPS
    img[data_offset:data_offset + size] = content

    # 更新 FAT 链
    for i in range(clusters):
        clust = start_cluster + i
        if i + 1 < clusters:
            fat32_set(clust, clust + 1)
        else:
            fat32_set(clust, 0x0FFFFFF8)  # EOF

    # 创建根目录项 (根目录在簇 2)
    rootdir_offset = (PART_START + DATA_START + (ROOT_CLUS - 2) * SEC_PER_CLUS) * BPS
    entry_idx = 0
    while entry_idx < (BPS * SEC_PER_CLUS) // 32:
        off = rootdir_offset + entry_idx * 32
        if img[off] == 0x00:
            break
        entry_idx += 1

    off = rootdir_offset + entry_idx * 32
    entry = bytearray(32)
    entry[0:11] = name83.encode('ascii')
    entry[11] = 0x20  # Archive
    struct.pack_into('<H', entry, 20, (start_cluster >> 16) & 0xFFFF)  # 首簇高16位
    struct.pack_into('<H', entry, 26, start_cluster & 0xFFFF)          # 首簇低16位
    struct.pack_into('<I', entry, 28, size)
    img[off:off + 32] = entry

    print(f"    {name83}: {size} bytes, {clusters} clusters @ cluster {start_cluster}")
    return clusters

# 添加示例文件
files = [
    ("README  TXT", b"Welcome to CatOS FAT32!\r\nThis is a 256MB FAT32 hard disk image (standard MBR layout).\r\nYou can use 'dir' to list files,\r\n'type <name>' to read files,\r\n'format y' to reformat,\r\nand 'touch <name>' to create files.\r\n\r\nFAT32 features:\r\n- Standard MBR + partition table\r\n- 32-bit FAT entries (28-bit effective)\r\n- Cluster chain root directory\r\n- FSInfo sector for free count hints\r\n"),
    ("HELLO   C  ", b"/* Hello World in C */\r\n#include <stdio.h>\r\n\r\nint main() {\r\n    printf(\"Hello, CatOS FAT32!\\n\");\r\n    return 0;\r\n}\r\n"),
    ("NOTES   TXT", b"CatOS FAT32 Development Notes\r\n=============================\r\n\r\n- Standard MBR at sector 0 with partition table\r\n- Partition starts at LBA 2048 (1MB aligned)\r\n- VBR at partition start with HiddSec=PART_START\r\n- FAT32 BPB uses FATSz32 (offset 36) since FATSz16=0\r\n- Root directory is a cluster chain starting at RootCluster (offset 44)\r\n- FAT entries are 32-bit, only low 28 bits valid (mask 0x0FFFFFFF)\r\n- Backup boot sector at partition+6, backup FSInfo at partition+7\r\n"),
    ("CONFIG  SYS", b"KERNEL=KERNEL BIN\r\nSHELL=SHELL   BIN\r\nDISPLAY=VGA80x25\r\nMEMORY=128M\r\nFS=FAT32\r\n"),
]

clust = 3   # 簇 2 是根目录
for name83, content in files:
    n = add_file(name83, content, clust)
    clust += n

# 写入 FAT1 和 FAT2 (绝对偏移 = (PART_START + RSVDSEC + i*FATSEC) * BPS)
fat1_off = (PART_START + RSVDSEC) * BPS
fat2_off = (PART_START + RSVDSEC + FATSEC) * BPS
img[fat1_off:fat1_off + len(fat)] = fat
img[fat2_off:fat2_off + len(fat)] = fat

# 写入镜像
with open(img_path, 'wb') as f:
    f.write(img)

print()
print(f"  Created: {img_path} ({TOTSEC * BPS} bytes = {TOTSEC * BPS // 1024 // 1024} MiB)")
print(f"  Layout: MBR @ sector 0, partition @ {PART_START} ({PART_SIZE} sectors)")
print(f"  Format: FAT32, {TOTAL_CLUSTERS} clusters, {SEC_PER_CLUS} sectors/cluster")
print(f"  FAT size: {FATSEC} sectors per FAT")
print(f"  Root directory: cluster {ROOT_CLUS}")
print(f"  FSInfo: partition+{FSINFO_SEC}, backup boot: partition+{BKBOOT_SEC}")
print()
print("  Attach as hard disk in VMware/QEMU/Bochs to use with 'diskinfo', 'dir', 'type' commands.")
PYEOF

	if [ $? -ne 0 ]; then
		echo "ERROR: Python FAT32 HD image builder failed!"
		return 1
	fi
}

do_build() {
	print_banner
	check_tools

	# ===== 第一遍编译 (用默认 KERNEL_FILE_SIZE) =====
	echo "[*] Pass 1: Compile with default KERNEL_FILE_SIZE..."
	ALL_OBJS=()
	compile_boot
	compile_kernel_asm
	compile_kernel_c
	link_kernel

	# ===== 计算 kernel.bin 实际大小 =====
	local KERNEL_SIZE=$(wc -c < "$KERNEL_BIN")
	echo "[*] Kernel size: $KERNEL_SIZE bytes"

	# ===== 第二遍编译 (用实际 KERNEL_FILE_SIZE) =====
	if [ "$KERNEL_SIZE" -ne 40960 ]; then
		echo ""
		echo "[*] Pass 2: Recompile with KERNEL_FILE_SIZE=$KERNEL_SIZE..."
		ALL_OBJS=()
		compile_boot
		compile_kernel_asm
		compile_kernel_c "$KERNEL_SIZE"
		link_kernel
		echo "[*] Final kernel size: $(wc -c < "$KERNEL_BIN") bytes"
	fi

	echo ""
	echo "========================================="
	echo "  Build SUCCESS!"
	echo "  Boot:   $BOOT_BIN"
	echo "  Loader: $LOADER_BIN"
	echo "  Kernel: $KERNEL_BIN ($(wc -c < "$KERNEL_BIN") bytes)"
	echo "========================================="
	echo ""
}

#======================================================================
#  构建 ISO 光盘映像 (El Torito 可启动 ISO)
#  使用 hd32 硬盘映像 + 硬盘 emulation 模式, 支持大镜像容量
#  BIOS 把 catos_hd32.img 当作硬盘模拟, LBA 0 = MBR, LBA 1 = VBR
#  mbr.bin 用 LBA 方式 (INT 13h AH=42h) 读取, 与硬盘启动完全一致
#  可突破 1.44MB 软盘限制, 适用于虚拟机和真实光盘刻录
#======================================================================
build_iso() {
	do_build

	echo ""
	echo "[*] Building ISO image: catos.iso"

	# 先确保 FAT32 硬盘映像存在 (用于 ISO 启动)
	if [ ! -f "catos_hd32.img" ]; then
		echo "[*] FAT32 hard disk image not found, building it first..."
		build_hd32_image
	fi

	# 检测 ISO 创建工具
	local ISO_TOOL=""
	for tool in mkisofs genisoimage xorriso; do
		if command -v "$tool" >/dev/null 2>&1; then
			ISO_TOOL="$tool"
			break
		fi
	done

	if [ -z "$ISO_TOOL" ]; then
		echo "ERROR: No ISO creation tool found (mkisofs/genisoimage/xorriso)"
		echo "  Install one of:"
		echo "    Debian/Ubuntu: sudo apt install genisoimage"
		echo "    Arch: sudo pacman -S cdrtools"
		echo "    MSYS2/MinGW: pacman -S cdrtools"
		echo "  Or use 'image' target to generate floppy image instead."
		return 1
	fi
	echo "    ISO tool: $ISO_TOOL"

	local ISO_FILE="catos.iso"
	local HD_IMG="catos_hd32.img"

	# 创建临时 ISO 目录结构
	local ISO_DIR=".iso_tmp_$$"
	mkdir -p "$ISO_DIR/boot"

	# 复制启动映像和文件到 ISO 目录
	cp "$HD_IMG"    "$ISO_DIR/boot/catos_hd32.img"
	cp "$MBR_BIN"  "$ISO_DIR/boot/mbr.bin"
	cp "$HDBOOT_BIN" "$ISO_DIR/boot/hdboot.bin"
	cp "$LOADER_BIN" "$ISO_DIR/boot/loader.bin"
	cp "$KERNEL_BIN" "$ISO_DIR/boot/kernel.bin"

	# 生成 ISO (El Torito 硬盘 emulation 模式, 使用 hd32 映像)
	# -hard-disk-boot: BIOS 把 catos_hd32.img 当作硬盘模拟
	#   LBA 0 = MBR, LBA 1 = VBR (与真实硬盘一致)
	#   不加 -no-emul-boot: BIOS 提供 INT 13h LBA 接口读取映像内容
	# mbr.bin 扫描分区表, 用 LBA 方式加载 VBR/loader/kernel
	# 可突破 1.44MB 软盘限制, 适用于虚拟机和真实光盘刻录
	case "$ISO_TOOL" in
		mkisofs|genisoimage)
			"$ISO_TOOL" -quiet \
				-b boot/catos_hd32.img \
				-c boot/boot.cat \
				-hard-disk-boot \
				-o "$ISO_FILE" \
				-V "CATOS" \
				-A "CatOS Bootable ISO" \
				"$ISO_DIR"
			;;
		xorriso)
			"$ISO_TOOL" -as mkisofs \
				-b boot/catos_hd32.img \
				-c boot/boot.cat \
				-hard-disk-boot \
				-V "CATOS" \
				-o "$ISO_FILE" \
				"$ISO_DIR"
			;;
	esac

	if [ $? -ne 0 ]; then
		echo "ERROR: ISO creation failed!"
		rm -rf "$ISO_DIR"
		return 1
	fi

	# 清理临时目录
	rm -rf "$ISO_DIR"

	local ISO_SIZE=$(wc -c < "$ISO_FILE")
	echo ""
	echo "========================================="
	echo "  ISO created: $ISO_FILE"
	echo "  Size: $ISO_SIZE bytes ($(($ISO_SIZE / 1024)) KiB)"
	echo "========================================="
	echo ""
	echo "  Run with:"
	echo "    qemu-system-i386 -cdrom $ISO_FILE -boot d"
	echo "    bochs -f bochsrc.bxrc  (set boot: cdrom)"
	echo ""
}

case "${1:-all}" in
	all)     do_build ;;
	clean)   print_banner; do_clean ;;
	rebuild) do_clean; do_build ;;
	image)   build_image ;;
	hdimg)   build_hd_image ;;
	hdimg32) build_hd32_image ;;
	iso)     build_iso ;;
	*)       echo "Usage: $0 {all|clean|rebuild|image|hdimg|hdimg32|iso}"
	         echo "  all     - Compile only (default)"
	         echo "  clean   - Remove build artifacts"
	         echo "  rebuild - Clean then compile"
	         echo "  image   - Full compile + generate catos.img (FAT12 floppy)"
	         echo "  hdimg   - Generate catos_hd.img (FAT16 hard disk for file browsing)"
	         echo "  hdimg32 - Generate catos_hd32.img (FAT32 hard disk, 256MB)"
	         echo "  iso     - Generate catos.iso (El Torito bootable ISO)"
	         exit 1 ;;
esac
