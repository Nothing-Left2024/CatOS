# CatOS User Program SDK - Makefile
#
# 用法:
#   make            构建 hello.cate
#   make clean      清理
#   make all        同 make

# ===== 工具链检测 =====
# 优先使用 MinGW 32位交叉编译器, 回退到系统 gcc
CC := $(shell which i686-w64-mingw32-gcc 2>/dev/null || which gcc 2>/dev/null)
NASM := $(shell which nasm 2>/dev/null || which nasm.exe 2>/dev/null)
OBJCOPY := $(shell which objcopy 2>/dev/null || which objcopy.exe 2>/dev/null)
PYTHON := $(shell which python3 2>/dev/null || which python 2>/dev/null)

# ===== 编译选项 =====
CFLAGS := -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-pie \
          -fno-asynchronous-unwind-tables -fno-merge-constants -Wall \
          -mno-sse -mno-sse2 -mno-mmx -march=i386
NASMFLAGS := -f win32
LDFLAGS := -m32 -nostdlib -Wl,-T,user.ld -Wl,--image-base=0 -Wl,--section-alignment=1 -Wl,--file-alignment=1 -Wl,-N

# ===== 目标 =====
TARGET := hello.ce
ELF := hello.elf
CRT0 := crt0.o
HELLO_O := hello.o
PROG_NAME := hello

.PHONY: all clean

all: $(TARGET)

# 编译 C 源文件
$(HELLO_O): hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 汇编 crt0
$(CRT0): crt0.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

# 链接为 ELF
$(ELF): $(CRT0) $(HELLO_O) user.ld
	$(CC) $(LDFLAGS) -o $@ $(CRT0) $(HELLO_O)

# 打包为 CE v1 格式
$(TARGET): $(ELF)
	$(PYTHON) mkcate.py $(ELF) $(TARGET) $(PROG_NAME)

clean:
	rm -f $(CRT0) $(HELLO_O) $(ELF) $(TARGET) $(TARGET).bin
