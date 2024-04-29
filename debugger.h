#ifndef _DEBUGGER_H
#define _DEBUGGER_H

#include <stdbool.h>
#include "i8088.h"
#include "mem.h"
#include "fe2010.h"
#include "fdc9268.h"

bool debugger(i8088_t *cpu, mem_t *mem, fe2010_t *fe2010, fdc9268_t *fdc9268);
#ifdef BREAKPOINT
extern int32_t debugger_breakpoint;
#endif /* BREAKPOINT */

#endif /* _DEBUGGER_H */
