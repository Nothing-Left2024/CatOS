
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               mouse.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS PS/2 Mouse Driver
======================================================================*/

#ifndef _CATOS_MOUSE_H_
#define _CATOS_MOUSE_H_

/* 8042 控制器端口 (与键盘共用) */
#define MOUSE_PORT_DATA   0x60
#define MOUSE_PORT_STATUS 0x64
#define MOUSE_PORT_CMD    0x64

/* 屏幕尺寸 (文本模式 80x25) */
#define MOUSE_SCR_W  80
#define MOUSE_SCR_H  25

#endif /* _CATOS_MOUSE_H_ */
