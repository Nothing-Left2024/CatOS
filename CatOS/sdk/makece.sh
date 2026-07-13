#!/bin/bash
######################################################################
#  makece.sh - CatOS CE 编译工具
#
#  用法:
#    ./makece.sh <source> [output_name]
#
#  示例:
#    ./makece.sh hello.c           -> hello.ce
#    ./makece.sh hello.c myprog    -> myprog.ce
#    ./makece.sh prog.asm          -> prog.ce
#
#  支持的源文件类型:
#    .c    - C 源文件 (用 GCC 编译)
#    .asm  - NASM 汇编源文件 (用 NASM 编译, -f win32)
#
#  功能:
#    1. 自动检测工具链 (nasm / gcc / python)
#    2. 确保 crt0.o 存在 (缺失或过期时自动构建)
#    3. 编译源文件 -> .o
#    4. 链接 crt0.o + 源.o -> .elf (使用 user.ld)
#    5. 打包为 CE v1 格式 -> .ce (使用 mkcate.py)
#    6. 清理中间文件
######################################################################

set -e

# ===== 切换到脚本所在目录 (sdk/) =====
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ===== 输出函数 (避免 stderr 重定向在某些 shell 环境中出错) =====
info()  { echo "[*] $*"; }
ok()    { echo "[+] $*"; }
warn()  { echo "[!] $*"; }
die()   { echo "[x] $*"; exit 1; }

# ===== 参数检查 =====
if [ $# -lt 1 ]; then
    cat <<'USAGE'
用法: makece.sh <source> [output_name]

示例:
  makece.sh hello.c           编译 hello.c 为 hello.ce
  makece.sh hello.c myprog    编译 hello.c 为 myprog.ce
  makece.sh prog.asm          编译 prog.asm 为 prog.ce
USAGE
    exit 1
fi

SRC="$1"
OUTNAME="${2:-}"

# ===== 解析源文件 =====
if [ ! -f "$SRC" ]; then
    die "源文件不存在: $SRC"
fi

SRC_BASE="$(basename "$SRC")"
SRC_NAME="${SRC_BASE%.*}"
SRC_EXT="${SRC_BASE##*.}"
SRC_EXT_LOWER="$(echo "$SRC_EXT" | tr 'A-Z' 'a-z')"

# 输出名默认 = 源文件名 (不含扩展名)
if [ -z "$OUTNAME" ]; then
    OUTNAME="$SRC_NAME"
fi

ELF="${OUTNAME}.elf"
OBJ="${OUTNAME}.o"
CE="${OUTNAME}.ce"
CRT0_OBJ="crt0.o"

# ===== 工具链检测 =====
info "检测工具链..."

# NASM
NASM=""
for c in "/c/Program Files/NASM/nasm.exe" "/c/NASM/nasm.exe" \
         "/mingw32/bin/nasm.exe" "/mingw64/bin/nasm.exe" \
         nasm nasm.exe; do
    if command -v "$c" >/dev/null 2>&1; then
        NASM="$(command -v "$c")"
        break
    fi
    if [ -f "$c" ]; then
        NASM="$c"; break
    fi
done
[ -n "$NASM" ] || die "未找到 nasm, 请安装或加入 PATH"
ok "NASM = $NASM"

# GCC (优先 32 位交叉编译器)
CC=""
for c in i686-w64-mingw32-gcc x86_64-w64-mingw32-gcc gcc; do
    if command -v "$c" >/dev/null 2>&1; then
        # 测试能否 -m32 编译
        _tmp_src=".makece_test_$$.c"
        _tmp_obj=".makece_test_$$.o"
        echo 'int x;' > "$_tmp_src"
        if "$c" -c "$_tmp_src" -o "$_tmp_obj" -m32 2>/dev/null; then
            CC="$c"
            rm -f "$_tmp_src" "$_tmp_obj" 2>/dev/null
            break
        fi
        rm -f "$_tmp_src" "$_tmp_obj" 2>/dev/null
    fi
done
[ -n "$CC" ] || die "未找到支持 -m32 的 gcc"
ok "CC   = $CC"

# OBJCOPY (与 gcc 同目录)
OBJCOPY=""
_oc_dir="$(dirname "$CC")"
for oc in "$_oc_dir/objcopy.exe" "$_oc_dir/objcopy" objcopy objcopy.exe; do
    if command -v "$oc" >/dev/null 2>&1; then
        OBJCOPY="$(command -v "$oc")"
        break
    fi
    if [ -f "$oc" ]; then OBJCOPY="$oc"; break; fi
done
[ -n "$OBJCOPY" ] || die "未找到 objcopy"
ok "OBJCOPY = $OBJCOPY"

# Python
PYTHON=""
for c in python3 python python.exe; do
    if command -v "$c" >/dev/null 2>&1; then
        PYTHON="$(command -v "$c")"
        break
    fi
done
[ -n "$PYTHON" ] || die "未找到 python"
ok "PYTHON = $PYTHON"

# ===== 编译选项 =====
CFLAGS="-m32 -ffreestanding -fno-pic -fno-stack-protector -fno-pie \
        -fno-asynchronous-unwind-tables -fno-merge-constants -Wall \
        -mno-sse -mno-sse2 -mno-mmx -march=i386 -c"
NASMFLAGS="-f win32"
LDFLAGS="-m32 -nostdlib -Wl,-T,user.ld -Wl,--image-base=0 \
         -Wl,--section-alignment=1 -Wl,--file-alignment=1 -Wl,-N"

# ===== 步骤1: 确保 crt0.o 存在 =====
info "检查 C 运行时 (crt0.o)..."
if [ ! -f "$CRT0_OBJ" ] || [ crt0.asm -nt "$CRT0_OBJ" ]; then
    "$NASM" $NASMFLAGS -o "$CRT0_OBJ" crt0.asm
    ok "已构建 $CRT0_OBJ"
else
    ok "$CRT0_OBJ 已是最新"
fi

# ===== 步骤2: 编译源文件 =====
info "编译 $SRC_BASE ..."
case "$SRC_EXT_LOWER" in
    c)
        "$CC" $CFLAGS -o "$OBJ" "$SRC"
        ;;
    asm|s)
        # NASM 汇编源: 使用 -f win32, 输出 .o
        "$NASM" $NASMFLAGS -o "$OBJ" "$SRC"
        ;;
    *)
        die "不支持的文件类型: .$SRC_EXT (仅支持 .c / .asm)"
        ;;
esac
ok "编译完成: $OBJ"

# ===== 步骤3: 链接 =====
info "链接 -> $ELF ..."
"$CC" $LDFLAGS -o "$ELF" "$CRT0_OBJ" "$OBJ" 2>&1 | grep -v "section below image base" || true
[ -f "$ELF" ] || die "链接失败"
ok "链接完成: $ELF"

# ===== 步骤4: 打包为 CE =====
info "打包 CE v1 格式 -> $CE ..."
"$PYTHON" mkcate.py "$ELF" "$CE" "$OUTNAME"

# ===== 步骤5: 验证输出 =====
if [ -f "$CE" ]; then
    CE_SIZE=$(wc -c < "$CE")
    echo ""
    ok "构建成功!"
    echo "    输出: $CE ($CE_SIZE bytes)"
    echo ""
else
    die "打包失败: $CE 未生成"
fi

# ===== 清理中间文件 =====
rm -f "$OBJ" "$ELF" "$CE.bin" 2>/dev/null
info "已清理中间文件"
