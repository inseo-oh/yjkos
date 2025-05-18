#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

struct Stream;

#define CON_BACKSPACE   '\x7f'
#define CON_DELETE      '\x08'

/* For Co~Set~Console functions, pass NULL to disable the console. */
void Co_SetPrimaryConsole(struct Stream *device);
void Co_SetDebugConsole(struct Stream *device);
void Co_AskPrimaryConsole(void);
void Co_PutChar(char c);
void Co_PutString(char const *s);
void Co_VPrintf(char const *fmt, va_list ap);
int Co_GetChar(void);
void Co_Printf(char const *fmt, ...);
