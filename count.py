import os

def count_text_lines_detailed(extensions=None):
    """
    统计当前目录及其子目录下所有指定扩展名（文本类型）文件的行数。
    参数：
        extensions: 列表或元组，文件扩展名（如 ['.txt', '.php']），
                    默认为常见文本格式列表（.txt .php .py .js .html .css .md .json .xml .csv .log）
    返回总行数。
    """
    if extensions is None:
        # 默认常见文本文件扩展名
        extensions = ('.php', '.js', '.html', '.htm', '.css',
                      '.md', '.json', '.xml', '.csv', '.log', '.ini', '.conf', '.c', '.asm')
    # 统一转为小写并确保以点开头
    extensions = tuple(ext.lower() if ext.startswith('.') else f'.{ext.lower()}' for ext in extensions)

    file_info = []
    total_lines = 0

    for root, dirs, files in os.walk("."):
        for file in files:
            # 检查文件扩展名
            ext = os.path.splitext(file)[1].lower()
            if ext in extensions:
                file_path = os.path.join(root, file)
                rel_path = os.path.relpath(file_path, start=".")
                rel_path = rel_path.replace("\\", "/")  # 统一正斜杠
                try:
                    with open(file_path, "rb") as f:
                        lines = sum(1 for _ in f)  # 二进制模式统计行数
                    file_info.append((rel_path, lines))
                    total_lines += lines
                except (IOError, OSError) as e:
                    print(f"警告：无法读取文件 {rel_path} - {e}")

    # 输出类似 dir 的结果
    if file_info:
        max_len = max(len(path) for path, _ in file_info)
        print(f"{'文件路径':<{max_len}}  行数")
        print("-" * (max_len + 8))
        for path, lines in file_info:
            print(f"{path:<{max_len}}  {lines:>6}")
        print("-" * (max_len + 8))
        print(f"{'总计':<{max_len}}  {total_lines:>6} 行")
    else:
        print("未找到符合条件的文本文件。")

    return total_lines

# 使用示例
if __name__ == "__main__":
    # 统计默认文本文件
    count_text_lines_detailed()

    # 示例：只统计 PHP 和 Python 文件
    # count_text_lines_detailed(extensions=['.php', '.py'])
    input()