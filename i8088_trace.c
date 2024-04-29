#include "i8088_trace.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "i8088.h"



#define I8088_TRACE_BUFFER_SIZE 512
#define I8088_TRACE_MC_MAX 8

#define I8088_TRACE_OP_PREFIX_MAX 8
#define I8088_TRACE_OP_SEG_MAX 3
#define I8088_TRACE_OP_MNEMONIC_MAX 8
#define I8088_TRACE_OP_DST_MAX 16
#define I8088_TRACE_OP_SRC_MAX 16

#define I8088_TRACE_INT_BUFFER_SIZE 256
#define I8088_TRACE_INT_MAX 80



typedef struct i8088_trace_s {
  i8088_t cpu;
  uint8_t mc[I8088_TRACE_MC_MAX];
  int mc_n;
  char op_prefix[I8088_TRACE_OP_PREFIX_MAX];
  char op_seg_override[I8088_TRACE_OP_SEG_MAX];
  char op_seg_default[I8088_TRACE_OP_SEG_MAX];
  char op_mnemonic[I8088_TRACE_OP_MNEMONIC_MAX];
  char op_dst[I8088_TRACE_OP_DST_MAX];
  char op_src[I8088_TRACE_OP_SRC_MAX];
  bool op_dst_eaddr;
  bool op_src_eaddr;
  uint16_t op_disp;
  bool op_disp_used;
  uint8_t op_bit_size;
} i8088_trace_t;

static i8088_trace_t trace_buffer[I8088_TRACE_BUFFER_SIZE];
static int trace_buffer_n = 0;

static char trace_int_buffer[I8088_TRACE_INT_BUFFER_SIZE][I8088_TRACE_INT_MAX];
static int trace_int_buffer_n = 0;



void i8088_trace_start(i8088_t *cpu)
{
  memcpy(&trace_buffer[trace_buffer_n].cpu, cpu, sizeof(i8088_t));
  trace_buffer[trace_buffer_n].mc_n = 0; /* Clear before use! */
  trace_buffer[trace_buffer_n].op_prefix[0] = '\0';
  trace_buffer[trace_buffer_n].op_seg_override[0] = '\0';
  trace_buffer[trace_buffer_n].op_mnemonic[0] = '\0';
  trace_buffer[trace_buffer_n].op_dst[0] = '\0';
  trace_buffer[trace_buffer_n].op_src[0] = '\0';
  trace_buffer[trace_buffer_n].op_dst_eaddr = false;
  trace_buffer[trace_buffer_n].op_src_eaddr = false;
  trace_buffer[trace_buffer_n].op_disp_used = false;
  trace_buffer[trace_buffer_n].op_bit_size = 0;
}



void i8088_trace_mc(uint8_t mc)
{
  trace_buffer[trace_buffer_n].mc[trace_buffer[trace_buffer_n].mc_n] = mc;
  trace_buffer[trace_buffer_n].mc_n++;
  if (trace_buffer[trace_buffer_n].mc_n >= I8088_TRACE_MC_MAX) {
    trace_buffer[trace_buffer_n].mc_n = 0;
  }
}



void i8088_trace_op_prefix(const char *s)
{
  strncpy(trace_buffer[trace_buffer_n].op_prefix, s,
    I8088_TRACE_OP_PREFIX_MAX);
}



void i8088_trace_op_seg_override(const char *s)
{
  strncpy(trace_buffer[trace_buffer_n].op_seg_override, s,
    I8088_TRACE_OP_SEG_MAX);
}



void i8088_trace_op_seg_default(const char *s)
{
  strncpy(trace_buffer[trace_buffer_n].op_seg_default, s,
    I8088_TRACE_OP_SEG_MAX);
}



void i8088_trace_op_mnemonic(const char *s)
{
  strncpy(trace_buffer[trace_buffer_n].op_mnemonic, s,
    I8088_TRACE_OP_MNEMONIC_MAX);
}



void i8088_trace_op_dst(bool eaddr, const char *format, ...)
{
  va_list args;
  trace_buffer[trace_buffer_n].op_dst_eaddr = eaddr;
  va_start(args, format);
  vsnprintf(trace_buffer[trace_buffer_n].op_dst,
    I8088_TRACE_OP_DST_MAX, format, args);
  va_end(args);
}



void i8088_trace_op_src(bool eaddr, const char *format, ...)
{
  va_list args;
  trace_buffer[trace_buffer_n].op_src_eaddr = eaddr;
  va_start(args, format);
  vsnprintf(trace_buffer[trace_buffer_n].op_src,
    I8088_TRACE_OP_SRC_MAX, format, args);
  va_end(args);
}



void i8088_trace_op_disp(uint16_t disp)
{
  trace_buffer[trace_buffer_n].op_disp = disp;
  trace_buffer[trace_buffer_n].op_disp_used = true;
}



void i8088_trace_op_bit_size(uint8_t size)
{
  trace_buffer[trace_buffer_n].op_bit_size = size;
}



void i8088_trace_op_dst_modrm_rm(uint8_t modrm, int bit_size)
{
  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    if (bit_size == 8) {
      switch (modrm_rm(modrm)) {
      case REG8_AL:
        i8088_trace_op_dst(false, "al");
        return;
      case REG8_CL:
        i8088_trace_op_dst(false, "cl");
        return;
      case REG8_DL:
        i8088_trace_op_dst(false, "dl");
        return;
      case REG8_BL:
        i8088_trace_op_dst(false, "bl");
        return;
      case REG8_AH:
        i8088_trace_op_dst(false, "ah");
        return;
      case REG8_CH:
        i8088_trace_op_dst(false, "ch");
        return;
      case REG8_DH:
        i8088_trace_op_dst(false, "dh");
        return;
      case REG8_BH:
        i8088_trace_op_dst(false, "bh");
        return;
      }
    } else if (bit_size == 16) {
      switch (modrm_rm(modrm)) {
      case REG16_AX:
        i8088_trace_op_dst(false, "ax");
        return;
      case REG16_CX:
        i8088_trace_op_dst(false, "cx");
        return;
      case REG16_DX:
        i8088_trace_op_dst(false, "dx");
        return;
      case REG16_BX:
        i8088_trace_op_dst(false, "bx");
        return;
      case REG16_SP:
        i8088_trace_op_dst(false, "sp");
        return;
      case REG16_BP:
        i8088_trace_op_dst(false, "bp");
        return;
      case REG16_SI:
        i8088_trace_op_dst(false, "si");
        return;
      case REG16_DI:
        i8088_trace_op_dst(false, "di");
        return;
      }
    }
    break;

  case MOD_DISP_LO_SIGN:
  case MOD_DISP_HI_LO:
  case MOD_DISP_ZERO:
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_dst(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    return;

  case EADDR_BX_DI:
    i8088_trace_op_dst(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    return;

  case EADDR_BP_SI:
    i8088_trace_op_dst(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    return;

  case EADDR_BP_DI:
    i8088_trace_op_dst(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    return;

  case EADDR_SI:
    i8088_trace_op_dst(true, "si");
    i8088_trace_op_seg_default("ds");
    return;

  case EADDR_DI:
    i8088_trace_op_dst(true, "di");
    i8088_trace_op_seg_default("ds");
    return;

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      i8088_trace_op_dst(true, "");
      i8088_trace_op_seg_default("ds");
      return;
    } else {
      i8088_trace_op_dst(true, "bp");
      i8088_trace_op_seg_default("ss");
      return;
    }

  case EADDR_BX:
    i8088_trace_op_dst(true, "bx");
    i8088_trace_op_seg_default("ds");
    return;
  }

  return;
}



void i8088_trace_op_dst_modrm_reg(uint8_t modrm, int bit_size)
{
  if (bit_size == 8) {
    switch (modrm_reg(modrm)) {
    case REG8_AL:
      i8088_trace_op_dst(false, "al");
      return;
    case REG8_CL:
      i8088_trace_op_dst(false, "cl");
      return;
    case REG8_DL:
      i8088_trace_op_dst(false, "dl");
      return;
    case REG8_BL:
      i8088_trace_op_dst(false, "bl");
      return;
    case REG8_AH:
      i8088_trace_op_dst(false, "ah");
      return;
    case REG8_CH:
      i8088_trace_op_dst(false, "ch");
      return;
    case REG8_DH:
      i8088_trace_op_dst(false, "dh");
      return;
    case REG8_BH:
      i8088_trace_op_dst(false, "bh");
      return;
    }
  } else if (bit_size == 16) {
    switch (modrm_reg(modrm)) {
    case REG16_AX:
      i8088_trace_op_dst(false, "ax");
      return;
    case REG16_CX:
      i8088_trace_op_dst(false, "cx");
      return;
    case REG16_DX:
      i8088_trace_op_dst(false, "dx");
      return;
    case REG16_BX:
      i8088_trace_op_dst(false, "bx");
      return;
    case REG16_SP:
      i8088_trace_op_dst(false, "sp");
      return;
    case REG16_BP:
      i8088_trace_op_dst(false, "bp");
      return;
    case REG16_SI:
      i8088_trace_op_dst(false, "si");
      return;
    case REG16_DI:
      i8088_trace_op_dst(false, "di");
      return;
    }
  }
}



void i8088_trace_end(void)
{
  trace_buffer_n++;
  if (trace_buffer_n >= I8088_TRACE_BUFFER_SIZE) {
    trace_buffer_n = 0;
  }
}



void i8088_trace_int(uint8_t int_no, i8088_t *cpu)
{
  snprintf(trace_int_buffer[trace_int_buffer_n], I8088_TRACE_INT_MAX,
    "int%02x : ax=%04x bx=%04x cx=%04x dx=%04x "
    "si=%04x di=%04x cs:ip=%04x:%04x\n",
    int_no, cpu->ax, cpu->bx, cpu->cx, cpu->dx,
    cpu->si, cpu->di, cpu->cs, cpu->ip);

  trace_int_buffer_n++;
  if (trace_int_buffer_n >= I8088_TRACE_INT_BUFFER_SIZE) {
    trace_int_buffer_n = 0;
  }
}



void i8088_trace_init(void)
{
  int i;

  for (i = 0; i < I8088_TRACE_BUFFER_SIZE; i++) {
    memset(&trace_buffer[i], 0, sizeof(i8088_trace_t));
  }
  trace_buffer_n = 0;

  for (i = 0; i < I8088_TRACE_INT_BUFFER_SIZE; i++) {
    trace_int_buffer[i][0] = '\0';
  }
  trace_int_buffer_n = 0;
}



static int i8088_trace_op_decode(char *buffer, size_t size,
  i8088_trace_t *trace)
{
  int n = 0;

  if ((strncmp(trace->op_mnemonic, "movs", 4) == 0) ||
      (strncmp(trace->op_mnemonic, "cmps", 4) == 0) ||
      (strncmp(trace->op_mnemonic, "stos", 4) == 0) ||
      (strncmp(trace->op_mnemonic, "lods", 4) == 0) ||
      (strncmp(trace->op_mnemonic, "scas", 4) == 0)) {
    if (trace->op_seg_override[0] != '\0') {
      n += snprintf(&buffer[n], size - n, trace->op_seg_override);
      n += snprintf(&buffer[n], size - n, " ");
    }
  }

  if (trace->op_prefix[0] != '\0') {
    n += snprintf(&buffer[n], size - n, trace->op_prefix);
    n += snprintf(&buffer[n], size - n, " ");
  }

  n += snprintf(&buffer[n], size - n, trace->op_mnemonic);

  if (trace->op_dst[0] != '\0' || trace->op_dst_eaddr) {
    n += snprintf(&buffer[n], size - n, " ");

    if (trace->op_dst_eaddr) {
      if (trace->op_bit_size == 8) {
        n += snprintf(&buffer[n], size - n, "byte ");
      } else if (trace->op_bit_size == 16) {
        n += snprintf(&buffer[n], size - n, "word ");
      } else if (trace->op_bit_size == 32) {
        n += snprintf(&buffer[n], size - n, "dword ");
      }

      n += snprintf(&buffer[n], size - n, "[");

      if (trace->op_seg_override[0] != '\0') {
        n += snprintf(&buffer[n], size - n, trace->op_seg_override);
        n += snprintf(&buffer[n], size - n, ":");
      } else if (trace->op_seg_default[0] != '\0') {
        n += snprintf(&buffer[n], size - n, trace->op_seg_default);
        n += snprintf(&buffer[n], size - n, ":");
      }

      if (trace->op_dst[0] != '\0') {
        n += snprintf(&buffer[n], size - n, trace->op_dst);
        if (trace->op_disp_used) {
          if (trace->op_disp >= 0x8000) {
            n += snprintf(&buffer[n], size - n, "-" FMT_U,
              0x10000 - trace->op_disp);
          } else {
            n += snprintf(&buffer[n], size - n, "+" FMT_U,
              trace->op_disp);
          }
        }
      } else {
        if (trace->op_disp_used) {
          n += snprintf(&buffer[n], size - n, FMT_U, trace->op_disp);
        }
      }

      n += snprintf(&buffer[n], size - n, "]");

    } else {
      n += snprintf(&buffer[n], size - n, trace->op_dst);
    }
  }

  if (trace->op_src[0] != '\0' || trace->op_src_eaddr) {
    n += snprintf(&buffer[n], size - n, ", ");

    if (trace->op_src_eaddr) {
      if (trace->op_bit_size == 8) {
        n += snprintf(&buffer[n], size - n, "byte ");
      } else if (trace->op_bit_size == 16) {
        n += snprintf(&buffer[n], size - n, "word ");
      } else if (trace->op_bit_size == 32) {
        n += snprintf(&buffer[n], size - n, "dword ");
      }

      n += snprintf(&buffer[n], size - n, "[");

      if (trace->op_seg_override[0] != '\0') {
        n += snprintf(&buffer[n], size - n, trace->op_seg_override);
        n += snprintf(&buffer[n], size - n, ":");
      } else if (trace->op_seg_default[0] != '\0') {
        n += snprintf(&buffer[n], size - n, trace->op_seg_default);
        n += snprintf(&buffer[n], size - n, ":");
      }

      if (trace->op_src[0] != '\0') {
        n += snprintf(&buffer[n], size - n, trace->op_src);
        if (trace->op_disp_used) {
          if (trace->op_disp >= 0x8000) {
            n += snprintf(&buffer[n], size - n, "-" FMT_U,
              0x10000 - trace->op_disp);
          } else {
            n += snprintf(&buffer[n], size - n, "+" FMT_U,
              trace->op_disp);
          }
        }
      } else {
        if (trace->op_disp_used) {
          n += snprintf(&buffer[n], size - n, FMT_U, trace->op_disp);
        }
      }

      n += snprintf(&buffer[n], size - n, "]");

    } else {
      n += snprintf(&buffer[n], size - n, trace->op_src);
    }
  }

  return n;
}



static void i8088_trace_print(FILE *fh, i8088_trace_t *trace, bool extended)
{
  int i;
  int written;
  char buffer[40];

  fprintf(fh, "%04X:%04X  ", trace->cpu.cs, trace->cpu.ip);

  if (extended) {
    for (i = 0; i < trace->mc_n; i++) {
      fprintf(fh, "%02X", trace->mc[i]);
    }
    for (; i < I8088_TRACE_MC_MAX + 1; i++) {
      fprintf(fh, "  ");
    }
  }

  written = i8088_trace_op_decode(buffer, sizeof(buffer), trace);
  fprintf(fh, "%s", buffer);
  for (i = written; i < 29; i++) {
    fprintf(fh, " ");
  }

  fprintf(fh, "%04x ", trace->cpu.ax);
  fprintf(fh, "%04x ", trace->cpu.bx);
  fprintf(fh, "%04x ", trace->cpu.cx);
  fprintf(fh, "%04x ", trace->cpu.dx);
  fprintf(fh, "%04x:", trace->cpu.ss);
  fprintf(fh, "%04x ", trace->cpu.sp);
  if (extended) {
    fprintf(fh, "%04x ", trace->cpu.bp);
    fprintf(fh, "%04x:", trace->cpu.ds);
    fprintf(fh, "%04x ", trace->cpu.si);
    fprintf(fh, "%04x:", trace->cpu.es);
    fprintf(fh, "%04x ", trace->cpu.di);
  }
  fprintf(fh, "%c", trace->cpu.o ? 'O' : '-');
  fprintf(fh, "%c", trace->cpu.d ? 'D' : '-');
  fprintf(fh, "%c", trace->cpu.i ? 'I' : '-');
  fprintf(fh, "%c", trace->cpu.t ? 'T' : '-');
  fprintf(fh, "%c", trace->cpu.s ? 'S' : '-');
  fprintf(fh, "%c", trace->cpu.z ? 'Z' : '-');
  fprintf(fh, "%c", trace->cpu.a ? 'A' : '-');
  fprintf(fh, "%c", trace->cpu.p ? 'P' : '-');
  fprintf(fh, "%c", trace->cpu.c ? 'C' : '-');

  fprintf(fh, "\n");
}



void i8088_trace_dump(FILE *fh, bool extended)
{
  int i;

  if (extended) {
    fprintf(fh, "  CS:IP    Code              Disassembly                  "
                "AX   BX   CX   DX     SS:SP   BP     DS:SI     ES:DI   Flags\n");
  } else {
    fprintf(fh, "  CS:IP    Disassembly                  "
                "AX   BX   CX   DX     SS:SP   Flags\n");
  }

  for (i = trace_buffer_n; i < I8088_TRACE_BUFFER_SIZE; i++) {
    if (trace_buffer[i].mc_n != 0 && trace_buffer[i].op_mnemonic[0] != '\0') {
      i8088_trace_print(fh, &trace_buffer[i], extended);
    }
  }
  for (i = 0; i < trace_buffer_n; i++) {
    if (trace_buffer[i].mc_n != 0 && trace_buffer[i].op_mnemonic[0] != '\0') {
      i8088_trace_print(fh, &trace_buffer[i], extended);
    }
  }
}



void i8088_trace_int_dump(FILE *fh)
{
  int i;

  for (i = trace_int_buffer_n; i < I8088_TRACE_INT_BUFFER_SIZE; i++) {
    if (trace_int_buffer[i][0] != '\0') {
      fprintf(fh, trace_int_buffer[i]);
    }
  }
  for (i = 0; i < trace_int_buffer_n; i++) {
    if (trace_int_buffer[i][0] != '\0') {
      fprintf(fh, trace_int_buffer[i]);
    }
  }
}



