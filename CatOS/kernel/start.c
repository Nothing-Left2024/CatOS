/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            start.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    CatOS v1.0
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

/*======================================================================*
                            cstart
 *======================================================================*/
PUBLIC void cstart()
{
	/* 图形模式下跳过文本输出, 避免在图形显存中写入垃圾像素 */
	if (*((volatile t_8*)0x504) == 0) {
		disp_str("\n----- CatOS v0.14 Pre 1 -----\n\n");
	}

	memset(gdt, 0, sizeof(gdt));
	memset(idt, 0, sizeof(idt));

	{
		t_16 loader_gdt_limit = *((t_16*)(&gdt_ptr[0]));
		t_32 loader_gdt_base  = *((t_32*)(&gdt_ptr[2]));
		memcpy(&gdt, (void*)loader_gdt_base, loader_gdt_limit + 1);
	}

	t_16* p_gdt_limit = (t_16*)(&gdt_ptr[0]);
	t_32* p_gdt_base  = (t_32*)(&gdt_ptr[2]);
	*p_gdt_limit = GDT_SIZE * sizeof(DESCRIPTOR) - 1;
	*p_gdt_base  = (t_32)&gdt;

	t_16* p_idt_limit = (t_16*)(&idt_ptr[0]);
	t_32* p_idt_base  = (t_32*)(&idt_ptr[2]);
	*p_idt_limit = IDT_SIZE * sizeof(GATE) - 1;
	*p_idt_base  = (t_32)&idt;

	init_prot();
}
