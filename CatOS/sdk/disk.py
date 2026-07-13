#!/usr/bin/env python3
"""
disk_image_put_gui.py - 将文件放入磁盘映像（挂载、复制、卸载，同名覆盖）
用法（命令行）： python disk_image_put_gui.py <映像文件> <要放入的文件>
双击运行： 弹出对话框选择映像文件和目标文件（自动记住上次路径）
兼容 Python 3.6+
"""

import sys
import os
import subprocess
import shutil
import tempfile
import platform
import time
import json

# 尝试导入 tkinter（用于 GUI 文件选择）
try:
    import tkinter as tk
    from tkinter import filedialog
    TKINTER_AVAILABLE = True
except ImportError:
    TKINTER_AVAILABLE = False

# 配置文件路径（保存上次打开的文件夹）
CONFIG_FILE = os.path.expanduser("~/.disk_img_put_config.json")


def load_config():
    """加载配置文件，返回 dict 或空 dict"""
    try:
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_config(config):
    """保存配置到文件"""
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2)
    except Exception:
        pass  # 忽略保存失败


def get_windows_drive_letter(image_path):
    """
    在 Windows 上挂载映像并返回分配的盘符（例如 'E'）
    """
    before = set()
    result = subprocess.run(
        ["powershell", "-Command", "Get-Volume | Where-Object {$_.DriveLetter} | Select-Object -ExpandProperty DriveLetter"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, check=True
    )
    for line in result.stdout.strip().splitlines():
        if line.strip():
            before.add(line.strip().upper())

    mount_cmd = f"Mount-DiskImage -ImagePath '{image_path}' -PassThru"
    subprocess.run(["powershell", "-Command", mount_cmd], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(2)

    after = set()
    result = subprocess.run(
        ["powershell", "-Command", "Get-Volume | Where-Object {$_.DriveLetter} | Select-Object -ExpandProperty DriveLetter"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, check=True
    )
    for line in result.stdout.strip().splitlines():
        if line.strip():
            after.add(line.strip().upper())

    new_letters = after - before
    if not new_letters:
        raise RuntimeError("未发现新增盘符，挂载可能失败。")
    drive = new_letters.pop()
    return drive


def gui_select_files():
    """弹出两个文件选择对话框，自动使用上次打开的目录，返回 (映像路径, 文件路径) 或 (None, None)"""
    config = load_config()
    image_dir = config.get("image_dir", os.path.expanduser("~"))
    file_dir = config.get("file_dir", os.path.expanduser("~"))

    root = tk.Tk()
    root.withdraw()
    root.attributes('-topmost', True)

    # 选择映像文件
    image_path = filedialog.askopenfilename(
        title="选择磁盘映像文件",
        initialdir=image_dir,
        filetypes=[("所有文件", "*.*"), ("磁盘映像", "*.img *.iso *.vhd *.vhd *.dmg *.bin")]
    )
    if not image_path:
        root.destroy()
        return None, None

    # 更新映像目录为当前选择的目录
    config["image_dir"] = os.path.dirname(image_path)

    # 选择要放入的文件
    file_path = filedialog.askopenfilename(
        title="选择要放入映像的文件",
        initialdir=file_dir,
        filetypes=[("所有文件", "*.*")]
    )
    root.destroy()
    if not file_path:
        # 用户取消，但已选择的映像目录仍可保存（便于下次）
        save_config(config)
        return None, None

    # 更新文件目录
    config["file_dir"] = os.path.dirname(file_path)
    save_config(config)

    return image_path, file_path


def main():
    # ---------- 获取文件路径（命令行或 GUI） ----------
    if len(sys.argv) == 3:
        image_path = os.path.abspath(sys.argv[1])
        file_path = os.path.abspath(sys.argv[2])
    else:
        if not TKINTER_AVAILABLE:
            print("tkinter 不可用，请手动输入路径。")
            image_path = input("请输入磁盘映像文件路径: ").strip()
            if not image_path:
                print("未输入路径，退出。")
                sys.exit(1)
            file_path = input("请输入要放入的文件路径: ").strip()
            if not file_path:
                print("未输入路径，退出。")
                sys.exit(1)
            image_path = os.path.abspath(image_path)
            file_path = os.path.abspath(file_path)
        else:
            print("请在弹出的对话框中选择文件...")
            image_path, file_path = gui_select_files()
            if image_path is None or file_path is None:
                print("用户取消选择，退出。")
                sys.exit(0)

    # ---------- 检查文件存在 ----------
    if not os.path.isfile(image_path):
        print(f"错误: 映像文件 '{image_path}' 不存在或不是文件。")
        sys.exit(1)
    if not os.path.isfile(file_path):
        print(f"错误: 目标文件 '{file_path}' 不存在或不是文件。")
        sys.exit(1)

    system = platform.system()
    mount_point = None
    drive_letter = None

    try:
        # ---------- 挂载 ----------
        if system == "Windows":
            drive_letter = get_windows_drive_letter(image_path)
            mount_point = f"{drive_letter}:\\"
            print(f"映像已挂载为盘符 {mount_point}")
        elif system == "Linux":
            mount_point = tempfile.mkdtemp(prefix="mount_")
            subprocess.run(["sudo", "mount", "-o", "loop", image_path, mount_point], check=True)
            print(f"映像已挂载到 {mount_point}")
        elif system == "Darwin":  # macOS
            mount_point = tempfile.mkdtemp(prefix="mount_")
            subprocess.run(["hdiutil", "attach", "-mountpoint", mount_point, image_path], check=True)
            print(f"映像已挂载到 {mount_point}")
        else:
            print(f"不支持的操作系统: {system}")
            sys.exit(1)

        # ---------- 复制文件（覆盖） ----------
        dest = os.path.join(mount_point, os.path.basename(file_path))
        shutil.copy2(file_path, dest)
        print(f"已复制 '{file_path}' 到 '{dest}'（已覆盖同名文件）")

    except subprocess.CalledProcessError as e:
        print(f"执行命令失败: {e}")
        if e.stdout:
            print(f"标准输出: {e.stdout}")
        if e.stderr:
            print(f"错误输出: {e.stderr}")
        sys.exit(1)
    except Exception as e:
        print(f"发生错误: {e}")
        sys.exit(1)

    finally:
        # ---------- 卸载 ----------
        if system == "Windows" and drive_letter:
            try:
                subprocess.run(
                    ["powershell", "-Command", f"Dismount-DiskImage -ImagePath '{image_path}'"],
                    check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
                )
                print("映像已卸载。")
            except Exception as e:
                print(f"警告: 卸载失败（可能需要手动卸载）: {e}")
        elif system == "Linux" and mount_point:
            try:
                subprocess.run(["sudo", "umount", mount_point], check=True)
                os.rmdir(mount_point)
                print("映像已卸载。")
            except Exception as e:
                print(f"警告: 卸载失败: {e}")
        elif system == "Darwin" and mount_point:
            try:
                subprocess.run(["hdiutil", "detach", mount_point], check=True)
                os.rmdir(mount_point)
                print("映像已卸载。")
            except Exception as e:
                print(f"警告: 卸载失败: {e}")


if __name__ == "__main__":
    main()
    input("press enter to continue...")