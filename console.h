#ifndef _CONSOLE_H
#define _CONSOLE_H

#include "mem.h"
#include "io.h"
#include "fe2010.h"
#include "mos5720.h"

void console_pause(void);
void console_resume(void);
void console_exit(void);
void console_init(io_t *io);
void console_execute_keyboard(fe2010_t *fe2010, mos5720_t *mos5720);
void console_execute_screen(mem_t *mem);

#endif /* _CONSOLE_H */
