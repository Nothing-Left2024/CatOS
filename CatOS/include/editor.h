/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               editor.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS 文本编辑器 — 函数声明
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _CATOS_EDITOR_H_
#define _CATOS_EDITOR_H_

#include "type.h"

PUBLIC void editor_run(int *p_cursor, const char *filename);

#endif /* _CATOS_EDITOR_H_ */
