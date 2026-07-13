
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               taskmgr.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS Task Manager Application
  第一个用户应用程序: 交互式任务管理器
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _CATOS_TASKMGR_H_
#define _CATOS_TASKMGR_H_


/* 任务管理器入口函数
 * 由 shell 的 "taskmgr" 命令调用, 接管键盘与显示
 * 参数: p_cursor — 指向当前光标位置(字节偏移)的指针
 * 返回: 无 (函数内部循环, 直到用户按下 Ctrl-C 才返回)
 * 退出时清屏并将 *p_cursor 置 0, 供 shell 重新打印提示符 */
PUBLIC void taskmgr_run(int *p_cursor);


#endif /* _CATOS_TASKMGR_H_ */
