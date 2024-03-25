#ifndef _I8088_TRACE_H
#define _I8088_TRACE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "i8088.h"

#ifdef FMT_UNIX
#define FMT_U "0x%x"
#define FMT_S "0x%hhx"
#define FMT_N "0x%hx"
#else
#define FMT_U "%Xh"
#define FMT_S "%04hXh"
#define FMT_N "%hXh"
#endif

void i8088_trace_start(i8088_t *cpu);
void i8088_trace_mc(uint8_t mc);
void i8088_trace_op_prefix(const char *s);
void i8088_trace_op_seg_override(const char *s);
void i8088_trace_op_seg_default(const char *s);
void i8088_trace_op_mnemonic(const char *s);
void i8088_trace_op_dst(bool eaddr, const char *format, ...);
void i8088_trace_op_src(bool eaddr, const char *format, ...);
void i8088_trace_op_disp(uint16_t disp);
void i8088_trace_op_bit_size(uint8_t size);
void i8088_trace_op_dst_modrm_rm(uint8_t modrm, int bit_size);
void i8088_trace_op_dst_modrm_reg(uint8_t modrm, int bit_size);
void i8088_trace_end(void);

void i8088_trace_int(uint8_t int_no, i8088_t *cpu);

void i8088_trace_init(void);
void i8088_trace_dump(FILE *fh, bool extended);
void i8088_trace_int_dump(FILE *fh);

#endif /* _I8088_TRACE_H */
