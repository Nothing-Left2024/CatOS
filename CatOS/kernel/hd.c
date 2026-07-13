/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               hd.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS IDE/ATA 硬盘驱动 — PIO 模式轮询读写
  支持主通道主盘 (Primary Master, 端口 0x1F0-0x1F7)
  使用 LBA28 寻址, 最大支持 128GB 硬盘

  注意: 进程运行在 ring 1, IOPL=1, in/out 指令可用
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "hd.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "proto.h"


/*======================================================================
  等待硬盘就绪 (BSY=0 且 DRDY=1)
  返回: 0=成功, -1=超时
======================================================================*/
static int hd_wait_ready(int timeout)
{
	int i;
	t_8 status;
	for (i = 0; i < timeout; i++) {
		status = in_byte(HD_PORT_STATUS);
		if (!(status & HD_STAT_BSY) && (status & HD_STAT_DRDY))
			return 0;
	}
	return -1;
}

/*======================================================================
  仅等待 BSY 清零 (写入完成后使用, 不要求 DRDY=1)
  某些 IDE 控制器 (如 VMware) 写入后不立即置 DRDY, 但 BSY 会清零
  返回: 0=成功, -1=超时
======================================================================*/
static int hd_wait_bsy_clear(int timeout)
{
	int i;
	t_8 status;
	for (i = 0; i < timeout; i++) {
		status = in_byte(HD_PORT_STATUS);
		if (!(status & HD_STAT_BSY))
			return 0;
	}
	return -1;
}

/*======================================================================
  等待数据请求 (BSY=0 且 DRQ=1)
  返回: 0=成功, -1=超时或错误
======================================================================*/
static int hd_wait_drq(int timeout)
{
	int i;
	t_8 status;
	for (i = 0; i < timeout; i++) {
		status = in_byte(HD_PORT_STATUS);
		if (status & HD_STAT_ERR) return -1;
		if (!(status & HD_STAT_BSY) && (status & HD_STAT_DRQ))
			return 0;
	}
	return -1;
}

/*======================================================================
  从数据端口读一个扇区 (256 个 16 位 word = 512 bytes)
  使用 inw 指令逐字读取
======================================================================*/
static void hd_read_data(void *buf)
{
	t_16 *p = (t_16 *)buf;
	int i;
	for (i = 0; i < 256; i++) {
		t_16 val;
		__asm__ __volatile__(
			"inw %%dx, %%ax"
			: "=a"(val)
			: "d"(HD_PORT_DATA)
		);
		p[i] = val;
	}
}

/*======================================================================
  硬盘驱动初始化
  执行 IDE 软复位清除 BIOS 留下的状态, 然后选择主盘
  复位后只等 BSY 清零 (不要求 DRDY, 某些控制器复位后不立即置 DRDY)
======================================================================*/
PUBLIC void hd_init(void)
{
	/* IDE 软复位: SRST=1, nIEN=1 */
	out_byte(0x3F6, 0x06);

	/* 保持 SRST 至少 5us (用循环延迟) */
	{
		volatile int d;
		for (d = 0; d < 100; d++) {}
	}

	/* 清除 SRST, 保留 nIEN=1 */
	out_byte(0x3F6, 0x02);

	/* 复位后等待 BSY 清零 (不要求 DRDY) */
	hd_wait_bsy_clear(500000);

	/* 选择主盘, LBA 模式 */
	out_byte(HD_PORT_DRIVE, 0xE0);  /* LBA=1, DRV=0 (Master) */

	/* 等待驱动器就绪 (此时 DRDY 应该已置位) */
	hd_wait_ready(100000);
}

/*======================================================================
  识别硬盘设备 (ATA IDENTIFY DEVICE)
  读取 512 字节设备信息, 解析型号/容量/CHS 参数
  返回: 0=成功, -1=失败 (无硬盘或超时)
======================================================================*/
PUBLIC int hd_identify(HD_INFO *info)
{
	t_16 buf[256];
	int i;
	t_8 status;

	/* 等待就绪 */
	if (hd_wait_ready(100000) != 0) return -1;

	/* 选择主盘 */
	out_byte(HD_PORT_DRIVE, 0xE0);

	/* 发送 IDENTIFY 命令 */
	out_byte(HD_PORT_COMMAND, HD_CMD_IDENTIFY);

	/* 等待数据就绪 */
	if (hd_wait_drq(100000) != 0) return -1;

	/* 检查错误 */
	status = in_byte(HD_PORT_STATUS);
	if (status & HD_STAT_ERR) return -1;

	/* 读取 256 个 word */
	hd_read_data(buf);

	/* 解析型号 (word 27-46, 每个 word 高字节在前) */
	for (i = 0; i < 20; i++) {
		info->model[i * 2]     = (char)(buf[27 + i] >> 8);
		info->model[i * 2 + 1] = (char)(buf[27 + i] & 0xFF);
	}
	info->model[40] = '\0';

	/* LBA28 扇区总数 (word 60-61) */
	info->lba_sectors = (t_32)buf[60] | ((t_32)buf[61] << 16);

	/* CHS 参数 (word 1=柱面, 3=磁头, 6=扇区/磁道) */
	info->cylinders = buf[1];
	info->heads = buf[3];
	info->sectors_per_track = buf[6];

	/* 容量 (KB) */
	info->capacity_kb = info->lba_sectors / 2;

	return 0;
}

/*======================================================================
  读一个扇区 (LBA28 模式)
  lba: 逻辑块地址 (0-based)
  buf: 512 字节缓冲区
  返回: 0=成功, 负数=失败
    -1 = wait_ready 失败
    -2 = wait_drq 失败
    -3 = 错误位置位
======================================================================*/
PUBLIC int hd_read_sector(t_32 lba, void *buf)
{
	t_8 status;

	/* 等待就绪 */
	if (hd_wait_ready(100000) != 0) return -1;

	/* 设置 LBA28 参数 */
	out_byte(HD_PORT_SECT_CNT, 1);                        /* 1 个扇区 */
	out_byte(HD_PORT_LBA_LOW,  (t_8)(lba & 0xFF));        /* LBA [0:7] */
	out_byte(HD_PORT_LBA_MID,  (t_8)((lba >> 8) & 0xFF)); /* LBA [8:15] */
	out_byte(HD_PORT_LBA_HIGH, (t_8)((lba >> 16) & 0xFF));/* LBA [16:23] */
	out_byte(HD_PORT_DRIVE, 0xE0 | (t_8)((lba >> 24) & 0x0F)); /* LBA + Master */

	/* 发送 READ SECTORS 命令 */
	out_byte(HD_PORT_COMMAND, HD_CMD_READ);

	/* 等待数据就绪 */
	if (hd_wait_drq(100000) != 0) return -2;

	/* 读取数据 */
	hd_read_data(buf);

	/* 检查错误 */
	status = in_byte(HD_PORT_STATUS);
	if (status & HD_STAT_ERR) return -3;

	return 0;
}

/*======================================================================
  读多个扇区 (逐扇区读取)
  lba: 起始逻辑块地址
  count: 扇区数
  buf: 缓冲区 (至少 count*512 字节)
  返回: 0=成功, -1=失败
======================================================================*/
PUBLIC int hd_read_sectors(t_32 lba, int count, void *buf)
{
	int i;
	for (i = 0; i < count; i++) {
		if (hd_read_sector(lba + i, (t_8 *)buf + i * 512) != 0)
			return -1;
	}
	return 0;
}

/*======================================================================
  向数据端口写一个扇区 (256 个 16 位 word = 512 bytes)
======================================================================*/
static void hd_write_data(void *buf)
{
	t_16 *p = (t_16 *)buf;
	int i;
	for (i = 0; i < 256; i++) {
		__asm__ __volatile__(
			"outw %%ax, %%dx"
			: : "a"(p[i]), "d"(HD_PORT_DATA)
		);
	}
}

/*======================================================================
  写一个扇区 (LBA28 模式)
  lba: 逻辑块地址 (0-based)
  buf: 512 字节缓冲区
  返回: 0=成功, 负数=失败 (不同值区分原因)
    -1 = wait_ready 失败 (BSY 未清或 DRDY 未就绪)
    -2 = wait_drq 失败 (数据请求超时)
    -3 = wait_bsy_clear 失败 (写入未完成)
    -4 = 错误位置位 (ERR=1, 状态码在 HD_PORT_ERROR)
======================================================================*/
PUBLIC int hd_write_sector(t_32 lba, void *buf)
{
	t_8 status;

	/* 等待就绪 */
	if (hd_wait_ready(100000) != 0) return -1;

	/* 设置 LBA28 参数 */
	out_byte(HD_PORT_SECT_CNT, 1);
	out_byte(HD_PORT_LBA_LOW,  (t_8)(lba & 0xFF));
	out_byte(HD_PORT_LBA_MID,  (t_8)((lba >> 8) & 0xFF));
	out_byte(HD_PORT_LBA_HIGH, (t_8)((lba >> 16) & 0xFF));
	out_byte(HD_PORT_DRIVE, 0xE0 | (t_8)((lba >> 24) & 0x0F));

	/* 发送 WRITE SECTORS 命令 */
	out_byte(HD_PORT_COMMAND, HD_CMD_WRITE);

	/* 等待数据请求 */
	if (hd_wait_drq(100000) != 0) return -2;

	/* 写入数据 */
	hd_write_data(buf);

	/* 等待写入完成 (只需 BSY 清零, 不要求 DRDY) */
	if (hd_wait_bsy_clear(100000) != 0) return -3;

	/* 检查错误 */
	status = in_byte(HD_PORT_STATUS);
	if (status & HD_STAT_ERR) return -4;

	return 0;
}

/*======================================================================
  写多个扇区 (逐扇区写入)
  返回: 0=成功, 负数=第一个失败的扇区的错误码 (见 hd_write_sector)
======================================================================*/
PUBLIC int hd_write_sectors(t_32 lba, int count, void *buf)
{
	int i;
	int ret;
	for (i = 0; i < count; i++) {
		ret = hd_write_sector(lba + i, (t_8 *)buf + i * 512);
		if (ret != 0)
			return ret;
	}
	return 0;
}
