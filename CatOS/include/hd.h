/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               hd.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS IDE/ATA 硬盘驱动 — 常量与函数声明
  使用 PIO 模式轮询读写, 支持主通道主盘 (Primary Master)
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _CATOS_HD_H_
#define _CATOS_HD_H_

#include "type.h"

/* IDE Primary Channel 端口 */
#define HD_PORT_DATA       0x1F0
#define HD_PORT_ERROR      0x1F1
#define HD_PORT_FEATURES   0x1F1
#define HD_PORT_SECT_CNT   0x1F2
#define HD_PORT_LBA_LOW    0x1F3
#define HD_PORT_LBA_MID    0x1F4
#define HD_PORT_LBA_HIGH   0x1F5
#define HD_PORT_DRIVE      0x1F6
#define HD_PORT_STATUS     0x1F7
#define HD_PORT_COMMAND    0x1F7

/* ATA 状态位 */
#define HD_STAT_BSY    0x80   /* 忙碌 */
#define HD_STAT_DRDY   0x40   /* 就绪 */
#define HD_STAT_DRQ    0x08   /* 数据请求 */
#define HD_STAT_ERR    0x01   /* 错误 */

/* ATA 命令 */
#define HD_CMD_IDENTIFY  0xEC   /* 识别设备 */
#define HD_CMD_READ      0x20   /* 读扇区 (LBA28) */
#define HD_CMD_WRITE     0x30   /* 写扇区 (LBA28) */

/* 硬盘识别信息 */
typedef struct {
	char	model[41];		/* 型号名称 (40字符 + '\0') */
	t_32	lba_sectors;		/* LBA28 总扇区数 */
	t_16	cylinders;		/* 柱面数 */
	t_16	heads;			/* 磁头数 */
	t_16	sectors_per_track;	/* 每磁道扇区数 */
	t_32	capacity_kb;		/* 容量 (KB) */
} HD_INFO;

/* 函数声明 */
PUBLIC void hd_init(void);
PUBLIC int  hd_identify(HD_INFO *info);
PUBLIC int  hd_read_sector(t_32 lba, void *buf);
PUBLIC int  hd_read_sectors(t_32 lba, int count, void *buf);
PUBLIC int  hd_write_sector(t_32 lba, void *buf);
PUBLIC int  hd_write_sectors(t_32 lba, int count, void *buf);

#endif /* _CATOS_HD_H_ */
