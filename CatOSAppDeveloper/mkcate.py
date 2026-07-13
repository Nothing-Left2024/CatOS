#!/usr/bin/env python3
"""
mkce.py - CatOS CE 可执行文件打包工具 (v1)

用法: python3 mkce.py <input.elf> <output.ce> [program_name]

功能:
  1. 用 `size` 命令获取段大小 (.text+.rodata, .data, .bss) — 兼容 ELF 和 PE
  2. 用 `nm` 命令读取 _start 符号地址作为 entry point
  3. 生成 64 字节 CE v1 头
  4. 用 objcopy 提取 flat binary (text+data)
  5. 输出: header(64) + flat_binary

CE v1 头格式 (64 字节, 小端):
  偏移  长度  字段          说明
  0     4    magic         0x00014543 ('CE\x01\x00')
  4     2    version       1
  6     2    flags         标志位 (bit0=32位)
  8     4    entry         入口偏移
  12    4    text_size     .text + .rodata 大小
  16    4    data_size     .data 大小
  20    4    bss_size      .bss 大小
  24    4    stack_size    请求栈大小
  28    4    total_size    text+data+bss 总大小 (校验用)
  32    28   name          程序名称 (ASCII, \0 结尾)
  60    4    header_crc    头部 CRC32 (预留, 当前为0)
"""
import sys
import struct
import subprocess
import os

CE_MAGIC = 0x00014543    # 'CE\x01\x00' 小端
CE_VERSION = 1
CE_HEADER_SIZE = 64
DEFAULT_STACK_SIZE = 4096


def run_cmd(cmd):
    """运行命令, 返回 (stdout, stderr)。尝试 .exe 后缀"""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.stdout, r.stderr
    except FileNotFoundError:
        try:
            r = subprocess.run([cmd[0] + '.exe'] + cmd[1:], capture_output=True, text=True)
            return r.stdout, r.stderr
        except FileNotFoundError:
            return '', f'{cmd[0]} not found'


def get_section_sizes(obj_path):
    """用 `size` 命令获取 text/data/bss 大小。"""
    out, err = run_cmd(['size', obj_path])
    if not out:
        print(f"Error: `size` command failed: {err}", file=sys.stderr)
        sys.exit(1)

    lines = out.strip().split('\n')
    if len(lines) < 2:
        print(f"Error: unexpected `size` output:\n{out}", file=sys.stderr)
        sys.exit(1)

    parts = lines[1].split()
    if len(parts) < 4:
        print(f"Error: cannot parse `size` output:\n{out}", file=sys.stderr)
        sys.exit(1)

    text_size = int(parts[0])
    data_size = int(parts[1])
    bss_size = int(parts[2])
    return text_size, data_size, bss_size


def get_entry_point(obj_path):
    """获取入口点地址
    使用 objdump -x 精确查找 _start 符号（不包括 _start.hang 等衍生符号）
    """
    # 方法1: 用 objdump -x 精确匹配 _start
    out, err = run_cmd(['objdump', '-x', obj_path])
    if out:
        for line in out.split('\n'):
            line = line.strip()
            # 精确匹配以 _start 结尾的符号（不包括 _start.hang 等）
            if line.endswith('_start') and not line.startswith('#'):
                parts = line.split()
                for p in parts:
                    try:
                        addr = int(p, 16)
                        if addr > 0:
                            return addr
                    except ValueError:
                        pass

    # 方法2: 用 nm 查找
    out2, err2 = run_cmd(['nm', obj_path])
    if out2:
        for line in out2.split('\n'):
            parts = line.strip().split()
            if len(parts) >= 2:
                sym = parts[-1]
                if sym == '_start':
                    try:
                        return int(parts[0], 16)
                    except ValueError:
                        pass

    # 都失败了, 默认 entry=0
    print("Warning: _start symbol not found, using entry=0", file=sys.stderr)
    return 0


def extract_flat_binary(obj_path, bin_path):
    """用 objcopy 提取 flat binary (含 .text + .rodata/.rdata + .data, 不含 .bss)"""
    for oc in ['objcopy', 'objcopy.exe']:
        try:
            # 尝试包含 .rodata (ELF) 和 .rdata (PE/MinGW)
            r = subprocess.run(
                [oc, '-O', 'binary', '-j', '.text', '-j', '.rodata', '-j', '.rdata',
                 '-j', '.data', obj_path, bin_path],
                capture_output=True, text=True
            )
            if r.returncode == 0:
                return
            # 回退: 只提取 .text + .data
            r2 = subprocess.run(
                [oc, '-O', 'binary', '-j', '.text', '-j', '.data', obj_path, bin_path],
                capture_output=True, text=True
            )
            if r2.returncode == 0:
                return
            # 最后回退: 全部段
            r3 = subprocess.run(
                [oc, '-O', 'binary', obj_path, bin_path],
                capture_output=True, text=True
            )
            if r3.returncode == 0:
                return
        except FileNotFoundError:
            continue

    print("Error: objcopy failed or not found", file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.elf> <output.ce> [program_name]", file=sys.stderr)
        sys.exit(1)

    obj_path = sys.argv[1]
    out_path = sys.argv[2]
    prog_name = sys.argv[3] if len(sys.argv) > 3 else ''

    # 1. 获取段大小 (兼容 ELF/PE)
    text_size, data_size, bss_size = get_section_sizes(obj_path)

    # 2. 获取入口地址
    entry = get_entry_point(obj_path)

    # 3. 计算总大小
    total_size = text_size + data_size + bss_size

    print(f"  entry      = 0x{entry:08x}")
    print(f"  text_size  = {text_size} bytes")
    print(f"  data_size  = {data_size} bytes")
    print(f"  bss_size   = {bss_size} bytes")
    print(f"  total_size = {total_size} bytes")
    if prog_name:
        print(f"  name       = {prog_name}")

    # 4. 提取 flat binary
    bin_path = out_path + '.bin'
    extract_flat_binary(obj_path, bin_path)

    with open(bin_path, 'rb') as f:
        body = f.read()

    # 5. 校正: body 大小应 = text + data
    if len(body) != text_size + data_size:
        print(f"Warning: body size {len(body)} != text+data {text_size + data_size}, adjusting")
        if len(body) >= data_size:
            text_size = len(body) - data_size
        else:
            text_size = 0
            data_size = len(body)
        total_size = text_size + data_size + bss_size

    # 6. 准备名称字段 (最多27字符, \0结尾)
    name_bytes = prog_name.encode('ascii', errors='replace')[:27]
    name_field = name_bytes + b'\x00' * (28 - len(name_bytes))

    # 7. 生成 CE v1 头 (64 字节, 小端)
    # 格式: magic(4) + version(2) + flags(2) + entry(4) + text_size(4) + data_size(4) +
    #       bss_size(4) + stack_size(4) + total_size(4) + name(28) + header_crc(4) = 64 字节
    header = struct.pack('<IHHIIIIII28sI',
        CE_MAGIC,           # 4 bytes: magic
        CE_VERSION,         # 2 bytes: version
        0x0001,             # 2 bytes: flags (bit0 = 32-bit)
        entry,              # 4 bytes: entry
        text_size,          # 4 bytes: text_size
        data_size,          # 4 bytes: data_size
        bss_size,           # 4 bytes: bss_size
        DEFAULT_STACK_SIZE, # 4 bytes: stack_size
        total_size,         # 4 bytes: total_size
        name_field,         # 28 bytes: name
        0                   # 4 bytes: header_crc (预留)
    )

    assert len(header) == CE_HEADER_SIZE, f"Header size {len(header)} != {CE_HEADER_SIZE}"

    # 8. 写出: header + body
    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(body)

    print(f"  Output: {out_path} ({len(header) + len(body)} bytes)")
    print(f"  Format: CE v1 ({CE_HEADER_SIZE}B header + {len(body)}B body)")

    # 清理临时文件
    try:
        os.remove(bin_path)
    except:
        pass


if __name__ == '__main__':
    main()
