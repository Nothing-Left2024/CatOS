/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               fat.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS FAT 文件系统 — 结构与函数声明
  支持 FAT12 / FAT16 / FAT32 (读+写)
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _CATOS_FAT_H_
#define _CATOS_FAT_H_

#include "type.h"

/* 目录项属性 */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F   /* 长文件名标记 */

/* FAT 类型判定阈值 (依据微软规范) */
#define FAT12_MAX_CLUSTERS  4084
#define FAT16_MAX_CLUSTERS  65524

/* FAT32 EOF 与空闲标记 (28 位有效) */
#define FAT32_EOF_MIN       0x0FFFFFF8
#define FAT32_EOF           0x0FFFFFFF
#define FAT32_MASK          0x0FFFFFFF

/* 标准 MBR 分区布局: 分区起始 LBA (1MB 对齐) */
#define PART_START          2048

/* FAT12/16 EOF 标记 */
#define FAT12_EOF_MIN       0x0FF8
#define FAT12_EOF           0x0FFF
#define FAT16_EOF_MIN       0xFFF8
#define FAT16_EOF           0xFFFF

/* FAT 目录项结构 (32 字节) */
typedef struct {
	char	name[8];		/* 文件名 (空格填充) */
	char	ext[3];		/* 扩展名 (空格填充) */
	t_8	attr;			/* 属性 */
	t_8	reserved[10];		/* 保留 (FAT32 此处含首簇高 16 位) */
	t_16	time;			/* 修改时间 */
	t_16	date;			/* 修改日期 */
	t_16	start_cluster;		/* 起始簇号低 16 位 */
	t_32	file_size;		/* 文件大小 (0=目录) */
} FAT_DIR_ENTRY;

/* FAT 文件系统信息 (从 BPB 解析) */
typedef struct {
	t_16	bytes_per_sector;	/* 每扇区字节数 */
	t_8	sectors_per_cluster;	/* 每簇扇区数 */
	t_16	reserved_sectors;	/* 保留扇区数 (FAT前) */
	t_8	num_fats;		/* FAT 表数量 */
	t_16	root_entry_count;	/* 根目录最大文件数 (FAT32=0) */
	t_32	total_sectors;		/* 总扇区数 */
	t_32	fat_size;		/* 每个 FAT 占扇区数 (FAT32 来自 FATSz32) */
	t_32	fat_start;		/* FAT 表起始扇区 (相对分区) */
	t_32	root_dir_start;		/* 根目录起始扇区 (FAT12/16) */
	t_32	root_dir_sectors;	/* 根目录占用扇区数 (FAT32=0) */
	t_32	data_start;		/* 数据区起始扇区 */
	t_32	total_clusters;		/* 总簇数 */
	t_32	partition_start;	/* 分区起始 LBA (MBR偏移) */
	int	fat_type;		/* 12, 16, 32 */
	/* FAT32 专属字段 */
	t_32	root_cluster;		/* FAT32 根目录起始簇号 (FAT12/16=0) */
	t_32	fsinfo_sector;		/* FAT32 FSInfo 扇区 (相对分区, 0=无) */
	t_32	free_count_hint;	/* FSInfo 中的空闲簇数提示 */
	t_32	next_free_hint;		/* FSInfo 中的下一个空闲簇提示 */
	char	volume_label[12];	/* 卷标 */
	char	fs_type[9];		/* 文件系统类型字符串 */
} FAT_INFO;

/* 函数声明 */
PUBLIC int  fat_init(void);
PUBLIC FAT_INFO *fat_get_info(void);
PUBLIC void fat_list_root(void (*callback)(FAT_DIR_ENTRY *));
PUBLIC int  fat_read_file(const char *name, void *buf, int bufsize);
PUBLIC void fat_format_name(const char *name, char *out83);
PUBLIC int  fat_touch(const char *name);
PUBLIC int  fat_delete(const char *name);
PUBLIC int  fat_format(t_32 total_sectors, int fat_type);
/* fat_type: 0=自动, 12=FAT12, 16=FAT16, 32=FAT32 */
PUBLIC int  fat_write_file(const char *name, void *buf, int size);
PUBLIC int  fat_append_file(const char *name, void *buf, int size);  /* 追加数据到文件末尾 */

/* 诊断变量: 记录最后一次读/写失败的扇区地址和错误码 */
EXTERN t_32 fat_err_sec;
EXTERN int  fat_err_code;

/* FAT32 专属: 获取目录起始簇号 (FAT12/16 返回 0 表示固定根目录) */
PUBLIC t_32 fat_get_root_cluster(void);

/* 公共: 读取指定簇号在 FAT 中的下一簇号 (供 shell 等模块遍历簇链) */
PUBLIC t_32 fat_get_entry_pub(t_32 cluster);

/* 公共: 获取目录项的起始簇号 (FAT32 含高 16 位) */
PUBLIC t_32 fat_get_start_cluster_pub(FAT_DIR_ENTRY *de);

/* 列出指定目录的内容 (dir_cluster=0 表示根目录, >=2 表示子目录)
   callback: 对每个有效目录项调用 (跳过 LFN/已删除/卷标/".") */
PUBLIC void fat_list_dir(t_32 dir_cluster, void (*callback)(FAT_DIR_ENTRY *));

/* ===== 子目录支持 (cd / mkdir) =====
   dir_cluster=0 表示根目录, >=2 表示子目录首簇 */

/* 在指定目录中创建空文件 */
PUBLIC int  fat_touch_in(t_32 dir_cluster, const char *name);

/* 从指定目录删除文件 */
PUBLIC int  fat_delete_in(t_32 dir_cluster, const char *name);

/* 从指定目录读取文件内容 */
PUBLIC int  fat_read_file_in(t_32 dir_cluster, const char *name, void *buf, int bufsize);

/* 查找子目录: 返回首簇号 (>=2=成功, 0=未找到, -1=读盘错误) */
PUBLIC t_32 fat_find_subdir(t_32 dir_cluster, const char *name);

/* 在指定目录中创建子目录 */
PUBLIC int  fat_mkdir_in(t_32 dir_cluster, const char *name);

/* 在指定目录中写入文件 (覆盖或创建) */
PUBLIC int  fat_write_file_in(t_32 dir_cluster, const char *name, void *buf, int size);

#endif /* _CATOS_FAT_H_ */
