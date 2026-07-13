import struct
with open("hello.ce", "rb") as f:
    data = f.read()
    print(f"File size: {len(data)} bytes")
    idx = data.find(b"Hello, CatOS!")
    if idx >= 0:
        print(f"Found 'Hello, CatOS!' at file offset: {idx}")
        print(f"Relative to body (offset 64): {idx - 64}")
    else:
        print("String NOT found in file!")
        # 打印前256字节看看内容
        print("\nFirst 256 bytes (hex):")
        for i in range(0, min(256, len(data)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in data[i:i+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
            print(f"{i:04x}: {hex_str}  {ascii_str}")
input("press enter to continue...")