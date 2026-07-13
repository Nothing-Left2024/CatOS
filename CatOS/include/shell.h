/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               shell.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    CatOS Shell
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _CATOS_SHELL_H_
#define _CATOS_SHELL_H_


#define SHELL_CMD_MAX_LEN    128  /* 命令行最大长度 */
#define SHELL_CMD_ARG_MAX     16  /* 最大参数个数 */
#define SHELL_PROMPT       "CatOS> "  /* 命令提示符 */
#define SHELL_PROMPT_LEN   7
#define SHELL_VERSION      "CatOS v1.0 - Protected Mode Shell"
#define MAX_CMD_HISTORY     10  /* 最大历史命令数 */


/* 命令结构 */
typedef struct s_shell_cmd {
	char name[32];           /* 命令名称 */
	char desc[64];           /* 命令描述 */
	void (*handler)(int argc, char** argv);  /* 命令处理函数 */
} SHELL_CMD;


/* Shell 函数声明 */
PUBLIC void shell_parse_and_execute(char* cmdline, int *p_cursor);

/* 获取当前工作目录簇号 (0=根目录, >=2=子目录) */
PUBLIC t_32 shell_get_cwd(void);

/* 获取当前工作目录路径字符串 (如 "/" 或 "/test/") */
PUBLIC const char *shell_get_cwd_path(void);


#endif /* _CATOS_SHELL_H_ */
