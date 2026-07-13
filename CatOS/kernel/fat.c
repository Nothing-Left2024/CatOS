/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               fat.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS FAT12/16/32 文件系统驱动 — 读+写
  从 IDE 硬盘读取 FAT 文件系统, 支持浏览根目录和读写文件

  支持:
    - FAT12 (软盘/小卷 < 16MB)
    - FAT16 (16MB ~ 2GB)
    - FAT32 (>= 2GB, 32 位 FAT 项, 簇链根目录, FSInfo)
    - 自动检测 FAT 类型 (依据簇数判定: <4085→12, <65525→16, 否则→32)
    - MBR 分区表解析 (查找第一个 FAT 分区)
    - Superfloppy 格式 (无 MBR, 扇区 0 即为 BPB)

  FAT32 关键差异:
    - BPB 中 FATSz16=0, 真实值在 FATSz32 (偏移 36)
    - 根目录条目数 RootEntCnt=0, 根目录作为簇链存在
    - 根目录首簇号在 BPB 偏移 44 (RootCluster)
    - FSInfo 扇区记录空闲簇提示 (偏移 48)
    - FAT 表项 32 位, 仅低 28 位有效 (高 4 位保留)
    - 目录项中首簇号分高 16 位 (偏移 20) 和低 16 位 (偏移 26)
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fat.h"
#include "hd.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "proto.h"


/*======================================================================
  全局变量
======================================================================*/
PRIVATE FAT_INFO fat_info;
PRIVATE int fat_initialized = 0;

/* 诊断: 记录最后一次读/写失败的扇区地址和错误码 */
PUBLIC t_32 fat_err_sec = 0;    /* 失败的扇区 LBA */
PUBLIC int  fat_err_code = 0;   /* hd_read/write_sector 的返回值 */

/* 扇区缓冲区 (1024 字节, 支持 FAT 项跨扇区边界时读取两个扇区) */
PRIVATE t_8 sec_buf[1024];

/* FAT32 目录遍历用缓冲区 (一簇最多 128 扇区 = 64KB, 此处用 32 扇区上限足够测试) */
#define DIR_CLUSTER_MAX_SECS  32
PRIVATE t_8 dir_buf[DIR_CLUSTER_MAX_SECS * 512];


/*======================================================================
  内部辅助函数
======================================================================*/

/* 从 16 位小端序读取 */
static inline t_16 rd16(t_8 *p)
{
	return (t_16)p[0] | ((t_16)p[1] << 8);
}

/* 从 32 位小端序读取 */
static inline t_32 rd32(t_8 *p)
{
	return (t_32)p[0] | ((t_32)p[1] << 8) |
	       ((t_32)p[2] << 16) | ((t_32)p[3] << 24);
}

/* 字符串拷贝 (受限) */
static void my_memcpy(char *dst, const char *src, int n)
{
	int i;
	for (i = 0; i < n; i++) dst[i] = src[i];
}

/* 字符串比较 (受限) */
static int my_memcmp(const char *a, const char *b, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (a[i] != b[i]) return a[i] - b[i];
	}
	return 0;
}

/* 返回当前 FAT 类型的 EOF 标记 */
static t_32 fat_eof_mark(void)
{
	if (fat_info.fat_type == 12) return FAT12_EOF;
	if (fat_info.fat_type == 16) return FAT16_EOF;
	return FAT32_EOF;
}

/* 返回当前 FAT 类型的 EOF 范围下限 (>= 此值表示 EOF) */
static t_32 fat_eof_min(void)
{
	if (fat_info.fat_type == 12) return FAT12_EOF_MIN;
	if (fat_info.fat_type == 16) return FAT16_EOF_MIN;
	return FAT32_EOF_MIN;
}


/*======================================================================
  获取 FAT 表项
  FAT12: 每项 12 位, 3 字节存 2 项
  FAT16: 每项 16 位
  FAT32: 每项 32 位 (低 28 位有效)
  返回: 簇号 (或 EOF 标记)
======================================================================*/
static t_32 fat_get_entry(t_32 cluster)
{
	t_32 fat_offset;
	t_32 sec_off;
	int in_sec;
	t_32 value;

	if (fat_info.fat_type == 12) {
		fat_offset = cluster * 3 / 2;

		/* 读 FAT 扇区 (读 2 个扇区避免跨边界) */
		sec_off = fat_info.partition_start + fat_info.fat_start + fat_offset / 512;
		in_sec = fat_offset % 512;

		if (hd_read_sector(sec_off, sec_buf) != 0) return 0;
		/* 处理跨扇区情况 */
		if (in_sec + 1 >= 512) {
			if (hd_read_sector(sec_off + 1, sec_buf + 512) != 0) return 0;
		}

		if (cluster % 2 == 0) {
			/* 偶数簇: 低 12 位 */
			value = (t_32)sec_buf[in_sec] | ((t_32)sec_buf[in_sec + 1] << 8);
			value &= 0x0FFF;
		} else {
			/* 奇数簇: 高 12 位 */
			value = (t_32)sec_buf[in_sec] | ((t_32)sec_buf[in_sec + 1] << 8);
			value >>= 4;
		}
	} else if (fat_info.fat_type == 16) {
		/* FAT16: 每项 2 字节 */
		fat_offset = cluster * 2;
		sec_off = fat_info.partition_start + fat_info.fat_start + fat_offset / 512;
		in_sec = fat_offset % 512;

		if (hd_read_sector(sec_off, sec_buf) != 0) return 0;
		value = rd16(&sec_buf[in_sec]);
	} else {
		/* FAT32: 每项 4 字节, 取低 28 位 */
		fat_offset = cluster * 4;
		sec_off = fat_info.partition_start + fat_info.fat_start + fat_offset / 512;
		in_sec = fat_offset % 512;

		if (hd_read_sector(sec_off, sec_buf) != 0) return 0;
		if (in_sec + 3 >= 512) {
			if (hd_read_sector(sec_off + 1, sec_buf + 512) != 0) return 0;
		}
		value = rd32(&sec_buf[in_sec]) & FAT32_MASK;
	}

	return value;
}


/*======================================================================
  设置 FAT 表项 (同时更新所有 FAT 副本)
  cluster: 簇号
  value:   要写入的值
  返回: 0=成功, -1=失败
======================================================================*/
static int fat_set_entry(t_32 cluster, t_32 value)
{
	t_32 fat_offset;
	t_32 sec_off;
	int in_sec;
	int fi;

	if (fat_info.fat_type == 12) {
		fat_offset = cluster * 3 / 2;
		sec_off = fat_info.partition_start + fat_info.fat_start + fat_offset / 512;
		in_sec = fat_offset % 512;

		if (hd_read_sector(sec_off, sec_buf) != 0) return -1;
		if (in_sec + 1 >= 512) {
			if (hd_read_sector(sec_off + 1, sec_buf + 512) != 0) return -1;
		}

		if (cluster % 2 == 0) {
			/* 偶数簇: 低 12 位 */
			sec_buf[in_sec]     = (t_8)(value & 0xFF);
			sec_buf[in_sec + 1] = (sec_buf[in_sec + 1] & 0xF0) |
			                      (t_8)((value >> 8) & 0x0F);
		} else {
			/* 奇数簇: 高 12 位 */
			sec_buf[in_sec]     = (sec_buf[in_sec] & 0x0F) |
			                      (t_8)((value & 0x0F) << 4);
			sec_buf[in_sec + 1] = (t_8)((value >> 4) & 0xFF);
		}
	} else if (fat_info.fat_type == 16) {
		/* FAT16: 每项 2 字节 */
		fat_offset = cluster * 2;
		sec_off = fat_info.partition_start + fat_info.fat_start + fat_offset / 512;
		in_sec = fat_offset % 512;

		if (hd_read_sector(sec_off, sec_buf) != 0) return -1;
		sec_buf[in_sec]     = (t_8)(value & 0xFF);
		sec_buf[in_sec + 1] = (t_8)((value >> 8) & 0xFF);
	} else {
		/* FAT32: 每项 4 字节, 保留高 4 位 */
		t_32 old_val;
		fat_offset = cluster * 4;
		sec_off = fat_info.partition_start + fat_info.fat_start + fat_offset / 512;
		in_sec = fat_offset % 512;

		if (hd_read_sector(sec_off, sec_buf) != 0) return -1;
		if (in_sec + 3 >= 512) {
			if (hd_read_sector(sec_off + 1, sec_buf + 512) != 0) return -1;
		}
		old_val = rd32(&sec_buf[in_sec]);
		/* 高 4 位保留, 低 28 位用新值 */
		value = (old_val & 0xF0000000) | (value & FAT32_MASK);
		sec_buf[in_sec]     = (t_8)(value & 0xFF);
		sec_buf[in_sec + 1] = (t_8)((value >> 8) & 0xFF);
		sec_buf[in_sec + 2] = (t_8)((value >> 16) & 0xFF);
		sec_buf[in_sec + 3] = (t_8)((value >> 24) & 0xFF);
	}

	/* 写回所有 FAT 副本 */
	/* FAT 项可能跨扇区边界, 此时 sec_buf[0..511] 为第一扇区, sec_buf[512..1023] 为第二扇区 */
	{
		int cross = 0;
		t_8 write_buf[512];

		/* 判断是否跨扇区边界 */
		if (fat_info.fat_type == 12) {
			if (in_sec + 1 >= 512) cross = 1;
		} else if (fat_info.fat_type == 32) {
			if (in_sec + 3 >= 512) cross = 1;
		}
		/* FAT16: 每项 2 字节, 512 是 2 的倍数, 不会跨边界 */

		/* 写回第一个扇区 */
		for (fi = 0; fi < 512; fi++) write_buf[fi] = sec_buf[fi];
		for (fi = 0; fi < fat_info.num_fats; fi++) {
			t_32 write_sec = sec_off + fi * fat_info.fat_size;
			if (hd_write_sector(write_sec, write_buf) != 0) return -1;
		}

		/* 跨扇区边界时写回第二个扇区 */
		if (cross) {
			for (fi = 0; fi < 512; fi++) write_buf[fi] = sec_buf[512 + fi];
			for (fi = 0; fi < fat_info.num_fats; fi++) {
				t_32 write_sec = sec_off + 1 + fi * fat_info.fat_size;
				if (hd_write_sector(write_sec, write_buf) != 0) return -1;
			}
		}
	}
	return 0;
}


/*======================================================================
  解析 BPB (BIOS Parameter Block)
  buf: 指向 512 字节的扇区 0 数据
  part_start: 分区起始 LBA
  返回: 0=成功, -1=无效 BPB
======================================================================*/
static int parse_bpb(t_8 *buf, t_32 part_start)
{
	t_16 bytes_per_sec;
	t_8  sec_per_clus;
	t_16 rsvd_sec_cnt;
	t_8  num_fats;
	t_16 root_ent_cnt;
	t_16 total_sec16;
	t_16 fat_sz16;
	t_32 total_sec32;
	t_32 fat_sz32;
	t_32 total_sec;
	t_32 data_sec;
	t_32 total_clusters;
	t_32 root_dir_sectors;

	/* 检查引导标志 */
	if (buf[510] != 0x55 || buf[511] != 0xAA) return -1;

	/* 读取 BPB 字段 */
	bytes_per_sec  = rd16(&buf[11]);
	sec_per_clus   = buf[13];
	rsvd_sec_cnt   = rd16(&buf[14]);
	num_fats       = buf[16];
	root_ent_cnt   = rd16(&buf[17]);
	total_sec16    = rd16(&buf[19]);
	fat_sz16       = rd16(&buf[22]);
	total_sec32    = rd32(&buf[32]);
	fat_sz32       = rd32(&buf[36]);

	/* 基本有效性检查 */
	if (bytes_per_sec != 512) return -1;
	if (sec_per_clus == 0 || (sec_per_clus & (sec_per_clus - 1)) != 0) return -1;
	if (num_fats == 0) return -1;

	/* 总扇区数 */
	total_sec = (total_sec16 != 0) ? total_sec16 : total_sec32;
	if (total_sec == 0) return -1;

	/* FAT 大小 (FAT32 时 fat_sz16=0, 使用 fat_sz32) */
	fat_info.fat_size = (fat_sz16 != 0) ? fat_sz16 : fat_sz32;
	if (fat_info.fat_size == 0) return -1;

	/* 计算根目录占用扇区数 (FAT32 为 0) */
	root_dir_sectors = ((root_ent_cnt * 32) + bytes_per_sec - 1) / bytes_per_sec;

	/* 计算数据区扇区数 */
	data_sec = total_sec - rsvd_sec_cnt - num_fats * fat_info.fat_size - root_dir_sectors;

	/* 计算总簇数 */
	total_clusters = data_sec / sec_per_clus;

	/* 根据簇数判断 FAT 类型 (微软官方阈值) */
	if (total_clusters < FAT12_MAX_CLUSTERS) {
		fat_info.fat_type = 12;
	} else if (total_clusters < FAT16_MAX_CLUSTERS) {
		fat_info.fat_type = 16;
	} else {
		fat_info.fat_type = 32;
	}

	/* 填充 FAT_INFO 结构 */
	fat_info.bytes_per_sector   = bytes_per_sec;
	fat_info.sectors_per_cluster = sec_per_clus;
	fat_info.reserved_sectors   = rsvd_sec_cnt;
	fat_info.num_fats           = num_fats;
	fat_info.root_entry_count   = root_ent_cnt;
	fat_info.total_sectors      = total_sec;
	fat_info.fat_start          = rsvd_sec_cnt;
	fat_info.root_dir_start     = rsvd_sec_cnt + num_fats * fat_info.fat_size;
	fat_info.root_dir_sectors   = root_dir_sectors;
	fat_info.data_start         = fat_info.root_dir_start + root_dir_sectors;
	fat_info.total_clusters     = total_clusters;
	fat_info.partition_start    = part_start;

	/* FAT32 专属字段 */
	if (fat_info.fat_type == 32) {
		fat_info.root_cluster   = rd32(&buf[44]);  /* RootCluster */
		fat_info.fsinfo_sector  = rd16(&buf[48]);  /* FSInfo 扇区 */
		/* FAT32 根目录无固定扇区, 用 root_cluster 跟踪 */
		fat_info.root_dir_start = 0;
		fat_info.root_dir_sectors = 0;
		/* 读取 FSInfo 获取提示 */
		if (fat_info.fsinfo_sector != 0 && fat_info.fsinfo_sector != 0xFFFF) {
			t_8 fsinfo[512];
			t_32 fsi_sec = part_start + fat_info.fsinfo_sector;
			if (hd_read_sector(fsi_sec, fsinfo) == 0) {
				/* 校验 FSI_LeadSig (0x41615252) 和 FSI_StructSig (0x61417272) */
				if (rd32(&fsinfo[0]) == 0x41615252 &&
				    rd32(&fsinfo[484]) == 0x61417272) {
					fat_info.free_count_hint = rd32(&fsinfo[488]);
					fat_info.next_free_hint  = rd32(&fsinfo[492]);
				} else {
					fat_info.free_count_hint = 0xFFFFFFFF;
					fat_info.next_free_hint  = 0xFFFFFFFF;
				}
			} else {
				fat_info.free_count_hint = 0xFFFFFFFF;
				fat_info.next_free_hint  = 0xFFFFFFFF;
			}
		} else {
			fat_info.fsinfo_sector = 0;
			fat_info.free_count_hint = 0xFFFFFFFF;
			fat_info.next_free_hint  = 0xFFFFFFFF;
		}
		/* FAT32 卷标在 BPB 偏移 71 (FAT12/16 在 43) */
		my_memcpy(fat_info.volume_label, (char *)&buf[71], 11);
		fat_info.volume_label[11] = '\0';
		fat_info.fs_type[0] = '\0';
	} else {
		/* FAT12/16 */
		fat_info.root_cluster   = 0;
		fat_info.fsinfo_sector  = 0;
		fat_info.free_count_hint = 0xFFFFFFFF;
		fat_info.next_free_hint  = 0xFFFFFFFF;
		/* 卷标和文件系统类型 (偏移 43 和 54) */
		my_memcpy(fat_info.volume_label, (char *)&buf[43], 11);
		fat_info.volume_label[11] = '\0';
		my_memcpy(fat_info.fs_type, (char *)&buf[54], 8);
		fat_info.fs_type[8] = '\0';
	}

	return 0;
}


/*======================================================================
  初始化 FAT 文件系统
  尝试从硬盘读取 BPB (支持 Superfloppy 和 MBR 分区)
  返回: 0=成功, -1=失败
======================================================================*/
PUBLIC int fat_init(void)
{
	t_8 mbr_buf[512];
	t_32 part_start;
	int i;

	if (fat_initialized) return 0;

	/* 初始化 IDE 控制器 (执行软复位, 确保写操作可靠) */
	hd_init();

	/* 方案1: Superfloppy — 扇区 0 即为 BPB */
	if (hd_read_sector(0, sec_buf) != 0) return -1;
	if (parse_bpb(sec_buf, 0) == 0) {
		fat_initialized = 1;
		return 0;
	}

	/* 方案2: MBR 分区表 — 查找第一个 FAT 分区 */
	if (hd_read_sector(0, mbr_buf) != 0) return -1;
	if (mbr_buf[510] != 0x55 || mbr_buf[511] != 0xAA) return -1;

	/* 检查分区表 (4 个条目, 偏移 0x1BE, 每条 16 字节) */
	{
		t_8 *pt = &mbr_buf[0x1BE];
		for (i = 0; i < 4; i++, pt += 16) {
			t_8 type = pt[4];          /* 分区类型 */
			if (type == 0x01 ||        /* FAT12 */
			    type == 0x04 ||        /* FAT16 < 32MB */
			    type == 0x06 ||        /* FAT16 > 32MB */
			    type == 0x0E ||        /* FAT16 LBA */
			    type == 0x0B ||        /* FAT32 */
			    type == 0x0C) {        /* FAT32 LBA */
				part_start = rd32(&pt[8]);
				if (hd_read_sector(part_start, sec_buf) != 0) continue;
				if (parse_bpb(sec_buf, part_start) == 0) {
					fat_initialized = 1;
					return 0;
				}
			}
		}
	}

	return -1;  /* 未找到有效 FAT 分区 */
}


/*======================================================================
  获取 FAT 信息指针
======================================================================*/
PUBLIC FAT_INFO *fat_get_info(void)
{
	if (!fat_initialized) return (FAT_INFO *)0;
	return &fat_info;
}

/* FAT32 专属: 获取目录起始簇号 */
PUBLIC t_32 fat_get_root_cluster(void)
{
	return fat_info.root_cluster;
}

/* 公共: 读取指定簇号的下一簇 (供 shell 等模块遍历簇链) */
PUBLIC t_32 fat_get_entry_pub(t_32 cluster)
{
	return fat_get_entry(cluster);
}

/* 前向声明: fat_get_start_cluster 定义在后面 */
static t_32 fat_get_start_cluster(FAT_DIR_ENTRY *de);

/* 公共: 获取目录项起始簇号 (FAT32 含高 16 位) */
PUBLIC t_32 fat_get_start_cluster_pub(FAT_DIR_ENTRY *de)
{
	return fat_get_start_cluster(de);
}

/*======================================================================
  列出指定目录内容
  dir_cluster = 0: 根目录
  dir_cluster >= 2: 子目录 (按簇链遍历)
  callback: 对每个有效条目调用 (跳过 LFN/已删除/卷标/".")
======================================================================*/
PUBLIC void fat_list_dir(t_32 dir_cluster, void (*callback)(FAT_DIR_ENTRY *))
{
	int entry;
	FAT_DIR_ENTRY *de;

	if (!fat_initialized) return;

	if (dir_cluster == 0 && fat_info.fat_type != 32) {
		/* FAT12/16 根目录: 固定扇区 */
		t_32 root_sec = fat_info.partition_start + fat_info.root_dir_start;
		t_32 s;
		for (s = 0; s < fat_info.root_dir_sectors; s++) {
			if (hd_read_sector(root_sec + s, sec_buf) != 0) return;
			for (entry = 0; entry < 16; entry++) {
				de = (FAT_DIR_ENTRY *)&sec_buf[entry * 32];
				if ((t_8)de->name[0] == 0x00) return;
				if ((t_8)de->name[0] == 0xE5) continue;
				if (de->attr == FAT_ATTR_LFN) continue;
				if (de->attr & FAT_ATTR_VOLUME_ID) continue;
				callback(de);
			}
		}
	} else {
		/* FAT32 根目录 (dir_cluster=0 → root_cluster) 或任意子目录 */
		t_32 cluster = (dir_cluster == 0) ? fat_info.root_cluster : dir_cluster;
		t_32 eof_min;
		int safety = 0;

		if (fat_info.fat_type == 12)      eof_min = FAT12_EOF_MIN;
		else if (fat_info.fat_type == 16)  eof_min = FAT16_EOF_MIN;
		else                              eof_min = FAT32_EOF_MIN;

		while (cluster >= 2 && cluster < eof_min && safety < 10000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cluster - 2) * fat_info.sectors_per_cluster;
			int nsec = fat_info.sectors_per_cluster;
			int csec;
			if (nsec > DIR_CLUSTER_MAX_SECS) nsec = DIR_CLUSTER_MAX_SECS;
			for (csec = 0; csec < nsec; csec++) {
				if (hd_read_sector(sec + csec, &dir_buf[csec * 512]) != 0) return;
				for (entry = 0; entry < 16; entry++) {
					de = (FAT_DIR_ENTRY *)&dir_buf[csec * 512 + entry * 32];
					if ((t_8)de->name[0] == 0x00) return;
					if ((t_8)de->name[0] == 0xE5) continue;
					if (de->attr == FAT_ATTR_LFN) continue;
					if (de->attr & FAT_ATTR_VOLUME_ID) continue;
					callback(de);
				}
			}
			cluster = fat_get_entry(cluster);
			safety++;
		}
	}
}


/*======================================================================
  列出根目录
  callback: 对每个有效目录项调用的回调函数 (跳过 LFN 和已删除项)

  FAT12/16: 遍历固定根目录扇区
  FAT32:    跟踪 root_cluster 簇链
======================================================================*/
PUBLIC void fat_list_root(void (*callback)(FAT_DIR_ENTRY *))
{
	t_32 s;
	int entry;
	FAT_DIR_ENTRY *de;

	if (!fat_initialized) return;

	if (fat_info.fat_type == 32) {
		/* FAT32: 跟踪簇链遍历 */
		t_32 cluster = fat_info.root_cluster;
		t_32 eof_min = FAT32_EOF_MIN;
		int safety = 0;

		while (cluster >= 2 && cluster < eof_min && safety < 10000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cluster - 2) * fat_info.sectors_per_cluster;
			int nsec = fat_info.sectors_per_cluster;
			int csec;

			/* 限制单簇扇区数防止溢出 */
			if (nsec > DIR_CLUSTER_MAX_SECS) nsec = DIR_CLUSTER_MAX_SECS;

			for (csec = 0; csec < nsec; csec++) {
				if (hd_read_sector(sec + csec, &dir_buf[csec * 512]) != 0) return;

				for (entry = 0; entry < 16; entry++) {
					de = (FAT_DIR_ENTRY *)&dir_buf[csec * 512 + entry * 32];

					if ((t_8)de->name[0] == 0x00) return;  /* 目录结束 */
					if ((t_8)de->name[0] == 0xE5) continue; /* 已删除 */
					if (de->attr == FAT_ATTR_LFN) continue;  /* LFN 条目 */
					if (de->attr & FAT_ATTR_VOLUME_ID) continue; /* 卷标 */

					callback(de);
				}
			}
			cluster = fat_get_entry(cluster);
			safety++;
		}
	} else {
		/* FAT12/16: 固定根目录扇区 */
		t_32 root_sec = fat_info.partition_start + fat_info.root_dir_start;
		t_32 sec_count = fat_info.root_dir_sectors;

		for (s = 0; s < sec_count; s++) {
			if (hd_read_sector(root_sec + s, sec_buf) != 0) break;

			for (entry = 0; entry < 16; entry++) {
				de = (FAT_DIR_ENTRY *)&sec_buf[entry * 32];

				if ((t_8)de->name[0] == 0x00) return;  /* 目录结束 */
				if ((t_8)de->name[0] == 0xE5) continue; /* 已删除 */
				if (de->attr == FAT_ATTR_LFN) continue;  /* LFN 条目 */
				if (de->attr & FAT_ATTR_VOLUME_ID) continue; /* 卷标 */

				callback(de);
			}
		}
	}
}


/*======================================================================
  将普通文件名转换为 8.3 格式
  "hello.txt" -> "HELLO   TXT"
  "readme"    -> "README     "
======================================================================*/
PUBLIC void fat_format_name(const char *name, char *out83)
{
	int i = 0;
	const char *dot;

	/* 清空输出 (11 个空格) */
	for (i = 0; i < 11; i++) out83[i] = ' ';

	/* 特殊处理 "." 和 ".." (目录项中的当前目录和父目录标记) */
	if (name[0] == '.' && name[1] == '\0') {
		out83[0] = '.';
		return;
	}
	if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
		out83[0] = '.';
		out83[1] = '.';
		return;
	}

	/* 查找 '.' 分隔符 */
	dot = (const char *)0;
	for (i = 0; name[i]; i++) {
		if (name[i] == '.') { dot = &name[i]; break; }
	}

	/* 拷贝文件名部分 (最多 8 字符, 转大写) */
	if (dot) {
		for (i = 0; i < 8 && &name[i] < dot; i++) {
			char c = name[i];
			if (c >= 'a' && c <= 'z') c -= 32;
			out83[i] = c;
		}
		/* 拷贝扩展名部分 (最多 3 字节, 跳过 '.') */
		dot++;
		for (i = 0; i < 3 && dot[i]; i++) {
			char c = dot[i];
			if (c >= 'a' && c <= 'z') c -= 32;
			out83[8 + i] = c;
		}
	} else {
		/* 无扩展名 */
		for (i = 0; i < 8 && name[i]; i++) {
			char c = name[i];
			if (c >= 'a' && c <= 'z') c -= 32;
			out83[i] = c;
		}
	}
}


/*======================================================================
  从 FAT_DIR_ENTRY 取完整 32 位首簇号
  FAT32: 高 16 位在 reserved[4..5], 低 16 位在 start_cluster
  FAT12/16: 仅 start_cluster
======================================================================*/
static t_32 fat_get_start_cluster(FAT_DIR_ENTRY *de)
{
	if (fat_info.fat_type == 32) {
		t_16 hi = (t_16)de->reserved[8] | ((t_16)de->reserved[9] << 8);
		return ((t_32)hi << 16) | de->start_cluster;
	}
	return de->start_cluster;
}


/*======================================================================
  读取簇链数据到缓冲区
  start_cluster: 起始簇
  buf: 输出缓冲区
  bufsize: 缓冲区最大字节
  返回: 实际读取字节数, -1=失败
======================================================================*/
static int fat_read_chain(t_32 start_cluster, t_8 *out, int bufsize)
{
	t_32 cluster = start_cluster;
	t_32 eof_min = fat_eof_min();
	int bytes_read = 0;
	int safety = 0;

	if (start_cluster < 2) return 0;

	while (cluster >= 2 && cluster < eof_min && safety < 100000) {
		t_32 sec = fat_info.partition_start + fat_info.data_start +
		           (cluster - 2) * fat_info.sectors_per_cluster;
		int csec;
		int nsec = fat_info.sectors_per_cluster;

		for (csec = 0; csec < nsec; csec++) {
			int to_copy;
			if (bytes_read >= bufsize) return bytes_read;
			if (hd_read_sector(sec + csec, sec_buf) != 0)
				return bytes_read;

			to_copy = bufsize - bytes_read;
			if (to_copy > 512) to_copy = 512;
			my_memcpy((char *)&out[bytes_read], (char *)sec_buf, to_copy);
			bytes_read += to_copy;
		}

		cluster = fat_get_entry(cluster);
		safety++;
	}

	return bytes_read;
}


/*======================================================================
  比较目录项名称与 8.3 格式名 (不区分大小写)
  返回: 1=匹配, 0=不匹配
======================================================================*/
PRIVATE int fat_name_matches(const FAT_DIR_ENTRY *de, const char *name83)
{
	char de_name[11];
	int j;
	for (j = 0; j < 11; j++) {
		char c = de->name[j];
		if (c >= 'a' && c <= 'z') c -= 32;
		de_name[j] = c;
	}
	return (my_memcmp(name83, de_name, 11) == 0) ? 1 : 0;
}


/*======================================================================
  在根目录中搜索指定文件名的目录项
  name83: 已格式化的 8.3 文件名 (11 字节)
  输出: out_sec = 目录项所在扇区号, out_entry = 扇区内索引 (0-15)
  返回: 1=找到, 0=未找到, -1=读盘错误
======================================================================*/
PRIVATE int fat_find_dir_entry(const char *name83, t_32 *out_sec, int *out_entry)
{
	if (fat_info.fat_type == 32) {
		/* FAT32: 跟踪根目录簇链 */
		t_32 cur = fat_info.root_cluster;
		int safety = 0;
		int done = 0;

		while (!done && cur >= 2 && cur < FAT32_EOF_MIN && safety < 10000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cur - 2) * fat_info.sectors_per_cluster;
			int nsec = fat_info.sectors_per_cluster;
			int csec;
			if (nsec > DIR_CLUSTER_MAX_SECS) nsec = DIR_CLUSTER_MAX_SECS;

			for (csec = 0; csec < nsec && !done; csec++) {
				int rd_ret = hd_read_sector(sec + csec, &dir_buf[csec * 512]);
				int entry;
				if (rd_ret != 0) {
					fat_err_sec = sec + csec;
					fat_err_code = rd_ret;
					return -1;
				}
				for (entry = 0; entry < 16; entry++) {
					FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&dir_buf[csec * 512 + entry * 32];
					if ((t_8)de->name[0] == 0x00) { done = 1; break; }
					if ((t_8)de->name[0] == 0xE5) continue;
					if (de->attr == FAT_ATTR_LFN) continue;
					if (de->attr & FAT_ATTR_VOLUME_ID) continue;
					if (fat_name_matches(de, name83)) {
						*out_sec = sec + csec;
						*out_entry = entry;
						return 1;
					}
				}
			}
			if (!done) cur = fat_get_entry(cur);
			safety++;
		}
		return 0;
	} else {
		/* FAT12/16: 固定根目录扇区 */
		t_32 root_sec = fat_info.partition_start + fat_info.root_dir_start;
		t_32 sec_count = fat_info.root_dir_sectors;
		t_32 s;
		int done = 0;

		for (s = 0; s < sec_count && !done; s++) {
			int rd_ret = hd_read_sector(root_sec + s, sec_buf);
			int entry;
			if (rd_ret != 0) {
				fat_err_sec = root_sec + s;
				fat_err_code = rd_ret;
				return -1;
			}
			for (entry = 0; entry < 16; entry++) {
				FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&sec_buf[entry * 32];
				if ((t_8)de->name[0] == 0x00) { done = 1; break; }
				if ((t_8)de->name[0] == 0xE5) continue;
				if (de->attr == FAT_ATTR_LFN) continue;
				if (de->attr & FAT_ATTR_VOLUME_ID) continue;
				if (fat_name_matches(de, name83)) {
					*out_sec = root_sec + s;
					*out_entry = entry;
					return 1;
				}
			}
		}
		return 0;
	}
}


/*======================================================================
  在根目录中搜索空闲目录项位置
  输出: out_sec = 空闲位置所在扇区号, out_entry = 扇区内索引 (0-15)
  返回: 1=找到, 0=未找到(目录满), -1=读盘错误
======================================================================*/
PRIVATE int fat_find_free_entry(t_32 *out_sec, int *out_entry)
{
	if (fat_info.fat_type == 32) {
		/* FAT32: 跟踪根目录簇链 */
		t_32 cur = fat_info.root_cluster;
		int safety = 0;

		while (cur >= 2 && cur < FAT32_EOF_MIN && safety < 10000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cur - 2) * fat_info.sectors_per_cluster;
			int nsec = fat_info.sectors_per_cluster;
			int csec;
			if (nsec > DIR_CLUSTER_MAX_SECS) nsec = DIR_CLUSTER_MAX_SECS;

			for (csec = 0; csec < nsec; csec++) {
				int rd_ret = hd_read_sector(sec + csec, &dir_buf[csec * 512]);
				int entry;
				if (rd_ret != 0) {
					fat_err_sec = sec + csec;
					fat_err_code = rd_ret;
					return -1;
				}
				for (entry = 0; entry < 16; entry++) {
					FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&dir_buf[csec * 512 + entry * 32];
					if ((t_8)de->name[0] == 0x00 || (t_8)de->name[0] == 0xE5) {
						*out_sec = sec + csec;
						*out_entry = entry;
						return 1;  /* 0x00: 后续都是空的, 直接返回 */
					}
				}
			}
			cur = fat_get_entry(cur);
			safety++;
		}
		return 0;
	} else {
		/* FAT12/16: 固定根目录扇区 */
		t_32 root_sec = fat_info.partition_start + fat_info.root_dir_start;
		t_32 sec_count = fat_info.root_dir_sectors;
		t_32 s;

		for (s = 0; s < sec_count; s++) {
			int rd_ret = hd_read_sector(root_sec + s, sec_buf);
			int entry;
			if (rd_ret != 0) {
				fat_err_sec = root_sec + s;
				fat_err_code = rd_ret;
				return -1;
			}
			for (entry = 0; entry < 16; entry++) {
				FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&sec_buf[entry * 32];
				if ((t_8)de->name[0] == 0x00 || (t_8)de->name[0] == 0xE5) {
					*out_sec = root_sec + s;
					*out_entry = entry;
					return 1;
				}
			}
		}
		return 0;
	}
}


/*======================================================================
  读取文件内容
  name: 文件名 (如 "readme.txt", 大小写不敏感)
  buf:  输出缓冲区
  bufsize: 缓冲区大小
  返回: 实际读取字节数, -1=文件未找到或读取失败
======================================================================*/
PUBLIC int fat_read_file(const char *name, void *buf, int bufsize)
{
	FAT_DIR_ENTRY *de;
	char name83[11];
	t_32 cluster;
	t_32 file_size;
	int bytes_read;
	t_8 *out = (t_8 *)buf;
	t_32 found_sec;
	int found_entry;
	t_32 found_cluster;
	t_32 found_size;
	int ret;

	if (!fat_initialized) return -1;

	/* 将输入文件名转为 8.3 格式 */
	fat_format_name(name, name83);

	/* === 遍历根目录查找文件 === */
	ret = fat_find_dir_entry(name83, &found_sec, &found_entry);
	if (ret <= 0) return -1;  /* 未找到或读盘错误 */

	/* 重新读扇区, 获取目录项数据 */
	if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
	found_cluster = fat_get_start_cluster(de);
	found_size = de->file_size;

	/* 跟踪 FAT 链读取文件数据 */
	cluster = found_cluster;
	file_size = found_size;
	if (file_size == 0) return 0;
	bytes_read = 0;

	{
		t_32 eof_min = fat_eof_min();
		int safety = 0;
		while (cluster >= 2 && cluster < eof_min && safety < 100000) {
			t_32 sec;
			int csec;
			int to_copy;

			sec = fat_info.partition_start + fat_info.data_start +
			      (cluster - 2) * fat_info.sectors_per_cluster;

			for (csec = 0; csec < fat_info.sectors_per_cluster; csec++) {
				if (hd_read_sector(sec + csec, sec_buf) != 0)
					return bytes_read;

				to_copy = file_size - bytes_read;
				if (to_copy > 512) to_copy = 512;
				if (to_copy > bufsize - bytes_read) to_copy = bufsize - bytes_read;
				if (to_copy <= 0) return bytes_read;

				my_memcpy((char *)&out[bytes_read], (char *)sec_buf, to_copy);
				bytes_read += to_copy;

				if (bytes_read >= (int)file_size || bytes_read >= bufsize)
					return bytes_read;
			}

			cluster = fat_get_entry(cluster);
			safety++;
		}
	}

	return bytes_read;
}


/*======================================================================
  分配一个空闲簇: 在 FAT 中查找值为 0 的条目, 标记为 EOF
  返回: 簇号, 0=无空闲簇
======================================================================*/
static t_32 fat_alloc_cluster(void)
{
	t_32 cluster;
	t_32 eof = fat_eof_mark();
	t_32 hint_start = 2;

	/* FAT32 优先使用 FSInfo 提示 */
	if (fat_info.fat_type == 32 && fat_info.next_free_hint != 0xFFFFFFFF &&
	    fat_info.next_free_hint >= 2 && fat_info.next_free_hint < fat_info.total_clusters + 2) {
		hint_start = fat_info.next_free_hint;
	}

	/* 先从提示位置向后搜索 */
	for (cluster = hint_start; cluster < fat_info.total_clusters + 2; cluster++) {
		if (fat_get_entry(cluster) == 0) {
			fat_set_entry(cluster, eof);
			return cluster;
		}
	}
	/* 再从头搜索 */
	for (cluster = 2; cluster < hint_start; cluster++) {
		if (fat_get_entry(cluster) == 0) {
			fat_set_entry(cluster, eof);
			return cluster;
		}
	}
	return 0;
}


/*======================================================================
  释放簇链: 从 start_cluster 开始, 逐个置 0 (空闲)
======================================================================*/
static void fat_free_chain(t_32 start_cluster)
{
	t_32 cluster = start_cluster;
	t_32 eof_min = fat_eof_min();

	while (cluster >= 2 && cluster < eof_min) {
		t_32 next = fat_get_entry(cluster);
		fat_set_entry(cluster, 0);
		if (next == 0) break;
		cluster = next;
	}
}


/*======================================================================
  创建空文件 (touch)
  name: 文件名 (如 "test.txt")
  返回: 0=成功, 1=文件已存在
    -1 = 未初始化
    -2 = 读取根目录扇区失败 (遍历)
    -3 = 未找到空目录项 (根目录已满)
    -4 = 重新读取目录扇区失败
    -5 = 写入目录扇区失败 (hd_write_sector 错误码 -10 起)
        -10 = wait_ready 失败, -11 = wait_drq 失败
        -12 = wait_bsy_clear 失败, -13 = ERR 位置位
======================================================================*/
PUBLIC int fat_touch(const char *name)
{
	char name83[11];
	t_32 free_sec = 0;
	int free_entry = 0;
	FAT_DIR_ENTRY *de;
	int j;
	int wr_ret;
	int ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	/* === 检查文件是否已存在 === */
	ret = fat_find_dir_entry(name83, &free_sec, &free_entry);
	if (ret < 0) return -2;  /* 读盘错误 */
	if (ret == 1) return 1;   /* 文件已存在 */

	/* === 查找空闲目录项 === */
	ret = fat_find_free_entry(&free_sec, &free_entry);
	if (ret < 0) return -2;  /* 读盘错误 */
	if (ret == 0) return -3;  /* 根目录已满 */

	/* 重新读扇区, 填写目录项 */
	{
		int rd_ret = hd_read_sector(free_sec, sec_buf);
		if (rd_ret != 0) {
			fat_err_sec = free_sec;
			fat_err_code = rd_ret;
			return -4;
		}
	}
	de = (FAT_DIR_ENTRY *)&sec_buf[free_entry * 32];
	my_memcpy(de->name, name83, 8);
	my_memcpy(de->ext, name83 + 8, 3);
	de->attr = FAT_ATTR_ARCHIVE;
	for (j = 0; j < 10; j++) de->reserved[j] = 0;
	de->time = 0; de->date = 0;
	de->start_cluster = 0;
	de->file_size = 0;
	wr_ret = hd_write_sector(free_sec, sec_buf);
	if (wr_ret != 0) {
		fat_err_sec = free_sec;
		fat_err_code = wr_ret;
		return -10 + wr_ret;  /* -10 ~ -13 */
	}
	return 0;
}


/*======================================================================
  删除文件
  name: 文件名 (如 "test.txt")
  返回: 0=成功, 1=文件不存在, 负数=失败
    -1 = FAT 未初始化
    -2 = 读根目录扇区失败
    -3 = 重新读目录扇区失败
    -4 = 写目录扇区失败 (错误码见 fat_err_code)
======================================================================*/
PUBLIC int fat_delete(const char *name)
{
	char name83[11];
	FAT_DIR_ENTRY *de;
	int wr_ret;
	t_32 found_sec;
	int found_entry;
	int ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	/* === 在根目录中搜索同名文件 === */
	ret = fat_find_dir_entry(name83, &found_sec, &found_entry);
	if (ret < 0) return -2;  /* 读盘错误 */
	if (ret == 0) return 1;   /* 文件不存在 */

	/* 重新读扇区 */
	{
		int rd_ret = hd_read_sector(found_sec, sec_buf);
		if (rd_ret != 0) {
			fat_err_sec = found_sec;
			fat_err_code = rd_ret;
			return -3;
		}
	}
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];

	/* 跳过子目录 */
	if (de->attr & FAT_ATTR_DIRECTORY) return 1;

	/* 释放簇链 (如果文件非空) */
	/* 注意: fat_free_chain 内部使用 sec_buf, 会覆盖目录项数据, 需先保存簇号 */
	{
		t_32 eof_min = fat_eof_min();
		t_32 sc = fat_get_start_cluster(de);
		if (sc >= 2 && sc < eof_min) {
			fat_free_chain(sc);
		}
	}

	/* 重新读入目录扇区 (sec_buf 已被 fat_free_chain 覆盖) */
	{
		int rd_ret = hd_read_sector(found_sec, sec_buf);
		if (rd_ret != 0) {
			fat_err_sec = found_sec;
			fat_err_code = rd_ret;
			return -3;
		}
	}
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];

	/* 标记为已删除 */
	de->name[0] = 0xE5;
	wr_ret = hd_write_sector(found_sec, sec_buf);
	if (wr_ret != 0) {
		fat_err_sec = found_sec;
		fat_err_code = wr_ret;
		return -4;
	}
	return 0;
}


/*======================================================================
  写入文件 (创建或覆盖)
  name: 文件名 (如 "test.txt")
  buf:  文件内容
  size: 文件大小 (字节)
  返回: 0=成功, -1=失败

  支持三种 FAT 类型, FAT32 时同时维护首簇高 16 位
======================================================================*/
PUBLIC int fat_write_file(const char *name, void *buf, int size)
{
	char name83[11];
	int found = 0;
	t_32 found_sec = 0;
	int found_entry = -1;
	t_32 old_cluster = 0;
	FAT_DIR_ENTRY *de;
	int j;
	t_32 first_cluster = 0, prev_cluster = 0;
	int cluster_size, bytes_left;
	t_8 *data;
	int ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	/* === 在根目录中搜索同名文件 === */
	ret = fat_find_dir_entry(name83, &found_sec, &found_entry);
	if (ret < 0) return -1;
	if (ret == 1) {
		found = 1;
		/* 读取旧文件的首簇 (用于释放旧簇链) */
		if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
		de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		old_cluster = fat_get_start_cluster(de);
	}

	/* 文件不存在: 找空目录项 */
	if (!found) {
		ret = fat_find_free_entry(&found_sec, &found_entry);
		if (ret <= 0) return -1;  /* 读盘错误或目录满 */
	}

	/* 释放旧簇链 */
	if (old_cluster >= 2) {
		fat_free_chain(old_cluster);
	}

	/* 空文件: 只需更新目录项 */
	if (size == 0) {
		if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
		de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		my_memcpy(de->name, name83, 8);
		my_memcpy(de->ext, name83 + 8, 3);
		de->attr = FAT_ATTR_ARCHIVE;
		for (j = 0; j < 10; j++) de->reserved[j] = 0;
		de->time = 0; de->date = 0;
		de->start_cluster = 0;
		de->file_size = 0;
		if (hd_write_sector(found_sec, sec_buf) != 0) return -1;
		return 0;
	}

	/* 分配簇并写入数据 */
	cluster_size = fat_info.sectors_per_cluster * 512;
	data = (t_8 *)buf;
	first_cluster = 0;
	prev_cluster = 0;
	bytes_left = size;

	while (bytes_left > 0) {
		t_32 cluster = fat_alloc_cluster();
		t_32 sec;
		int csec, to_write;

		if (cluster == 0) return -1;  /* 磁盘满 */

		if (prev_cluster != 0) {
			fat_set_entry(prev_cluster, cluster);
		} else {
			first_cluster = cluster;
		}

		sec = fat_info.partition_start + fat_info.data_start +
		      (cluster - 2) * fat_info.sectors_per_cluster;

		to_write = bytes_left;
		if (to_write > cluster_size) to_write = cluster_size;

		/* 逐扇区写入 */
		for (csec = 0; csec < fat_info.sectors_per_cluster; csec++) {
			t_8 wbuf[512];
			int off = size - bytes_left + csec * 512;
			int chunk = to_write - csec * 512;

			for (j = 0; j < 512; j++) wbuf[j] = 0;
			if (chunk > 0) {
				if (chunk > 512) chunk = 512;
				my_memcpy((char *)wbuf, (char *)&data[off], chunk);
			}
			if (hd_write_sector(sec + csec, wbuf) != 0) return -1;
		}

		bytes_left -= to_write;
		prev_cluster = cluster;
	}

	/* 更新目录项 */
	if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
	my_memcpy(de->name, name83, 8);
	my_memcpy(de->ext, name83 + 8, 3);
	de->attr = FAT_ATTR_ARCHIVE;
	for (j = 0; j < 10; j++) de->reserved[j] = 0;
	/* FAT32: 首簇高 16 位写入 reserved[8..9] (offset 20-21 in dir entry) */
	if (fat_info.fat_type == 32) {
		de->reserved[8] = (t_8)((first_cluster >> 16) & 0xFF);
		de->reserved[9] = (t_8)((first_cluster >> 24) & 0xFF);
	}
	de->time = 0; de->date = 0;
	de->start_cluster = (t_16)(first_cluster & 0xFFFF);
	de->file_size = size;
	if (hd_write_sector(found_sec, sec_buf) != 0) return -1;

	return 0;
}


/* 前向声明: 子目录支持函数 (定义在文件后面) */
PRIVATE int fat_find_dir_entry_in(t_32 dir_cluster, const char *name83,
                                  t_32 *out_sec, int *out_entry);
PRIVATE int fat_find_free_entry_in(t_32 dir_cluster, t_32 *out_sec, int *out_entry);

/*----------------------------------------------------------------------
  在指定目录中写入文件 (覆盖或创建)
  dir_cluster=0 表示根目录, >=2 表示子目录首簇
  返回: 0=成功, -1=失败
----------------------------------------------------------------------*/
PUBLIC int fat_write_file_in(t_32 dir_cluster, const char *name, void *buf, int size)
{
	char name83[11];
	int found = 0;
	t_32 found_sec = 0;
	int found_entry = -1;
	t_32 old_cluster = 0;
	FAT_DIR_ENTRY *de;
	int j;
	t_32 first_cluster = 0, prev_cluster = 0;
	int cluster_size, bytes_left;
	t_8 *data;
	int ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	/* === 在指定目录中搜索同名文件 === */
	ret = fat_find_dir_entry_in(dir_cluster, name83, &found_sec, &found_entry);
	if (ret < 0) return -1;
	if (ret == 1) {
		found = 1;
		/* 读取旧文件的首簇 (用于释放旧簇链) */
		if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
		de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		old_cluster = fat_get_start_cluster(de);
	}

	/* 文件不存在: 找空目录项 */
	if (!found) {
		ret = fat_find_free_entry_in(dir_cluster, &found_sec, &found_entry);
		if (ret <= 0) return -1;  /* 读盘错误或目录满 */
	}

	/* 释放旧簇链 */
	if (old_cluster >= 2) {
		fat_free_chain(old_cluster);
	}

	/* 空文件: 只需更新目录项 */
	if (size == 0) {
		if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
		de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		my_memcpy(de->name, name83, 8);
		my_memcpy(de->ext, name83 + 8, 3);
		de->attr = FAT_ATTR_ARCHIVE;
		for (j = 0; j < 10; j++) de->reserved[j] = 0;
		de->time = 0; de->date = 0;
		de->start_cluster = 0;
		de->file_size = 0;
		if (hd_write_sector(found_sec, sec_buf) != 0) return -1;
		return 0;
	}

	/* 分配簇并写入数据 */
	cluster_size = fat_info.sectors_per_cluster * 512;
	data = (t_8 *)buf;
	first_cluster = 0;
	prev_cluster = 0;
	bytes_left = size;

	while (bytes_left > 0) {
		t_32 cluster = fat_alloc_cluster();
		t_32 sec;
		int csec, to_write;

		if (cluster == 0) return -1;  /* 磁盘满 */

		if (prev_cluster != 0) {
			fat_set_entry(prev_cluster, cluster);
		} else {
			first_cluster = cluster;
		}

		sec = fat_info.partition_start + fat_info.data_start +
		      (cluster - 2) * fat_info.sectors_per_cluster;

		to_write = bytes_left;
		if (to_write > cluster_size) to_write = cluster_size;

		/* 逐扇区写入 */
		for (csec = 0; csec < fat_info.sectors_per_cluster; csec++) {
			t_8 wbuf[512];
			int off = size - bytes_left + csec * 512;
			int chunk = to_write - csec * 512;

			for (j = 0; j < 512; j++) wbuf[j] = 0;
			if (chunk > 0) {
				if (chunk > 512) chunk = 512;
				my_memcpy((char *)wbuf, (char *)&data[off], chunk);
			}
			if (hd_write_sector(sec + csec, wbuf) != 0) return -1;
		}

		bytes_left -= to_write;
		prev_cluster = cluster;
	}

	/* 更新目录项 (需重读扇区, 因 fat_alloc_cluster/fat_set_entry 破坏了 sec_buf) */
	if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
	my_memcpy(de->name, name83, 8);
	my_memcpy(de->ext, name83 + 8, 3);
	de->attr = FAT_ATTR_ARCHIVE;
	for (j = 0; j < 10; j++) de->reserved[j] = 0;
	/* FAT32: 首簇高 16 位写入 reserved[8..9] */
	if (fat_info.fat_type == 32) {
		de->reserved[8] = (t_8)((first_cluster >> 16) & 0xFF);
		de->reserved[9] = (t_8)((first_cluster >> 24) & 0xFF);
	}
	de->time = 0; de->date = 0;
	de->start_cluster = (t_16)(first_cluster & 0xFFFF);
	de->file_size = size;
	if (hd_write_sector(found_sec, sec_buf) != 0) return -1;

	return 0;
}


/*======================================================================
  追加数据到文件末尾 (不覆盖原有内容)
  name: 文件名 (如 "log.txt")
  buf:  要追加的数据
  size: 追加字节数
  返回: 0=成功, -1=失败

  算法:
    1. 文件不存在或为空: 等价于 fat_write_file (创建新文件)
    2. 文件存在且有内容:
       a. 遍历簇链找到最后一个簇
       b. 填满最后一个簇的剩余空间 (如果有)
       c. 为剩余数据分配新簇, 续接簇链
       d. 更新目录项 file_size = old_size + size
======================================================================*/
PUBLIC int fat_append_file(const char *name, void *buf, int size)
{
	char name83[11];
	int found = 0;
	t_32 found_sec = 0;
	int found_entry = -1;
	t_32 old_cluster = 0;
	t_32 old_size = 0;
	FAT_DIR_ENTRY *de;
	t_32 eof_min;
	int cluster_size;
	int ret;

	if (!fat_initialized) return -1;
	if (size <= 0) return 0;

	fat_format_name(name, name83);

	/* === 在根目录中搜索同名文件 === */
	ret = fat_find_dir_entry(name83, &found_sec, &found_entry);
	if (ret < 0) return -1;
	found = ret;

	if (found) {
		/* 重新读扇区, 获取目录项数据 */
		if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
		de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		old_cluster = fat_get_start_cluster(de);
		old_size = de->file_size;
	}

	/* 文件不存在或为空: 委托给 fat_write_file (创建/覆盖) */
	if (!found || old_size == 0 || old_cluster < 2) {
		return fat_write_file(name, buf, size);
	}

	/* === 文件存在且有内容: 追加到簇链末尾 === */
	cluster_size = fat_info.sectors_per_cluster * 512;
	eof_min = fat_eof_min();

	/* 遍历簇链找到最后一个簇 */
	{
		t_32 last_cluster = old_cluster;
		t_32 next;
		int safety = 0;

		while (1) {
			next = fat_get_entry(last_cluster);
			if (next >= eof_min || next < 2 || safety > 100000) break;
			last_cluster = next;
			safety++;
		}

		/* last_cluster 现在指向文件最后一个簇 */
		{
			t_8 *data = (t_8 *)buf;
			int bytes_left = size;
			int offset_in_last;   /* 最后一个簇中已使用的字节数 */
			int remain_in_last;   /* 最后一个簇中剩余可用字节 */

			offset_in_last = old_size % cluster_size;
			remain_in_last = cluster_size - offset_in_last;

			/* 步骤 1: 填满最后一个簇的剩余空间 */
			if (remain_in_last > 0 && bytes_left > 0) {
				int to_fill = (bytes_left < remain_in_last) ? bytes_left : remain_in_last;
				t_32 last_sec = fat_info.partition_start + fat_info.data_start +
				                (last_cluster - 2) * fat_info.sectors_per_cluster;
				int sec_idx = offset_in_last / 512;
				int off_in_sec = offset_in_last % 512;
				int data_off = 0;

				while (to_fill > 0) {
					int chunk = 512 - off_in_sec;
					if (chunk > to_fill) chunk = to_fill;

					if (hd_read_sector(last_sec + sec_idx, sec_buf) != 0) return -1;
					my_memcpy((char *)&sec_buf[off_in_sec],
					          (char *)&data[data_off], chunk);
					if (hd_write_sector(last_sec + sec_idx, sec_buf) != 0) return -1;

					data_off += chunk;
					bytes_left -= chunk;
					to_fill -= chunk;
					sec_idx++;
					off_in_sec = 0;
				}
			}

			/* 步骤 2: 为剩余数据分配新簇 */
			if (bytes_left > 0) {
				t_32 prev_cluster = last_cluster;
				int data_off = size - bytes_left;

				while (bytes_left > 0) {
					t_32 new_cluster = fat_alloc_cluster();
					t_32 new_sec;
					int csec, to_write;

					if (new_cluster == 0) return -1;  /* 磁盘满 */

					/* 续接簇链: 旧尾 → 新簇 */
					fat_set_entry(prev_cluster, new_cluster);
					prev_cluster = new_cluster;

					new_sec = fat_info.partition_start + fat_info.data_start +
					          (new_cluster - 2) * fat_info.sectors_per_cluster;

					to_write = bytes_left;
					if (to_write > cluster_size) to_write = cluster_size;

					/* 逐扇区写入 (不足一扇区的部分补零) */
					for (csec = 0; csec < fat_info.sectors_per_cluster; csec++) {
						t_8 wbuf[512];
						int off = data_off + csec * 512;
						int chunk = to_write - csec * 512;
						int k;

						for (k = 0; k < 512; k++) wbuf[k] = 0;
						if (chunk > 0) {
							if (chunk > 512) chunk = 512;
							my_memcpy((char *)wbuf, (char *)&data[off], chunk);
						}
						if (hd_write_sector(new_sec + csec, wbuf) != 0) return -1;
					}

					bytes_left -= to_write;
					data_off += to_write;
				}
			}
		}
	}

	/* === 更新目录项: file_size = old_size + size === */
	if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
	{
		de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		de->file_size = old_size + size;
	}
	if (hd_write_sector(found_sec, sec_buf) != 0) return -1;

	return 0;
}


/*======================================================================
  格式化硬盘为 FAT 文件系统
  total_sectors: 硬盘总扇区数
  fat_type: 0=自动选择, 12=FAT12, 16=FAT16, 32=FAT32
  返回: 0=成功, -1=失败

  FAT 类型选择逻辑:
    - 用户指定 fat_type=12/16/32 时, 强制使用指定类型 (若簇数合法)
    - fat_type=0 时, 依据簇数自动判定:
      < 4085 簇   -> FAT12
      < 65525 簇  -> FAT16
      >= 65525 簇 -> FAT32
    - 每簇扇区数依据微软规范, 按磁盘容量选择
======================================================================*/
PUBLIC int fat_format(t_32 total_sectors, int fat_type)
{
	t_8 bpb[512];
	t_8 mbr[512];
	t_16 rsvd_sec;
	t_8  num_fats = 2;
	t_16 root_ent_cnt = 0;
	t_8  sec_per_clus;
	t_32 fat_sz;
	t_32 data_sec;
	t_32 total_clusters;
	t_32 root_dir_sec;
	t_32 s;
	int fi;
	int is_fat32;
	int fat_bits;          /* 12, 16, 或 32 */
	t_32 root_cluster = 2;
	t_32 fsinfo_sec = 1;
	t_8  sec_buf_zero[512];
	t_32 part_start = PART_START;   /* 分区起始 LBA */
	t_32 part_size;                 /* 分区扇区数 = 磁盘总扇区 - part_start */

	part_size = total_sectors - part_start;

	/* ===== 第1步: 确定初始每簇扇区数 (从小开始, 后续按 FAT 类型调整) ===== */
	/* 初始值偏小, 倾向于产生更多簇; 第3步会根据目标 FAT 类型调整 */
	if (part_size < 32768)        sec_per_clus = 1;   /* < 16MB */
	else if (part_size < 65536)   sec_per_clus = 2;   /* 16~32MB */
	else if (part_size < 131072)  sec_per_clus = 4;   /* 32~64MB */
	else if (part_size < 262144)  sec_per_clus = 8;   /* 64~128MB */
	else if (part_size < 524288)  sec_per_clus = 1;   /* 128~256MB (FAT32 1扇区/簇) */
	else if (part_size < 1048576) sec_per_clus = 2;   /* 256~512MB */
	else if (part_size < 2097152) sec_per_clus = 4;   /* 512MB~1GB */
	else if (part_size < 4194304) sec_per_clus = 8;   /* 1~2GB */
	else                          sec_per_clus = 16;  /* >=2GB */

	/* ===== 第2步: 粗略估算簇数, 用于判定 FAT 类型 ===== */
	/* 先用假设的 FAT 大小估算 */
	{
		t_32 est_fat_sz = (part_size / sec_per_clus * 4 + 511) / 512 + 1;
		t_32 est_data = part_size - 32 - 2 * est_fat_sz;
		t_32 est_clusters = est_data / sec_per_clus;

		if (fat_type == 0) {
			/* 自动选择 */
			if (est_clusters < FAT12_MAX_CLUSTERS)      fat_bits = 12;
			else if (est_clusters < FAT16_MAX_CLUSTERS) fat_bits = 16;
			else                                        fat_bits = 32;
		} else {
			/* 用户指定 */
			fat_bits = fat_type;
		}
	}

	is_fat32 = (fat_bits == 32);

	/* ===== 第3步: 调整簇大小, 使簇数落在目标 FAT 类型的合法范围 ===== */
	/* FAT12: < 4085 簇; FAT16: 4085~65524 簇; FAT32: >= 65525 簇 */
	if (fat_bits == 12) {
		/* FAT12: 增大簇数不超过 4084, 可能需要增大 sec_per_clus */
		while (sec_per_clus < 128) {
			t_32 est_fat_sz = (part_size / sec_per_clus * 3 / 2 + 511) / 512 + 1;
			t_32 est_data = part_size - 1 - 2 * est_fat_sz - 32;
			t_32 est_clusters = est_data / sec_per_clus;
			if (est_clusters < FAT12_MAX_CLUSTERS) break;
			sec_per_clus *= 2;
		}
	} else if (fat_bits == 16) {
		/* FAT16: 簇数应在 4085~65524 之间 */
		/* 如果簇数太少 (< 4085), 增大簇; 如果太多 (>= 65525), 也增大簇 */
		while (sec_per_clus < 128) {
			t_32 est_fat_sz = (part_size / sec_per_clus * 2 + 511) / 512 + 1;
			t_32 est_data = part_size - 1 - 2 * est_fat_sz - 32;
			t_32 est_clusters = est_data / sec_per_clus;
			if (est_clusters >= FAT12_MAX_CLUSTERS && est_clusters < FAT16_MAX_CLUSTERS) break;
			if (est_clusters < FAT12_MAX_CLUSTERS) {
				/* 簇太少, 减小簇大小 */
				if (sec_per_clus > 1) sec_per_clus /= 2;
				else break;
			} else {
				/* 簇太多, 增大簇大小 */
				sec_per_clus *= 2;
			}
		}
	} else {
		/* FAT32: 簇数应 >= 65525, 通常磁盘够大就行 */
		/* 如果簇数 < 65525, 减小簇大小 */
		while (sec_per_clus > 1) {
			t_32 est_fat_sz = (part_size / sec_per_clus * 4 + 511) / 512 + 1;
			t_32 est_data = part_size - 32 - 2 * est_fat_sz;
			t_32 est_clusters = est_data / sec_per_clus;
			if (est_clusters >= FAT16_MAX_CLUSTERS) break;
			sec_per_clus /= 2;
		}
	}

	/* ===== 第4步: 设置 BPB 参数 ===== */
	/* 标准 FAT32 保留 32 扇区 (VBR + FSInfo + 备份VBR + 备份FSInfo + 保留) */
	/* loader 在 MBR 间隙 (扇区 2~33), 不占用 FAT 保留区 */
	rsvd_sec = is_fat32 ? 32 : 1;
	root_ent_cnt = is_fat32 ? 0 : 512;
	root_dir_sec = is_fat32 ? 0 : (root_ent_cnt * 32 + 511) / 512;

	/* ===== 第5步: 迭代计算 FAT 大小 ===== */
	fat_sz = 1;
	{
		int iter;
		for (iter = 0; iter < 20; iter++) {
			t_32 new_fat_sz;
			data_sec = part_size - rsvd_sec - num_fats * fat_sz - root_dir_sec;
			if (data_sec <= 0) return -1;
			total_clusters = data_sec / sec_per_clus;
			if (fat_bits == 12) {
				/* FAT12: 每项 1.5 字节 */
				new_fat_sz = (total_clusters * 3 + 1) / 2;
				new_fat_sz = (new_fat_sz + 511) / 512;
			} else if (fat_bits == 16) {
				new_fat_sz = (total_clusters * 2 + 511) / 512;
			} else {
				new_fat_sz = (total_clusters * 4 + 511) / 512;
			}
			if (new_fat_sz < 1) new_fat_sz = 1;
			if (new_fat_sz == fat_sz) break;
			fat_sz = new_fat_sz;
		}
		/* 最终簇数 */
		data_sec = part_size - rsvd_sec - num_fats * fat_sz - root_dir_sec;
		total_clusters = data_sec / sec_per_clus;
	}

	/* ===== 第6步: 最终校验 FAT 类型是否合法 ===== */
	if (fat_bits == 12 && total_clusters >= FAT12_MAX_CLUSTERS) return -1;
	if (fat_bits == 16 && (total_clusters < FAT12_MAX_CLUSTERS || total_clusters >= FAT16_MAX_CLUSTERS)) {
		/* 簇数不在 FAT16 范围, 但仍允许格式化 (某些情况下用户可能强制) */
	}
	if (fat_bits == 32 && total_clusters < FAT16_MAX_CLUSTERS) return -1;


	/* 构造 BPB (清零后填写) */
	for (s = 0; s < 512; s++) bpb[s] = 0;

	/* 跳转指令 */
	bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;

	/* OEM "CatOS   " */
	bpb[3] = 'C'; bpb[4] = 'a'; bpb[5] = 't'; bpb[6] = 'O';
	bpb[7] = 'S'; bpb[8] = ' '; bpb[9] = ' '; bpb[10] = ' ';

	/* BPB */
	bpb[11] = 0x00; bpb[12] = 0x02;   /* bytes_per_sector = 512 */
	bpb[13] = sec_per_clus;
	bpb[14] = (t_8)(rsvd_sec & 0xFF); bpb[15] = (t_8)(rsvd_sec >> 8);
	bpb[16] = num_fats;
	bpb[17] = (t_8)(root_ent_cnt & 0xFF); bpb[18] = (t_8)(root_ent_cnt >> 8);
	{
		/* TotSec16: 分区扇区数 (<=65535 时使用, 否则 0 用 TotSec32) */
		t_16 total16 = (part_size <= 65535) ? (t_16)part_size : 0;
		bpb[19] = (t_8)(total16 & 0xFF); bpb[20] = (t_8)(total16 >> 8);
	}
	bpb[21] = 0xF8;   /* media = 硬盘 */
	if (!is_fat32) {
		/* FAT16: FATSz16 */
		bpb[22] = (t_8)(fat_sz & 0xFF); bpb[23] = (t_8)(fat_sz >> 8);
	} else {
		/* FAT32: FATSz16=0, 真实值在 FATSz32 */
		bpb[22] = 0; bpb[23] = 0;
	}
	bpb[24] = 63; bpb[25] = 0;        /* sectors_per_track */
	bpb[26] = 16; bpb[27] = 0;        /* num_heads */
	/* HiddSec (偏移 28-31): 分区起始 LBA (MBR 分区布局必需) */
	bpb[28] = (t_8)(part_start & 0xFF);
	bpb[29] = (t_8)((part_start >> 8) & 0xFF);
	bpb[30] = (t_8)((part_start >> 16) & 0xFF);
	bpb[31] = (t_8)((part_start >> 24) & 0xFF);
	/* TotSec32 (偏移 32-35): 分区扇区数 (>65535 时使用) */
	if (part_size > 65535) {
		bpb[32] = (t_8)(part_size & 0xFF);
		bpb[33] = (t_8)((part_size >> 8) & 0xFF);
		bpb[34] = (t_8)((part_size >> 16) & 0xFF);
		bpb[35] = (t_8)((part_size >> 24) & 0xFF);
	}

	if (!is_fat32) {
		/* FAT12/16 扩展 BPB */
		bpb[36] = 0x80;   /* drive_number */
		bpb[37] = 0;
		bpb[38] = 0x29;   /* boot_sig */
		bpb[39] = 0x01; bpb[40] = 0x02; bpb[41] = 0x03; bpb[42] = 0x04;
		/* 卷标 "NO NAME    " */
		bpb[43] = 'N'; bpb[44] = 'O'; bpb[45] = ' '; bpb[46] = 'N';
		bpb[47] = 'A'; bpb[48] = 'M'; bpb[49] = 'E'; bpb[50] = ' ';
		bpb[51] = ' '; bpb[52] = ' '; bpb[53] = ' ';
		/* 文件系统类型字符串 */
		if (fat_bits == 12) {
			/* "FAT12   " */
			bpb[54] = 'F'; bpb[55] = 'A'; bpb[56] = 'T'; bpb[57] = '1';
			bpb[58] = '2'; bpb[59] = ' '; bpb[60] = ' '; bpb[61] = ' ';
		} else {
			/* "FAT16   " */
			bpb[54] = 'F'; bpb[55] = 'A'; bpb[56] = 'T'; bpb[57] = '1';
			bpb[58] = '6'; bpb[59] = ' '; bpb[60] = ' '; bpb[61] = ' ';
		}
	} else {
		/* FAT32 扩展 BPB */
		/* FATSz32 (偏移 36) */
		bpb[36] = (t_8)(fat_sz & 0xFF);
		bpb[37] = (t_8)((fat_sz >> 8) & 0xFF);
		bpb[38] = (t_8)((fat_sz >> 16) & 0xFF);
		bpb[39] = (t_8)((fat_sz >> 24) & 0xFF);
		/* ExtFlags (偏移 40) = 0 */
		bpb[40] = 0; bpb[41] = 0;
		/* FSVersion (偏移 42) = 0 */
		bpb[42] = 0; bpb[43] = 0;
		/* RootCluster (偏移 44) = 2 */
		bpb[44] = (t_8)(root_cluster & 0xFF);
		bpb[45] = (t_8)((root_cluster >> 8) & 0xFF);
		bpb[46] = (t_8)((root_cluster >> 16) & 0xFF);
		bpb[47] = (t_8)((root_cluster >> 24) & 0xFF);
		/* FSInfo (偏移 48) = 1 */
		bpb[48] = (t_8)(fsinfo_sec & 0xFF);
		bpb[49] = (t_8)((fsinfo_sec >> 8) & 0xFF);
		/* BkBootSec (偏移 50) = 6 (备份引导扇区) */
		bpb[50] = 6; bpb[51] = 0;
		/* Reserved (偏移 52..63) = 0 */
		/* DrvNum (偏移 64) */
		bpb[64] = 0x80;
		bpb[65] = 0;
		bpb[66] = 0x29;  /* boot_sig */
		bpb[67] = 0x01; bpb[68] = 0x02; bpb[69] = 0x03; bpb[70] = 0x04;
		/* 卷标 "NO NAME    " (偏移 71) */
		bpb[71] = 'N'; bpb[72] = 'O'; bpb[73] = ' '; bpb[74] = 'N';
		bpb[75] = 'A'; bpb[76] = 'M'; bpb[77] = 'E'; bpb[78] = ' ';
		bpb[79] = ' '; bpb[80] = ' '; bpb[81] = ' ';
		/* "FAT32   " (偏移 82) */
		bpb[82] = 'F'; bpb[83] = 'A'; bpb[84] = 'T'; bpb[85] = '3';
		bpb[86] = '2'; bpb[87] = ' '; bpb[88] = ' '; bpb[89] = ' ';
	}

	/* 引导扇区标志 */
	bpb[510] = 0x55; bpb[511] = 0xAA;

	/* ===== 构造 MBR (主引导记录, 扇区 0) ===== */
	/* MBR 包含引导代码 + 分区表 (偏移 0x1BE) + 0xAA55 标志 */
	for (s = 0; s < 512; s++) mbr[s] = 0;
	/* 分区表条目 1 (偏移 0x1BE, 16 字节):
	 *   [0]   = 0x80 (活动分区/可引导)
	 *   [1-3] = 起始 CHS (用 0xFE/0xFF/0xFF 表示 LBA 模式)
	 *   [4]   = 分区类型 (FAT12=0x01, FAT16 LBA=0x0E, FAT32 LBA=0x0C)
	 *   [5-7] = 结束 CHS (0xFE/0xFF/0xFF)
	 *   [8-11]= 起始 LBA (part_start)
	 *   [12-15]= 扇区数 (part_size)
	 */
	{
		t_8 *pte = &mbr[0x1BE];
		pte[0] = 0x80;                          /* 引导标志 */
		pte[1] = 0xFE; pte[2] = 0xFF; pte[3] = 0xFF;  /* 起始 CHS (LBA 模式) */
		/* 分区类型: FAT12=0x01, FAT16 LBA=0x0E, FAT32 LBA=0x0C */
		if (fat_bits == 12)      pte[4] = 0x01;  /* FAT12 */
		else if (fat_bits == 16) pte[4] = 0x0E;  /* FAT16 LBA */
		else                     pte[4] = 0x0C;  /* FAT32 LBA */
		pte[5] = 0xFE; pte[6] = 0xFF; pte[7] = 0xFF;  /* 结束 CHS */
		pte[8]  = (t_8)(part_start & 0xFF);
		pte[9]  = (t_8)((part_start >> 8) & 0xFF);
		pte[10] = (t_8)((part_start >> 16) & 0xFF);
		pte[11] = (t_8)((part_start >> 24) & 0xFF);
		pte[12] = (t_8)(part_size & 0xFF);
		pte[13] = (t_8)((part_size >> 8) & 0xFF);
		pte[14] = (t_8)((part_size >> 16) & 0xFF);
		pte[15] = (t_8)((part_size >> 24) & 0xFF);
	}
	mbr[510] = 0x55; mbr[511] = 0xAA;
	/* 写入 MBR 到扇区 0 (引导代码由 installer.c 后续从 _mbr_bin 覆盖) */
	if (hd_write_sector(0, mbr) != 0) return -1;

	/* ===== 写入 VBR (分区引导扇区, 扇区 part_start) ===== */
	if (hd_write_sector(part_start, bpb) != 0) return -1;

	/* FAT32: 写 FSInfo 扇区 + 备份引导扇区 (全部相对 part_start) */
	if (is_fat32) {
		t_8 fsinfo[512];
		for (s = 0; s < 512; s++) fsinfo[s] = 0;
		/* FSI_LeadSig (偏移 0) = 0x41615252 */
		fsinfo[0] = 0x52; fsinfo[1] = 0x52; fsinfo[2] = 0x61; fsinfo[3] = 0x41;
		/* FSI_StrucSig (偏移 484) = 0x61417272 */
		fsinfo[484] = 0x72; fsinfo[485] = 0x72; fsinfo[486] = 0x41; fsinfo[487] = 0x61;
		/* FSI_Free_Count (偏移 488) = total_clusters - 1 (减去根目录占用的簇 2) */
		{
			t_32 fc = total_clusters - 1;
			fsinfo[488] = (t_8)(fc & 0xFF);
			fsinfo[489] = (t_8)((fc >> 8) & 0xFF);
			fsinfo[490] = (t_8)((fc >> 16) & 0xFF);
			fsinfo[491] = (t_8)((fc >> 24) & 0xFF);
		}
		/* FSI_Nxt_Free (偏移 492) = 3 (根目录占用簇 2) */
		fsinfo[492] = 3; fsinfo[493] = 0; fsinfo[494] = 0; fsinfo[495] = 0;
		/* FSI_TrailSig (偏移 508) = 0xAA550000 */
		fsinfo[508] = 0x00; fsinfo[509] = 0x00; fsinfo[510] = 0x55; fsinfo[511] = 0xAA;

		/* FSInfo 在 part_start + fsinfo_sec */
		if (hd_write_sector(part_start + fsinfo_sec, fsinfo) != 0) return -1;
		/* 备份 VBR (part_start + 6) */
		if (hd_write_sector(part_start + 6, bpb) != 0) return -1;
		/* 备份 FSInfo (part_start + 7) */
		if (hd_write_sector(part_start + 7, fsinfo) != 0) return -1;
	}

	/* 清零 sec_buf_zero 用于写空扇区 */
	for (s = 0; s < 512; s++) sec_buf_zero[s] = 0;

	/* 初始化 FAT 表: 前两项为介质标识和 EOF 标记 */
	for (fi = 0; fi < num_fats; fi++) {
		t_32 fat_start_sec = part_start + rsvd_sec + fi * fat_sz;
		t_8 fat_init_buf[512];

		for (s = 0; s < 512; s++) fat_init_buf[s] = 0;

		if (fat_bits == 12) {
			/* FAT12: FAT[0]=0xFF8, FAT[1]=0xFFF (3 字节) */
			fat_init_buf[0] = 0xF8; fat_init_buf[1] = 0xFF; fat_init_buf[2] = 0xFF;
		} else if (fat_bits == 16) {
			/* FAT16: FAT[0]=0xFFF8, FAT[1]=0xFFFF */
			fat_init_buf[0] = 0xF8; fat_init_buf[1] = 0xFF;
			fat_init_buf[2] = 0xFF; fat_init_buf[3] = 0xFF;
		} else {
			/* FAT32: FAT[0]=0x0FFFFFF8, FAT[1]=0x0FFFFFFF, FAT[2]=0x0FFFFFFF (根目录占簇2) */
			fat_init_buf[0] = 0xF8; fat_init_buf[1] = 0xFF; fat_init_buf[2] = 0xFF; fat_init_buf[3] = 0x0F;
			fat_init_buf[4] = 0xFF; fat_init_buf[5] = 0xFF; fat_init_buf[6] = 0xFF; fat_init_buf[7] = 0x0F;
			fat_init_buf[8] = 0xFF; fat_init_buf[9] = 0xFF; fat_init_buf[10] = 0xFF; fat_init_buf[11] = 0x0F;
		}
		if (hd_write_sector(fat_start_sec, fat_init_buf) != 0) return -1;

		/* 清零 FAT 其余扇区 */
		for (s = 1; s < fat_sz; s++) {
			if (hd_write_sector(fat_start_sec + s, sec_buf_zero) != 0) return -1;
		}
	}

	/* 清零根目录区 (FAT16) 或根目录首簇 (FAT32) — 全部加 part_start 偏移 */
	if (!is_fat32) {
		t_32 root_start = part_start + rsvd_sec + num_fats * fat_sz;
		for (s = 0; s < root_dir_sec; s++) {
			if (hd_write_sector(root_start + s, sec_buf_zero) != 0) return -1;
		}
	} else {
		/* FAT32: 清零 root_cluster 对应的所有扇区 */
		t_32 root_sec = part_start + rsvd_sec + num_fats * fat_sz + (root_cluster - 2) * sec_per_clus;
		for (s = 0; s < sec_per_clus; s++) {
			if (hd_write_sector(root_sec + s, sec_buf_zero) != 0) return -1;
		}
	}

	/* 重新初始化 FAT 信息 */
	fat_initialized = 0;
	return fat_init();
}


/*======================================================================
  === 子目录支持 (cd / mkdir) ===
  以下函数接受 dir_cluster 参数:
    0  = 根目录
    >=2 = 子目录首簇
======================================================================*/

/*----------------------------------------------------------------------
  在指定目录中搜索同名目录项 (支持子目录)
  返回: 1=找到, 0=未找到, -1=读盘错误
----------------------------------------------------------------------*/
PRIVATE int fat_find_dir_entry_in(t_32 dir_cluster, const char *name83,
                                  t_32 *out_sec, int *out_entry)
{
	/* dir_cluster==0 且非 FAT32: 固定根目录 (复用原逻辑) */
	if (dir_cluster == 0 && fat_info.fat_type != 32) {
		return fat_find_dir_entry(name83, out_sec, out_entry);
	}

	/* FAT32 根目录或子目录: 按簇链遍历 */
	{
		t_32 start_cluster = (dir_cluster == 0) ? fat_info.root_cluster : dir_cluster;
		t_32 cur = start_cluster;
		t_32 eof_min = fat_eof_min();
		int safety = 0;
		int done = 0;

		while (!done && cur >= 2 && cur < eof_min && safety < 10000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cur - 2) * fat_info.sectors_per_cluster;
			int nsec = fat_info.sectors_per_cluster;
			int csec;
			if (nsec > DIR_CLUSTER_MAX_SECS) nsec = DIR_CLUSTER_MAX_SECS;

			for (csec = 0; csec < nsec && !done; csec++) {
				int rd_ret = hd_read_sector(sec + csec, &dir_buf[csec * 512]);
				int entry;
				if (rd_ret != 0) {
					fat_err_sec = sec + csec;
					fat_err_code = rd_ret;
					return -1;
				}
				for (entry = 0; entry < 16; entry++) {
					FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&dir_buf[csec * 512 + entry * 32];
					if ((t_8)de->name[0] == 0x00) { done = 1; break; }
					if ((t_8)de->name[0] == 0xE5) continue;
					if (de->attr == FAT_ATTR_LFN) continue;
					if (de->attr & FAT_ATTR_VOLUME_ID) continue;
					if (fat_name_matches(de, name83)) {
						*out_sec = sec + csec;
						*out_entry = entry;
						return 1;
					}
				}
			}
			if (!done) cur = fat_get_entry(cur);
			safety++;
		}
		return 0;
	}
}

/*----------------------------------------------------------------------
  在指定目录中搜索空闲目录项位置 (支持子目录)
  返回: 1=找到, 0=未找到(目录满), -1=读盘错误
----------------------------------------------------------------------*/
PRIVATE int fat_find_free_entry_in(t_32 dir_cluster, t_32 *out_sec, int *out_entry)
{
	/* dir_cluster==0 且非 FAT32: 固定根目录 (复用原逻辑) */
	if (dir_cluster == 0 && fat_info.fat_type != 32) {
		return fat_find_free_entry(out_sec, out_entry);
	}

	/* FAT32 根目录或子目录: 按簇链遍历 */
	{
		t_32 start_cluster = (dir_cluster == 0) ? fat_info.root_cluster : dir_cluster;
		t_32 cur = start_cluster;
		t_32 eof_min = fat_eof_min();
		int safety = 0;

		while (cur >= 2 && cur < eof_min && safety < 10000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cur - 2) * fat_info.sectors_per_cluster;
			int nsec = fat_info.sectors_per_cluster;
			int csec;
			if (nsec > DIR_CLUSTER_MAX_SECS) nsec = DIR_CLUSTER_MAX_SECS;

			for (csec = 0; csec < nsec; csec++) {
				int rd_ret = hd_read_sector(sec + csec, &dir_buf[csec * 512]);
				int entry;
				if (rd_ret != 0) {
					fat_err_sec = sec + csec;
					fat_err_code = rd_ret;
					return -1;
				}
				for (entry = 0; entry < 16; entry++) {
					FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&dir_buf[csec * 512 + entry * 32];
					if ((t_8)de->name[0] == 0x00 || (t_8)de->name[0] == 0xE5) {
						*out_sec = sec + csec;
						*out_entry = entry;
						return 1;
					}
				}
			}
			cur = fat_get_entry(cur);
			safety++;
		}
		return 0;
	}
}

/*----------------------------------------------------------------------
  在指定目录中创建空文件
  返回: 0=成功, 1=已存在, 负数=失败
----------------------------------------------------------------------*/
PUBLIC int fat_touch_in(t_32 dir_cluster, const char *name)
{
	char name83[11];
	t_32 free_sec = 0;
	int free_entry = 0;
	FAT_DIR_ENTRY *de;
	int j, wr_ret, ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	ret = fat_find_dir_entry_in(dir_cluster, name83, &free_sec, &free_entry);
	if (ret < 0) return -2;
	if (ret == 1) return 1;

	ret = fat_find_free_entry_in(dir_cluster, &free_sec, &free_entry);
	if (ret < 0) return -2;
	if (ret == 0) return -3;

	if (hd_read_sector(free_sec, sec_buf) != 0) return -4;

	de = (FAT_DIR_ENTRY *)&sec_buf[free_entry * 32];
	my_memcpy(de->name, name83, 8);
	my_memcpy(de->ext, name83 + 8, 3);
	de->attr = FAT_ATTR_ARCHIVE;
	for (j = 0; j < 10; j++) de->reserved[j] = 0;
	de->time = 0; de->date = 0;
	de->start_cluster = 0;
	de->file_size = 0;

	wr_ret = hd_write_sector(free_sec, sec_buf);
	if (wr_ret != 0) {
		fat_err_sec = free_sec;
		fat_err_code = wr_ret;
		return -10 + wr_ret;
	}
	return 0;
}

/*----------------------------------------------------------------------
  在指定目录中删除文件
  返回: 0=成功, 1=不存在, 负数=失败
----------------------------------------------------------------------*/
PUBLIC int fat_delete_in(t_32 dir_cluster, const char *name)
{
	char name83[11];
	FAT_DIR_ENTRY *de;
	int wr_ret, ret;
	t_32 found_sec;
	int found_entry;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	ret = fat_find_dir_entry_in(dir_cluster, name83, &found_sec, &found_entry);
	if (ret < 0) return -2;
	if (ret == 0) return 1;

	if (hd_read_sector(found_sec, sec_buf) != 0) return -3;
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];

	/* 跳过子目录 */
	if (de->attr & FAT_ATTR_DIRECTORY) return 1;

	/* 释放簇链 */
	{
		t_32 eof_min = fat_eof_min();
		t_32 sc = fat_get_start_cluster(de);
		if (sc >= 2 && sc < eof_min) {
			fat_free_chain(sc);
		}
	}

	/* 重新读入目录扇区 */
	if (hd_read_sector(found_sec, sec_buf) != 0) return -3;
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];

	de->name[0] = 0xE5;
	wr_ret = hd_write_sector(found_sec, sec_buf);
	if (wr_ret != 0) {
		fat_err_sec = found_sec;
		fat_err_code = wr_ret;
		return -4;
	}
	return 0;
}

/*----------------------------------------------------------------------
  从指定目录读取文件内容
  返回: 实际读取字节数, -1=未找到或失败
----------------------------------------------------------------------*/
PUBLIC int fat_read_file_in(t_32 dir_cluster, const char *name, void *buf, int bufsize)
{
	FAT_DIR_ENTRY *de;
	char name83[11];
	t_32 cluster, file_size;
	int bytes_read;
	t_8 *out = (t_8 *)buf;
	t_32 found_sec, found_cluster, found_size;
	int found_entry, ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	ret = fat_find_dir_entry_in(dir_cluster, name83, &found_sec, &found_entry);
	if (ret <= 0) return -1;

	if (hd_read_sector(found_sec, sec_buf) != 0) return -1;
	de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
	found_cluster = fat_get_start_cluster(de);
	found_size = de->file_size;

	cluster = found_cluster;
	file_size = found_size;
	if (file_size == 0) return 0;
	bytes_read = 0;

	{
		t_32 eof_min = fat_eof_min();
		int safety = 0;
		while (cluster >= 2 && cluster < eof_min && safety < 100000) {
			t_32 sec = fat_info.partition_start + fat_info.data_start +
			           (cluster - 2) * fat_info.sectors_per_cluster;
			int csec, to_copy;

			for (csec = 0; csec < fat_info.sectors_per_cluster; csec++) {
				if (hd_read_sector(sec + csec, sec_buf) != 0)
					return bytes_read;
				to_copy = file_size - bytes_read;
				if (to_copy > 512) to_copy = 512;
				if (to_copy > bufsize - bytes_read) to_copy = bufsize - bytes_read;
				if (to_copy <= 0) return bytes_read;
				my_memcpy((char *)&out[bytes_read], (char *)sec_buf, to_copy);
				bytes_read += to_copy;
				if (bytes_read >= (int)file_size || bytes_read >= bufsize)
					return bytes_read;
			}
			cluster = fat_get_entry(cluster);
			safety++;
		}
	}
	return bytes_read;
}

/*----------------------------------------------------------------------
  查找子目录: 在指定目录中按名查找子目录, 返回首簇号
  返回: >=2=首簇号, 0=未找到或非目录, -1=读盘错误
----------------------------------------------------------------------*/
PUBLIC t_32 fat_find_subdir(t_32 dir_cluster, const char *name)
{
	char name83[11];
	t_32 found_sec;
	int found_entry, ret;

	if (!fat_initialized) return 0;

	fat_format_name(name, name83);

	ret = fat_find_dir_entry_in(dir_cluster, name83, &found_sec, &found_entry);
	if (ret <= 0) return (t_32)0;

	if (hd_read_sector(found_sec, sec_buf) != 0) return (t_32)(-1);
	{
		FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&sec_buf[found_entry * 32];
		if (!(de->attr & FAT_ATTR_DIRECTORY)) return 0;  /* 不是目录 */
		return fat_get_start_cluster(de);
	}
}

/*----------------------------------------------------------------------
  创建子目录: 在 parent_cluster 指定目录中创建名为 name 的子目录
  parent_cluster=0 表示根目录
  返回: 0=成功, 1=已存在, 负数=失败
----------------------------------------------------------------------*/
PUBLIC int fat_mkdir_in(t_32 parent_cluster, const char *name)
{
	char name83[11];
	t_32 free_sec = 0;
	int free_entry = 0;
	t_32 new_cluster;
	t_32 new_sec;
	t_32 parent_for_dotdot;
	t_8 init_buf[512];
	int j, s, wr_ret, ret;

	if (!fat_initialized) return -1;

	fat_format_name(name, name83);

	/* 检查是否已存在同名条目 */
	ret = fat_find_dir_entry_in(parent_cluster, name83, &free_sec, &free_entry);
	if (ret < 0) return -2;
	if (ret == 1) return 1;  /* 已存在 */

	/* 查找空闲目录项位置 */
	ret = fat_find_free_entry_in(parent_cluster, &free_sec, &free_entry);
	if (ret < 0) return -2;
	if (ret == 0) return -3;  /* 目录满 */

	/* 分配新簇 (注意: fat_alloc_cluster 会破坏 sec_buf) */
	new_cluster = fat_alloc_cluster();
	if (new_cluster == 0) return -5;  /* 无空闲簇 */

	/* 计算新簇的起始扇区 */
	new_sec = fat_info.partition_start + fat_info.data_start +
	          (new_cluster - 2) * fat_info.sectors_per_cluster;

	/* parent_cluster 用于 ".." 项: 根目录时按 FAT 规范写 0 */
	parent_for_dotdot = (parent_cluster == 0) ? 0 : parent_cluster;

	/* 初始化新簇: 写入 "." 和 ".." 两个目录项, 其余清零 */
	for (j = 0; j < 512; j++) init_buf[j] = 0;

	/* 第 0 项: "." (指向自身) */
	{
		FAT_DIR_ENTRY *dot = (FAT_DIR_ENTRY *)&init_buf[0];
		dot->name[0] = '.';  for (j = 1; j < 8; j++) dot->name[j] = ' ';
		dot->ext[0] = ' '; dot->ext[1] = ' '; dot->ext[2] = ' ';
		dot->attr = FAT_ATTR_DIRECTORY;
		for (j = 0; j < 10; j++) dot->reserved[j] = 0;
		dot->time = 0; dot->date = 0;
		dot->start_cluster = (t_16)(new_cluster & 0xFFFF);
		if (fat_info.fat_type == 32) {
			dot->reserved[8] = (t_8)((new_cluster >> 16) & 0xFF);
			dot->reserved[9] = (t_8)((new_cluster >> 24) & 0xFF);
		}
		dot->file_size = 0;
	}

	/* 第 1 项: ".." (指向父目录) */
	{
		FAT_DIR_ENTRY *ddot = (FAT_DIR_ENTRY *)&init_buf[32];
		ddot->name[0] = '.'; ddot->name[1] = '.';
		for (j = 2; j < 8; j++) ddot->name[j] = ' ';
		ddot->ext[0] = ' '; ddot->ext[1] = ' '; ddot->ext[2] = ' ';
		ddot->attr = FAT_ATTR_DIRECTORY;
		for (j = 0; j < 10; j++) ddot->reserved[j] = 0;
		ddot->time = 0; ddot->date = 0;
		ddot->start_cluster = (t_16)(parent_for_dotdot & 0xFFFF);
		if (fat_info.fat_type == 32) {
			ddot->reserved[8] = (t_8)((parent_for_dotdot >> 16) & 0xFF);
			ddot->reserved[9] = (t_8)((parent_for_dotdot >> 24) & 0xFF);
		}
		ddot->file_size = 0;
	}

	/* 第 2 项: name[0]=0x00 表示目录结束 */
	init_buf[64] = 0x00;

	/* 写入新簇的所有扇区 (第一扇区含 "." "..", 其余清零) */
	wr_ret = hd_write_sector(new_sec, init_buf);
	if (wr_ret != 0) {
		fat_err_sec = new_sec;
		fat_err_code = wr_ret;
		return -10 + wr_ret;
	}
	/* 清零该簇其余扇区 */
	{
		t_8 zero_buf[512];
		for (j = 0; j < 512; j++) zero_buf[j] = 0;
		for (s = 1; s < fat_info.sectors_per_cluster && s < DIR_CLUSTER_MAX_SECS; s++) {
			hd_write_sector(new_sec + s, zero_buf);
		}
	}

	/* 在父目录中写入新目录项 (需重读扇区, 因 fat_alloc_cluster 破坏了 sec_buf) */
	if (hd_read_sector(free_sec, sec_buf) != 0) return -4;
	{
		FAT_DIR_ENTRY *de = (FAT_DIR_ENTRY *)&sec_buf[free_entry * 32];
		my_memcpy(de->name, name83, 8);
		my_memcpy(de->ext, name83 + 8, 3);
		de->attr = FAT_ATTR_DIRECTORY;
		for (j = 0; j < 10; j++) de->reserved[j] = 0;
		de->time = 0; de->date = 0;
		de->start_cluster = (t_16)(new_cluster & 0xFFFF);
		if (fat_info.fat_type == 32) {
			de->reserved[8] = (t_8)((new_cluster >> 16) & 0xFF);
			de->reserved[9] = (t_8)((new_cluster >> 24) & 0xFF);
		}
		de->file_size = 0;
	}
	wr_ret = hd_write_sector(free_sec, sec_buf);
	if (wr_ret != 0) {
		fat_err_sec = free_sec;
		fat_err_code = wr_ret;
		return -10 + wr_ret;
	}
	return 0;
}
