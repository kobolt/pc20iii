#include "i8088.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "i8088_trace.h"
#include "mem.h"
#include "io.h"
#include "panic.h"

#define INT_DIVIDE_ERROR 0
#define INT_SINGLE_STEP  1
#define INT_NMI          2
#define INT_1_BYTE       3
#define INT_OVERFLOW     4

#define MODRM_OPCODE_ADD   0b000
#define MODRM_OPCODE_OR    0b001
#define MODRM_OPCODE_ADC   0b010
#define MODRM_OPCODE_SBB   0b011
#define MODRM_OPCODE_AND   0b100
#define MODRM_OPCODE_SUB   0b101
#define MODRM_OPCODE_XOR   0b110
#define MODRM_OPCODE_CMP   0b111

#define MODRM_OPCODE_ROL   0b000
#define MODRM_OPCODE_ROR   0b001
#define MODRM_OPCODE_RCL   0b010
#define MODRM_OPCODE_RCR   0b011
#define MODRM_OPCODE_SHL   0b100
#define MODRM_OPCODE_SHR   0b101
#define MODRM_OPCODE_SAR   0b111

#define MODRM_OPCODE_TEST     0b000
#define MODRM_OPCODE_TEST_2   0b001
#define MODRM_OPCODE_NOT      0b010
#define MODRM_OPCODE_NEG      0b011
#define MODRM_OPCODE_MUL      0b100
#define MODRM_OPCODE_IMUL     0b101
#define MODRM_OPCODE_DIV      0b110
#define MODRM_OPCODE_IDIV     0b111

#define MODRM_OPCODE_INC        0b000
#define MODRM_OPCODE_DEC        0b001
#define MODRM_OPCODE_CALL       0b010
#define MODRM_OPCODE_CALL_FAR   0b011
#define MODRM_OPCODE_JMP        0b100
#define MODRM_OPCODE_JMP_FAR    0b101
#define MODRM_OPCODE_PUSH       0b110
#define MODRM_OPCODE_PUSH_2     0b111

#ifndef CPU_TRACE
#define i8088_trace_start(...)
#define i8088_trace_mc(...)
#define i8088_trace_op_prefix(...)
#define i8088_trace_op_seg_override(...)
#define i8088_trace_op_seg_default(...)
#define i8088_trace_op_mnemonic(...)
#define i8088_trace_op_dst(...)
#define i8088_trace_op_src(...)
#define i8088_trace_op_disp(...)
#define i8088_trace_op_bit_size(...)
#define i8088_trace_op_dst_modrm_rm(...)
#define i8088_trace_op_dst_modrm_reg(...)
#define i8088_trace_end(...)
#define i8088_trace_int(...)
#endif



static inline bool parity_even(uint16_t value)
{
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return (~value) & 1;
}



static inline uint8_t fetch(i8088_t *cpu, mem_t *mem)
{
  uint8_t mc;
  mc = mem_read_by_segment(mem, cpu->cs, cpu->ip);
  cpu->ip++;
  i8088_trace_mc(mc);
  return mc;
}



static uint8_t eaddr_read_8(i8088_t *cpu, mem_t *mem,
  uint16_t segment_default, uint16_t address, uint16_t *eaddr)
{
  if (eaddr != NULL) {
    *eaddr = address; /* Store for later use. */
  }
  switch (cpu->segment_override) {
  case SEGMENT_CS:
    return mem_read_by_segment(mem, cpu->cs, address);
  case SEGMENT_DS:
    return mem_read_by_segment(mem, cpu->ds, address);
  case SEGMENT_ES:
    return mem_read_by_segment(mem, cpu->es, address);
  case SEGMENT_SS:
    return mem_read_by_segment(mem, cpu->ss, address);
  case SEGMENT_NONE:
  default:
    return mem_read_by_segment(mem, segment_default, address);
  }
}



static void eaddr_write_8(i8088_t *cpu, mem_t *mem,
  uint16_t segment_default, uint16_t address, uint8_t value)
{
  switch (cpu->segment_override) {
  case SEGMENT_CS:
    mem_write_by_segment(mem, cpu->cs, address, value);
    break;
  case SEGMENT_DS:
    mem_write_by_segment(mem, cpu->ds, address, value);
    break;
  case SEGMENT_ES:
    mem_write_by_segment(mem, cpu->es, address, value);
    break;
  case SEGMENT_SS:
    mem_write_by_segment(mem, cpu->ss, address, value);
    break;
  case SEGMENT_NONE:
  default:
    mem_write_by_segment(mem, segment_default, address, value);
    break;
  }
}



static uint16_t eaddr_read_16(i8088_t *cpu, mem_t *mem,
  uint16_t segment_default, uint16_t address, uint16_t *eaddr)
{
  uint16_t value;
  if (eaddr != NULL) {
    *eaddr = address; /* Store for later use. */
  }
  switch (cpu->segment_override) {
  case SEGMENT_CS:
    value  = mem_read_by_segment(mem, cpu->cs, address);
    value += mem_read_by_segment(mem, cpu->cs, address+1) * 0x100;
    break;
  case SEGMENT_DS:
    value  = mem_read_by_segment(mem, cpu->ds, address);
    value += mem_read_by_segment(mem, cpu->ds, address+1) * 0x100;
    break;
  case SEGMENT_ES:
    value  = mem_read_by_segment(mem, cpu->es, address);
    value += mem_read_by_segment(mem, cpu->es, address+1) * 0x100;
    break;
  case SEGMENT_SS:
    value  = mem_read_by_segment(mem, cpu->ss, address);
    value += mem_read_by_segment(mem, cpu->ss, address+1) * 0x100;
    break;
  case SEGMENT_NONE:
  default:
    value  = mem_read_by_segment(mem, segment_default, address);
    value += mem_read_by_segment(mem, segment_default, address+1) * 0x100;
    break;
  }
  return value;
}



static void eaddr_write_16(i8088_t *cpu, mem_t *mem,
  uint16_t segment_default, uint16_t address, uint16_t value)
{
  switch (cpu->segment_override) {
  case SEGMENT_CS:
    mem_write_by_segment(mem, cpu->cs, address,   value % 0x100);
    mem_write_by_segment(mem, cpu->cs, address+1, value / 0x100);
    break;
  case SEGMENT_DS:
    mem_write_by_segment(mem, cpu->ds, address,   value % 0x100);
    mem_write_by_segment(mem, cpu->ds, address+1, value / 0x100);
    break;
  case SEGMENT_ES:
    mem_write_by_segment(mem, cpu->es, address,   value % 0x100);
    mem_write_by_segment(mem, cpu->es, address+1, value / 0x100);
    break;
  case SEGMENT_SS:
    mem_write_by_segment(mem, cpu->ss, address,   value % 0x100);
    mem_write_by_segment(mem, cpu->ss, address+1, value / 0x100);
    break;
  case SEGMENT_NONE:
  default:
    mem_write_by_segment(mem, segment_default, address,   value % 0x100);
    mem_write_by_segment(mem, segment_default, address+1, value / 0x100);
    break;
  }
}



static uint8_t modrm_get_rm_8(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint16_t *eaddr)
{
  uint16_t disp;
  i8088_trace_op_bit_size(8);

  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG8_AL:
      i8088_trace_op_src(false, "al");
      return cpu->al;
    case REG8_CL:
      i8088_trace_op_src(false, "cl");
      return cpu->cl;
    case REG8_DL:
      i8088_trace_op_src(false, "dl");
      return cpu->dl;
    case REG8_BL:
      i8088_trace_op_src(false, "bl");
      return cpu->bl;
    case REG8_AH:
      i8088_trace_op_src(false, "ah");
      return cpu->ah;
    case REG8_CH:
      i8088_trace_op_src(false, "ch");
      return cpu->ch;
    case REG8_DH:
      i8088_trace_op_src(false, "dh");
      return cpu->dh;
    case REG8_BH:
      i8088_trace_op_src(false, "bh");
      return cpu->bh;
    }
    break;

  case MOD_DISP_LO_SIGN:
    disp = (int8_t)fetch(cpu, mem);
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_HI_LO:
    disp  = fetch(cpu, mem);
    disp += fetch(cpu, mem) * 0x100;
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_ZERO:
    disp = 0;
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_src(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_8(cpu, mem, cpu->ds, cpu->bx + cpu->si + disp, eaddr);

  case EADDR_BX_DI:
    i8088_trace_op_src(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_8(cpu, mem, cpu->ds, cpu->bx + cpu->di + disp, eaddr);

  case EADDR_BP_SI:
    i8088_trace_op_src(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    return eaddr_read_8(cpu, mem, cpu->ss, cpu->bp + cpu->si + disp, eaddr);

  case EADDR_BP_DI:
    i8088_trace_op_src(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    return eaddr_read_8(cpu, mem, cpu->ss, cpu->bp + cpu->di + disp, eaddr);

  case EADDR_SI:
    i8088_trace_op_src(true, "si");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_8(cpu, mem, cpu->ds, cpu->si + disp, eaddr);

  case EADDR_DI:
    i8088_trace_op_src(true, "di");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_8(cpu, mem, cpu->ds, cpu->di + disp, eaddr);

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      disp  = fetch(cpu, mem);
      disp += fetch(cpu, mem) * 0x100;
      i8088_trace_op_disp(disp);
      i8088_trace_op_src(true, "");
      i8088_trace_op_seg_default("ds");
      return eaddr_read_8(cpu, mem, cpu->ds, disp, eaddr);
    } else {
      i8088_trace_op_src(true, "bp");
      i8088_trace_op_seg_default("ss");
      return eaddr_read_8(cpu, mem, cpu->ss, cpu->bp + disp, eaddr);
    }

  case EADDR_BX:
    i8088_trace_op_src(true, "bx");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_8(cpu, mem, cpu->ds, cpu->bx + disp, eaddr);
  }

  return 0;
}



static void modrm_set_rm_8(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint8_t value)
{
  uint16_t disp;
  i8088_trace_op_bit_size(8);

  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG8_AL:
      i8088_trace_op_dst(false, "al");
      cpu->al = value;
      return;
    case REG8_CL:
      i8088_trace_op_dst(false, "cl");
      cpu->cl = value;
      return;
    case REG8_DL:
      i8088_trace_op_dst(false, "dl");
      cpu->dl = value;
      return;
    case REG8_BL:
      i8088_trace_op_dst(false, "bl");
      cpu->bl = value;
      return;
    case REG8_AH:
      i8088_trace_op_dst(false, "ah");
      cpu->ah = value;
      return;
    case REG8_CH:
      i8088_trace_op_dst(false, "ch");
      cpu->ch = value;
      return;
    case REG8_DH:
      i8088_trace_op_dst(false, "dh");
      cpu->dh = value;
      return;
    case REG8_BH:
      i8088_trace_op_dst(false, "bh");
      cpu->bh = value;
      return;
    }
    break;

  case MOD_DISP_LO_SIGN:
    disp = (int8_t)fetch(cpu, mem);
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_HI_LO:
    disp  = fetch(cpu, mem);
    disp += fetch(cpu, mem) * 0x100;
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_ZERO:
    disp = 0;
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_dst(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, cpu->bx + cpu->si + disp, value);
    break;

  case EADDR_BX_DI:
    i8088_trace_op_dst(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, cpu->bx + cpu->di + disp, value);
    break;

  case EADDR_BP_SI:
    i8088_trace_op_dst(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    eaddr_write_8(cpu, mem, cpu->ss, cpu->bp + cpu->si + disp, value);
    break;

  case EADDR_BP_DI:
    i8088_trace_op_dst(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    eaddr_write_8(cpu, mem, cpu->ss, cpu->bp + cpu->di + disp, value);
    break;

  case EADDR_SI:
    i8088_trace_op_dst(true, "si");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, cpu->si + disp, value);
    break;

  case EADDR_DI:
    i8088_trace_op_dst(true, "di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, cpu->di + disp, value);
    break;

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      disp  = fetch(cpu, mem);
      disp += fetch(cpu, mem) * 0x100;
      i8088_trace_op_disp(disp);
      i8088_trace_op_dst(true, "");
      i8088_trace_op_seg_default("ds");
      eaddr_write_8(cpu, mem, cpu->ds, disp, value);
    } else {
      i8088_trace_op_dst(true, "bp");
      i8088_trace_op_seg_default("ss");
      eaddr_write_8(cpu, mem, cpu->ss, cpu->bp + disp, value);
    }
    break;

  case EADDR_BX:
    i8088_trace_op_dst(true, "bx");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, cpu->bx + disp, value);
    break;
  }
}



static void modrm_set_rm_eaddr_8(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint16_t eaddr, uint8_t value)
{
  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG8_AL:
      i8088_trace_op_dst(false, "al");
      cpu->al = value;
      return;
    case REG8_CL:
      i8088_trace_op_dst(false, "cl");
      cpu->cl = value;
      return;
    case REG8_DL:
      i8088_trace_op_dst(false, "dl");
      cpu->dl = value;
      return;
    case REG8_BL:
      i8088_trace_op_dst(false, "bl");
      cpu->bl = value;
      return;
    case REG8_AH:
      i8088_trace_op_dst(false, "ah");
      cpu->ah = value;
      return;
    case REG8_CH:
      i8088_trace_op_dst(false, "ch");
      cpu->ch = value;
      return;
    case REG8_DH:
      i8088_trace_op_dst(false, "dh");
      cpu->dh = value;
      return;
    case REG8_BH:
      i8088_trace_op_dst(false, "bh");
      cpu->bh = value;
      return;
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
    eaddr_write_8(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_BX_DI:
    i8088_trace_op_dst(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_BP_SI:
    i8088_trace_op_dst(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    eaddr_write_8(cpu, mem, cpu->ss, eaddr, value);
    break;

  case EADDR_BP_DI:
    i8088_trace_op_dst(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    eaddr_write_8(cpu, mem, cpu->ss, eaddr, value);
    break;

  case EADDR_SI:
    i8088_trace_op_dst(true, "si");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_DI:
    i8088_trace_op_dst(true, "di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      i8088_trace_op_dst(true, "");
      i8088_trace_op_seg_default("ds");
      eaddr_write_8(cpu, mem, cpu->ds, eaddr, value);
    } else {
      i8088_trace_op_dst(true, "bp");
      i8088_trace_op_seg_default("ss");
      eaddr_write_8(cpu, mem, cpu->ss, eaddr, value);
    }
    break;

  case EADDR_BX:
    i8088_trace_op_dst(true, "bx");
    i8088_trace_op_seg_default("ds");
    eaddr_write_8(cpu, mem, cpu->ds, eaddr, value);
    break;
  }
}



static uint8_t modrm_get_reg_8(i8088_t *cpu, uint8_t modrm)
{
  switch (modrm_reg(modrm)) {
  case REG8_AL:
    i8088_trace_op_src(false, "al");
    return cpu->al;
  case REG8_CL:
    i8088_trace_op_src(false, "cl");
    return cpu->cl;
  case REG8_DL:
    i8088_trace_op_src(false, "dl");
    return cpu->dl;
  case REG8_BL:
    i8088_trace_op_src(false, "bl");
    return cpu->bl;
  case REG8_AH:
    i8088_trace_op_src(false, "ah");
    return cpu->ah;
  case REG8_CH:
    i8088_trace_op_src(false, "ch");
    return cpu->ch;
  case REG8_DH:
    i8088_trace_op_src(false, "dh");
    return cpu->dh;
  case REG8_BH:
    i8088_trace_op_src(false, "bh");
    return cpu->bh;
  }
  return 0;
}



static void modrm_set_reg_8(i8088_t *cpu, uint8_t modrm, uint8_t value)
{
  switch (modrm_reg(modrm)) {
  case REG8_AL:
    i8088_trace_op_dst(false, "al");
    cpu->al = value;
    break;
  case REG8_CL:
    i8088_trace_op_dst(false, "cl");
    cpu->cl = value;
    break;
  case REG8_DL:
    i8088_trace_op_dst(false, "dl");
    cpu->dl = value;
    break;
  case REG8_BL:
    i8088_trace_op_dst(false, "bl");
    cpu->bl = value;
    break;
  case REG8_AH:
    i8088_trace_op_dst(false, "ah");
    cpu->ah = value;
    break;
  case REG8_CH:
    i8088_trace_op_dst(false, "ch");
    cpu->ch = value;
    break;
  case REG8_DH:
    i8088_trace_op_dst(false, "dh");
    cpu->dh = value;
    break;
  case REG8_BH:
    i8088_trace_op_dst(false, "bh");
    cpu->bh = value;
    break;
  }
}



static uint16_t modrm_get_rm_16(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint16_t *eaddr)
{
  uint16_t disp;
  i8088_trace_op_bit_size(16);

  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG16_AX:
      i8088_trace_op_src(false, "ax");
      return cpu->ax;
    case REG16_CX:
      i8088_trace_op_src(false, "cx");
      return cpu->cx;
    case REG16_DX:
      i8088_trace_op_src(false, "dx");
      return cpu->dx;
    case REG16_BX:
      i8088_trace_op_src(false, "bx");
      return cpu->bx;
    case REG16_SP:
      i8088_trace_op_src(false, "sp");
      return cpu->sp;
    case REG16_BP:
      i8088_trace_op_src(false, "bp");
      return cpu->bp;
    case REG16_SI:
      i8088_trace_op_src(false, "si");
      return cpu->si;
    case REG16_DI:
      i8088_trace_op_src(false, "di");
      return cpu->di;
    }
    break;

  case MOD_DISP_LO_SIGN:
    disp = (int8_t)fetch(cpu, mem);
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_HI_LO:
    disp  = fetch(cpu, mem);
    disp += fetch(cpu, mem) * 0x100;
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_ZERO:
    disp = 0;
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_src(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, cpu->bx + cpu->si + disp, eaddr);

  case EADDR_BX_DI:
    i8088_trace_op_src(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, cpu->bx + cpu->di + disp, eaddr);

  case EADDR_BP_SI:
    i8088_trace_op_src(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    return eaddr_read_16(cpu, mem, cpu->ss, cpu->bp + cpu->si + disp, eaddr);

  case EADDR_BP_DI:
    i8088_trace_op_src(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    return eaddr_read_16(cpu, mem, cpu->ss, cpu->bp + cpu->di + disp, eaddr);

  case EADDR_SI:
    i8088_trace_op_src(true, "si");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, cpu->si + disp, eaddr);

  case EADDR_DI:
    i8088_trace_op_src(true, "di");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, cpu->di + disp, eaddr);

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      disp  = fetch(cpu, mem);
      disp += fetch(cpu, mem) * 0x100;
      i8088_trace_op_disp(disp);
      i8088_trace_op_src(true, "");
      i8088_trace_op_seg_default("ds");
      return eaddr_read_16(cpu, mem, cpu->ds, disp, eaddr);
    } else {
      i8088_trace_op_src(true, "bp");
      i8088_trace_op_seg_default("ss");
      return eaddr_read_16(cpu, mem, cpu->ss, cpu->bp + disp, eaddr);
    }

  case EADDR_BX:
    i8088_trace_op_src(true, "bx");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, cpu->bx + disp, eaddr);
  }

  return 0;
}



static uint16_t modrm_get_rm_eaddr_16(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint16_t eaddr)
{
  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG16_AX:
      i8088_trace_op_src(false, "ax");
      return cpu->ax;
    case REG16_CX:
      i8088_trace_op_src(false, "cx");
      return cpu->cx;
    case REG16_DX:
      i8088_trace_op_src(false, "dx");
      return cpu->dx;
    case REG16_BX:
      i8088_trace_op_src(false, "bx");
      return cpu->bx;
    case REG16_SP:
      i8088_trace_op_src(false, "sp");
      return cpu->sp;
    case REG16_BP:
      i8088_trace_op_src(false, "bp");
      return cpu->bp;
    case REG16_SI:
      i8088_trace_op_src(false, "si");
      return cpu->si;
    case REG16_DI:
      i8088_trace_op_src(false, "di");
      return cpu->di;
    }
    break;

  case MOD_DISP_LO_SIGN:
  case MOD_DISP_HI_LO:
  case MOD_DISP_ZERO:
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_src(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);

  case EADDR_BX_DI:
    i8088_trace_op_src(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);

  case EADDR_BP_SI:
    i8088_trace_op_src(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    return eaddr_read_16(cpu, mem, cpu->ss, eaddr, NULL);

  case EADDR_BP_DI:
    i8088_trace_op_src(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    return eaddr_read_16(cpu, mem, cpu->ss, eaddr, NULL);

  case EADDR_SI:
    i8088_trace_op_src(true, "si");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);

  case EADDR_DI:
    i8088_trace_op_src(true, "di");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      i8088_trace_op_src(true, "");
      i8088_trace_op_seg_default("ds");
      return eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);
    } else {
      i8088_trace_op_src(true, "bp");
      i8088_trace_op_seg_default("ss");
      return eaddr_read_16(cpu, mem, cpu->ss, eaddr, NULL);
    }

  case EADDR_BX:
    i8088_trace_op_src(true, "bx");
    i8088_trace_op_seg_default("ds");
    return eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);
  }

  return 0;
}



static void modrm_set_rm_16(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint16_t value)
{
  uint16_t disp;
  i8088_trace_op_bit_size(16);

  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG16_AX:
      i8088_trace_op_dst(false, "ax");
      cpu->ax = value;
      return;
    case REG16_CX:
      i8088_trace_op_dst(false, "cx");
      cpu->cx = value;
      return;
    case REG16_DX:
      i8088_trace_op_dst(false, "dx");
      cpu->dx = value;
      return;
    case REG16_BX:
      i8088_trace_op_dst(false, "bx");
      cpu->bx = value;
      return;
    case REG16_SP:
      i8088_trace_op_dst(false, "sp");
      cpu->sp = value;
      return;
    case REG16_BP:
      i8088_trace_op_dst(false, "bp");
      cpu->bp = value;
      return;
    case REG16_SI:
      i8088_trace_op_dst(false, "si");
      cpu->si = value;
      return;
    case REG16_DI:
      i8088_trace_op_dst(false, "di");
      cpu->di = value;
      return;
    }
    break;

  case MOD_DISP_LO_SIGN:
    disp = (int8_t)fetch(cpu, mem);
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_HI_LO:
    disp  = fetch(cpu, mem);
    disp += fetch(cpu, mem) * 0x100;
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_ZERO:
    disp = 0;
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_dst(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, cpu->bx + cpu->si + disp, value);
    break;

  case EADDR_BX_DI:
    i8088_trace_op_dst(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, cpu->bx + cpu->di + disp, value);
    break;

  case EADDR_BP_SI:
    i8088_trace_op_dst(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    eaddr_write_16(cpu, mem, cpu->ss, cpu->bp + cpu->si + disp, value);
    break;

  case EADDR_BP_DI:
    i8088_trace_op_dst(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    eaddr_write_16(cpu, mem, cpu->ss, cpu->bp + cpu->di + disp, value);
    break;

  case EADDR_SI:
    i8088_trace_op_dst(true, "si");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, cpu->si + disp, value);
    break;

  case EADDR_DI:
    i8088_trace_op_dst(true, "di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, cpu->di + disp, value);
    break;

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      disp  = fetch(cpu, mem);
      disp += fetch(cpu, mem) * 0x100;
      i8088_trace_op_disp(disp);
      i8088_trace_op_dst(true, "");
      i8088_trace_op_seg_default("ds");
      eaddr_write_16(cpu, mem, cpu->ds, disp, value);
    } else {
      i8088_trace_op_dst(true, "bp");
      i8088_trace_op_seg_default("ss");
      eaddr_write_16(cpu, mem, cpu->ss, cpu->bp + disp, value);
    }
    break;

  case EADDR_BX:
    i8088_trace_op_dst(true, "bx");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, cpu->bx + disp, value);
    break;
  }
}



static void modrm_set_rm_eaddr_16(i8088_t *cpu, mem_t *mem, uint8_t modrm,
  uint16_t eaddr, uint16_t value)
{
  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
    switch (modrm_rm(modrm)) {
    case REG16_AX:
      i8088_trace_op_dst(false, "ax");
      cpu->ax = value;
      return;
    case REG16_CX:
      i8088_trace_op_dst(false, "cx");
      cpu->cx = value;
      return;
    case REG16_DX:
      i8088_trace_op_dst(false, "dx");
      cpu->dx = value;
      return;
    case REG16_BX:
      i8088_trace_op_dst(false, "bx");
      cpu->bx = value;
      return;
    case REG16_SP:
      i8088_trace_op_dst(false, "sp");
      cpu->sp = value;
      return;
    case REG16_BP:
      i8088_trace_op_dst(false, "bp");
      cpu->bp = value;
      return;
    case REG16_SI:
      i8088_trace_op_dst(false, "si");
      cpu->si = value;
      return;
    case REG16_DI:
      i8088_trace_op_dst(false, "di");
      cpu->di = value;
      return;
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
    eaddr_write_16(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_BX_DI:
    i8088_trace_op_dst(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_BP_SI:
    i8088_trace_op_dst(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    eaddr_write_16(cpu, mem, cpu->ss, eaddr, value);
    break;

  case EADDR_BP_DI:
    i8088_trace_op_dst(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    eaddr_write_16(cpu, mem, cpu->ss, eaddr, value);
    break;

  case EADDR_SI:
    i8088_trace_op_dst(true, "si");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_DI:
    i8088_trace_op_dst(true, "di");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, eaddr, value);
    break;

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      i8088_trace_op_dst(true, "");
      i8088_trace_op_seg_default("ds");
      eaddr_write_16(cpu, mem, cpu->ds, eaddr, value);
    } else {
      i8088_trace_op_dst(true, "bp");
      i8088_trace_op_seg_default("ss");
      eaddr_write_16(cpu, mem, cpu->ss, eaddr, value);
    }
    break;

  case EADDR_BX:
    i8088_trace_op_dst(true, "bx");
    i8088_trace_op_seg_default("ds");
    eaddr_write_16(cpu, mem, cpu->ds, eaddr, value);
    break;
  }
}



static void modrm_void_rm_16(i8088_t *cpu, mem_t *mem, uint8_t modrm)
{
  uint16_t disp;
  i8088_trace_op_bit_size(16);

  switch (modrm_mod(modrm)) {
  case MOD_REGISTER:
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
    break;

  case MOD_DISP_LO_SIGN:
    disp = (int8_t)fetch(cpu, mem);
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_HI_LO:
    disp  = fetch(cpu, mem);
    disp += fetch(cpu, mem) * 0x100;
    i8088_trace_op_disp(disp);
    break;

  case MOD_DISP_ZERO:
    disp = 0;
    break;
  }

  switch (modrm_rm(modrm)) {
  case EADDR_BX_SI:
    i8088_trace_op_dst(true, "bx+si");
    i8088_trace_op_seg_default("ds");
    break;

  case EADDR_BX_DI:
    i8088_trace_op_dst(true, "bx+di");
    i8088_trace_op_seg_default("ds");
    break;

  case EADDR_BP_SI:
    i8088_trace_op_dst(true, "bp+si");
    i8088_trace_op_seg_default("ss");
    break;

  case EADDR_BP_DI:
    i8088_trace_op_dst(true, "bp+di");
    i8088_trace_op_seg_default("ss");
    break;

  case EADDR_SI:
    i8088_trace_op_dst(true, "si");
    i8088_trace_op_seg_default("ds");
    break;

  case EADDR_DI:
    i8088_trace_op_dst(true, "di");
    i8088_trace_op_seg_default("ds");
    break;

  case EADDR_BP:
    if (modrm_mod(modrm) == MOD_DISP_ZERO) {
      /* Direct Addressing */
      disp  = fetch(cpu, mem);
      disp += fetch(cpu, mem) * 0x100;
      i8088_trace_op_disp(disp);
      i8088_trace_op_dst(true, "");
      i8088_trace_op_seg_default("ds");
    } else {
      i8088_trace_op_dst(true, "bp");
      i8088_trace_op_seg_default("ss");
    }
    break;

  case EADDR_BX:
    i8088_trace_op_dst(true, "bx");
    i8088_trace_op_seg_default("ds");
    break;
  }
}



static uint16_t modrm_get_reg_16(i8088_t *cpu, uint8_t modrm)
{
  switch (modrm_reg(modrm)) {
  case REG16_AX:
    i8088_trace_op_src(false, "ax");
    return cpu->ax;
  case REG16_CX:
    i8088_trace_op_src(false, "cx");
    return cpu->cx;
  case REG16_DX:
    i8088_trace_op_src(false, "dx");
    return cpu->dx;
  case REG16_BX:
    i8088_trace_op_src(false, "bx");
    return cpu->bx;
  case REG16_SP:
    i8088_trace_op_src(false, "sp");
    return cpu->sp;
  case REG16_BP:
    i8088_trace_op_src(false, "bp");
    return cpu->bp;
  case REG16_SI:
    i8088_trace_op_src(false, "si");
    return cpu->si;
  case REG16_DI:
    i8088_trace_op_src(false, "di");
    return cpu->di;
  }
  return 0;
}



static void modrm_set_reg_16(i8088_t *cpu, uint8_t modrm, uint16_t value)
{
  switch (modrm_reg(modrm)) {
  case REG16_AX:
    i8088_trace_op_dst(false, "ax");
    cpu->ax = value;
    break;
  case REG16_CX:
    i8088_trace_op_dst(false, "cx");
    cpu->cx = value;
    break;
  case REG16_DX:
    i8088_trace_op_dst(false, "dx");
    cpu->dx = value;
    break;
  case REG16_BX:
    i8088_trace_op_dst(false, "bx");
    cpu->bx = value;
    break;
  case REG16_SP:
    i8088_trace_op_dst(false, "sp");
    cpu->sp = value;
    break;
  case REG16_BP:
    i8088_trace_op_dst(false, "bp");
    cpu->bp = value;
    break;
  case REG16_SI:
    i8088_trace_op_dst(false, "si");
    cpu->si = value;
    break;
  case REG16_DI:
    i8088_trace_op_dst(false, "di");
    cpu->di = value;
    break;
  }
}



static uint16_t modrm_get_reg_seg(i8088_t *cpu, uint8_t modrm)
{
  switch (modrm_reg(modrm) & 3) {
  case REGSEG_ES:
    i8088_trace_op_src(false, "es");
    return cpu->es;
  case REGSEG_CS:
    i8088_trace_op_src(false, "cs");
    return cpu->cs;
  case REGSEG_SS:
    i8088_trace_op_src(false, "ss");
    return cpu->ss;
  case REGSEG_DS:
    i8088_trace_op_src(false, "ds");
    return cpu->ds;
  }
  return 0;
}



static void modrm_set_reg_seg(i8088_t *cpu, uint8_t modrm, uint16_t value)
{
  switch (modrm_reg(modrm) & 3) {
  case REGSEG_ES:
    i8088_trace_op_dst(false, "es");
    cpu->es = value;
    break;
  case REGSEG_CS:
    i8088_trace_op_dst(false, "cs");
    cpu->cs = value;
    break;
  case REGSEG_SS:
    i8088_trace_op_dst(false, "ss");
    cpu->ss = value;
    break;
  case REGSEG_DS:
    i8088_trace_op_dst(false, "ds");
    cpu->ds = value;
    break;
  }
}



static void i8088_interrupt(i8088_t *cpu, mem_t *mem, uint8_t int_no)
{
  i8088_trace_int(int_no, cpu);
  cpu->sp -= 6;
  mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ip % 0x100);
  mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ip / 0x100);
  mem_write_by_segment(mem, cpu->ss, cpu->sp+2, cpu->cs % 0x100);
  mem_write_by_segment(mem, cpu->ss, cpu->sp+3, cpu->cs / 0x100);
  mem_write_by_segment(mem, cpu->ss, cpu->sp+4, cpu->flags % 0x100);
  mem_write_by_segment(mem, cpu->ss, cpu->sp+5, cpu->flags / 0x100);
  cpu->ip  = mem_read(mem, (int_no * 4));
  cpu->ip += mem_read(mem, (int_no * 4) + 1) * 0x100;
  cpu->cs  = mem_read(mem, (int_no * 4) + 2);
  cpu->cs += mem_read(mem, (int_no * 4) + 3) * 0x100;
  cpu->t = 0;
}



static void i8088_aaa(i8088_t *cpu)
{
  uint8_t initial = cpu->al;
  if (((cpu->al & 0x0F) > 9) || (cpu->a == 1)) {
    cpu->ah = cpu->ah + 1;
    cpu->al = cpu->al + 6;
    cpu->a = 1;
    cpu->c = 1;
  } else {
    cpu->a = 0;
    cpu->c = 0;
  }
  cpu->o = (!(initial & 0x80) && (cpu->al & 0x80));
  cpu->p = parity_even(cpu->al);
  cpu->s = cpu->al >> 7;
  cpu->z = cpu->al == 0;
  cpu->al = cpu->al & 0x0F;
}



static void i8088_aas(i8088_t *cpu)
{
  uint8_t initial = cpu->al;
  if (((cpu->al & 0x0F) > 9) || (cpu->a == 1)) {
    cpu->al = cpu->al - 6;
    cpu->ah = cpu->ah - 1;
    cpu->a = 1;
    cpu->c = 1;
  } else {
    cpu->a = 0;
    cpu->c = 0;
  }
  cpu->o = ((initial & 0x80) && !(cpu->al & 0x80));
  cpu->p = parity_even(cpu->al);
  cpu->s = cpu->al >> 7;
  cpu->z = cpu->al == 0;
  cpu->al = cpu->al & 0x0F;
}



static uint8_t i8088_adc_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 + input2 + cpu->c;
  cpu->a = (((input1 & 0xF) + (input2 & 0xF) + cpu->c) & 0x10) > 0;
  cpu->c = ((uint16_t)(input1 + input2 + cpu->c) & 0x100) > 0;
  cpu->o = (((input1 & 0x80) && (input2 & 0x80) && !(result & 0x80)) ||
           (!(input1 & 0x80) && !(input2 & 0x80) && (result & 0x80)));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_adc_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 + input2 + cpu->c;
  cpu->a = (((input1 & 0xF) + (input2 & 0xF) + cpu->c) & 0x10) > 0;
  cpu->c = ((uint32_t)(input1 + input2 + cpu->c) & 0x10000) > 0;
  cpu->o = (((input1 & 0x8000) && (input2 & 0x8000) && !(result & 0x8000)) ||
           (!(input1 & 0x8000) && !(input2 & 0x8000) && (result & 0x8000)));
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static uint8_t i8088_add_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 + input2;
  cpu->a = (((input1 & 0xF) + (input2 & 0xF)) & 0x10) > 0;
  cpu->c = ((uint16_t)(input1 + input2) & 0x100) > 0;
  cpu->o = (((input1 & 0x80) && (input2 & 0x80) && !(result & 0x80)) ||
           (!(input1 & 0x80) && !(input2 & 0x80) && (result & 0x80)));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_add_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 + input2;
  cpu->a = (((input1 & 0xF) + (input2 & 0xF)) & 0x10) > 0;
  cpu->c = ((uint32_t)(input1 + input2) & 0x10000) > 0;
  cpu->o = (((input1 & 0x8000) && (input2 & 0x8000) && !(result & 0x8000)) ||
           (!(input1 & 0x8000) && !(input2 & 0x8000) && (result & 0x8000)));
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static uint8_t i8088_and_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 & input2;
  cpu->c = 0;
  cpu->o = (((input1 & 0x80) && (input2 & 0x80) && !(result & 0x80)) ||
           (!(input1 & 0x80) && !(input2 & 0x80) && (result & 0x80)));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_and_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 & input2;
  cpu->c = 0;
  cpu->o = (((input1 & 0x8000) && (input2 & 0x8000) && !(result & 0x8000)) ||
           (!(input1 & 0x8000) && !(input2 & 0x8000) && (result & 0x8000)));
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static void i8088_cmp_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 - input2;
  cpu->a = (((input1 & 0xF) - (input2 & 0xF)) & 0x10) > 0;
  cpu->c = ((uint16_t)(input1 - input2) & 0x100) > 0;
  cpu->o = (((input1 & 0x80) && !(input2 & 0x80) && !(result & 0x80)) ||
           (!(input1 & 0x80) && (input2 & 0x80) && (result & 0x80)));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
}



static void i8088_cmp_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 - input2;
  cpu->a = (((input1 & 0xF) - (input2 & 0xF)) & 0x10) > 0;
  cpu->c = ((uint32_t)(input1 - input2) & 0x10000) > 0;
  cpu->o = (((input1 & 0x8000) && !(input2 & 0x8000) && !(result & 0x8000)) ||
           (!(input1 & 0x8000) && (input2 & 0x8000) && (result & 0x8000)));
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
}



static void i8088_cmpsb(i8088_t *cpu, mem_t *mem)
{
  i8088_cmp_8(cpu,
    eaddr_read_8(cpu, mem, cpu->ds, cpu->si, NULL),
    mem_read_by_segment(mem, cpu->es, cpu->di));
  if (cpu->d) {
    cpu->di -= 1;
    cpu->si -= 1;
  } else {
    cpu->di += 1;
    cpu->si += 1;
  }
}



static void i8088_cmpsw(i8088_t *cpu, mem_t *mem)
{
  uint16_t data;
  data  = mem_read_by_segment(mem, cpu->es, cpu->di);
  data += mem_read_by_segment(mem, cpu->es, cpu->di+1) * 0x100;
  i8088_cmp_16(cpu, eaddr_read_16(cpu, mem, cpu->ds, cpu->si, NULL), data);
  if (cpu->d) {
    cpu->di -= 2;
    cpu->si -= 2;
  } else {
    cpu->di += 2;
    cpu->si += 2;
  }
}



static void i8088_daa(i8088_t *cpu)
{
  uint8_t initial = cpu->al;
  bool temp_a = cpu->a;
  if (((cpu->al & 0x0F) > 9) || (cpu->a == 1)) {
    cpu->al = cpu->al + 6;
    cpu->a = 1;
  }
  if (temp_a) {
    if ((initial > 0x9F) || (cpu->c == 1)) {
      cpu->al = cpu->al + 0x60;
      cpu->c = 1;
    }
  } else {
    if ((initial > 0x99) || (cpu->c == 1)) {
      cpu->al = cpu->al + 0x60;
      cpu->c = 1;
    }
  }
  cpu->o = (!(initial & 0x80) && (cpu->al & 0x80));
  cpu->p = parity_even(cpu->al);
  cpu->s = cpu->al >> 7;
  cpu->z = cpu->al == 0;
}



static void i8088_das(i8088_t *cpu)
{
  uint8_t initial = cpu->al;
  bool temp_a = cpu->a;
  if (((cpu->al & 0x0F) > 9) || (cpu->a == 1)) {
    cpu->al = cpu->al - 6;
    cpu->a = 1;
  }
  if (temp_a) {
    if ((initial > 0x9F) || (cpu->c == 1)) {
      cpu->al = cpu->al - 0x60;
      cpu->c = 1;
    }
  } else {
    if ((initial > 0x99) || (cpu->c == 1)) {
      cpu->al = cpu->al - 0x60;
      cpu->c = 1;
    }
  }
  cpu->o = ((initial & 0x80) && !(cpu->al & 0x80));
  cpu->p = parity_even(cpu->al);
  cpu->s = cpu->al >> 7;
  cpu->z = cpu->al == 0;
}



static uint8_t i8088_dec_8(i8088_t *cpu, uint8_t input)
{
  uint8_t result = input - 1;
  cpu->a = !(input & 0xF);
  cpu->o = ((input & 0x80) && !(result & 0x80));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_dec_16(i8088_t *cpu, uint16_t input)
{
  uint16_t result = input - 1;
  cpu->a = !(input & 0xF);
  cpu->o = 0;
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static void i8088_div_8(i8088_t *cpu, mem_t *mem, uint8_t input)
{
  uint16_t quotient;
  uint8_t remainder;
  if (input == 0) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  quotient  = cpu->ax / input;
  remainder = cpu->ax % input;
  if (quotient > 0xFF) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  cpu->al = quotient & 0xFF;
  cpu->ah = remainder;
}



static void i8088_div_16(i8088_t *cpu, mem_t *mem, uint16_t input)
{
  uint32_t quotient;
  uint16_t remainder;
  if (input == 0) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  quotient  = (uint32_t)((cpu->dx << 16) + cpu->ax) / input;
  remainder = (uint32_t)((cpu->dx << 16) + cpu->ax) % input;
  if (quotient > 0xFFFF) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  cpu->ax = quotient & 0xFFFF;
  cpu->dx = remainder;
}



static void i8088_idiv_8(i8088_t *cpu, mem_t *mem, uint8_t input)
{
  int16_t quotient;
  int8_t remainder;
  if (input == 0) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  if (cpu->ax == 0x8000) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  quotient  = (int16_t)cpu->ax / (int8_t)input;
  remainder = (int16_t)cpu->ax % (int8_t)input;
  if (quotient > 127 || quotient <= -128) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  cpu->al = (int8_t)(quotient & 0xFF);
  cpu->ah = remainder;
}



static void i8088_idiv_16(i8088_t *cpu, mem_t *mem, uint16_t input)
{
  int32_t quotient;
  int16_t remainder;
  if (input == 0) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  if (cpu->dx == 0x8000 && cpu->ax == 0) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  quotient  = (int32_t)((cpu->dx << 16) + cpu->ax) / (int16_t)input;
  remainder = (int32_t)((cpu->dx << 16) + cpu->ax) % (int16_t)input;
  if (quotient > 32767 || quotient <= -32768) {
    i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
    return;
  }
  cpu->ax = (int16_t)quotient & 0xFFFF;
  cpu->dx = remainder;
}



static void i8088_imul_8(i8088_t *cpu, uint8_t input)
{
  cpu->ax = (int8_t)cpu->al * (int8_t)input;
  cpu->c = (int16_t)cpu->ax != (int8_t)cpu->ax;
  cpu->o = cpu->c;
  cpu->p = parity_even(cpu->ax >> 8);
  cpu->s = cpu->ax >> 15;
  cpu->z = (cpu->ax >> 8) == 0;
}



static void i8088_imul_16(i8088_t *cpu, uint16_t input)
{
  int32_t result = (int16_t)cpu->ax * (int16_t)input;
  cpu->ax = result & 0xFFFF;
  cpu->dx = result >> 16;
  cpu->c = result != (int16_t)result;
  cpu->o = cpu->c;
  cpu->p = parity_even(result >> 16);
  cpu->s = result >> 31;
  cpu->z = (result >> 16) == 0;
}



static uint8_t i8088_inc_8(i8088_t *cpu, uint8_t input)
{
  uint8_t result = input + 1;
  cpu->a = !(result & 0xF);
  cpu->o = input == 0x7F;
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_inc_16(i8088_t *cpu, uint16_t input)
{
  uint16_t result = input + 1;
  cpu->a = !(result & 0xF);
  cpu->o = input == 0x7FFF;
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static void i8088_lodsb(i8088_t *cpu, mem_t *mem)
{
  cpu->al = eaddr_read_8(cpu, mem, cpu->ds, cpu->si, NULL);
  if (cpu->d) {
    cpu->si -= 1;
  } else {
    cpu->si += 1;
  }
}



static void i8088_lodsw(i8088_t *cpu, mem_t *mem)
{
  cpu->al = eaddr_read_16(cpu, mem, cpu->ds, cpu->si,   NULL);
  cpu->ah = eaddr_read_16(cpu, mem, cpu->ds, cpu->si+1, NULL);
  if (cpu->d) {
    cpu->si -= 2;
  } else {
    cpu->si += 2;
  }
}



static void i8088_movsb(i8088_t *cpu, mem_t *mem)
{
  mem_write_by_segment(mem, cpu->es, cpu->di,
    eaddr_read_8(cpu, mem, cpu->ds, cpu->si, NULL));
  if (cpu->d) {
    cpu->di -= 1;
    cpu->si -= 1;
  } else {
    cpu->di += 1;
    cpu->si += 1;
  }
}



static void i8088_movsw(i8088_t *cpu, mem_t *mem)
{
  mem_write_by_segment(mem, cpu->es, cpu->di,
    eaddr_read_8(cpu, mem, cpu->ds, cpu->si, NULL));
  mem_write_by_segment(mem, cpu->es, cpu->di+1,
    eaddr_read_8(cpu, mem, cpu->ds, cpu->si+1, NULL));
  if (cpu->d) {
    cpu->di -= 2;
    cpu->si -= 2;
  } else {
    cpu->di += 2;
    cpu->si += 2;
  }
}



static void i8088_mul_8(i8088_t *cpu, uint8_t input)
{
  cpu->ax = cpu->al * input;
  cpu->c = (cpu->ax >> 8) > 0;
  cpu->o = cpu->c;
  cpu->p = parity_even(cpu->ax >> 8);
  cpu->s = cpu->ax >> 15;
  cpu->z = (cpu->ax >> 8) == 0;
}



static void i8088_mul_16(i8088_t *cpu, uint16_t input)
{
  uint32_t result = cpu->ax * input;
  cpu->ax = result & 0xFFFF;
  cpu->dx = result >> 16;
  cpu->c = (result >> 16) > 0;
  cpu->o = cpu->c;
  cpu->p = parity_even(result >> 16);
  cpu->s = result >> 31;
  cpu->z = (result >> 16) == 0;
}



static uint8_t i8088_or_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 | input2;
  cpu->c = 0;
  cpu->o = 0;
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_or_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 | input2;
  cpu->c = 0;
  cpu->o = 0;
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static uint8_t i8088_rcl_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  bool temp_c;
  if (count == 0) {
    return input;
  } else if (count == 1) {
    temp_c = cpu->c;
    cpu->c = (input >> 7);
    input <<= 1;
    input |= temp_c;
    cpu->o = ((input >> 7) & 1) != cpu->c;
  } else {
    while (count > 0) {
      temp_c = cpu->c;
      cpu->c = (input >> 7);
      input <<= 1;
      input |= temp_c;
      count--;
    }
  }
  return input;
}



static uint16_t i8088_rcl_16(i8088_t *cpu, uint16_t input, uint8_t count)
{
  bool temp_c;
  if (count == 0) {
    return input;
  } else if (count == 1) {
    temp_c = cpu->c;
    cpu->c = (input >> 15);
    input <<= 1;
    input |= temp_c;
    cpu->o = ((input >> 15) & 1) != cpu->c;
  } else {
    while (count > 0) {
      temp_c = cpu->c;
      cpu->c = (input >> 15);
      input <<= 1;
      input |= temp_c;
      count--;
    }
  }
  return input;
}



static uint8_t i8088_rcr_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  bool temp_c;
  if (count == 0) {
    return input;
  } else if (count == 1) {
    temp_c = cpu->c;
    cpu->c = input & 1;
    input >>= 1;
    input |= (temp_c << 7);
    cpu->o = ((input >> 7) & 1) ^ ((input >> 6) & 1);
  } else {
    while (count > 0) {
      temp_c = cpu->c;
      cpu->c = input & 1;
      input >>= 1;
      input |= (temp_c << 7);
      count--;
    }
  }
  return input;
}



static uint16_t i8088_rcr_16(i8088_t *cpu, uint16_t input, uint8_t count)
{
  bool temp_c;
  if (count == 0) {
    return input;
  } else if (count == 1) {
    temp_c = cpu->c;
    cpu->c = input & 1;
    input >>= 1;
    input |= (temp_c << 15);
    cpu->o = ((input >> 15) & 1) ^ ((input >> 14) & 1);
  } else {
    while (count > 0) {
      temp_c = cpu->c;
      cpu->c = input & 1;
      input >>= 1;
      input |= (temp_c << 15);
      count--;
    }
  }
  return input;
}



static uint8_t i8088_rol_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  if (count == 0) {
    return input;
  } else if (count == 1) {
    cpu->c = (input >> 7);
    input <<= 1;
    input |= cpu->c;
    cpu->o = ((input >> 7) & 1) != cpu->c;
  } else {
    while (count > 0) {
      cpu->c = (input >> 7);
      input <<= 1;
      input |= cpu->c;
      count--;
    }
  }
  return input;
}



static uint16_t i8088_rol_16(i8088_t *cpu, uint16_t input, uint8_t count)
{
  if (count == 0) {
    return input;
  } else if (count == 1) {
    cpu->c = (input >> 15);
    input <<= 1;
    input |= cpu->c;
    cpu->o = ((input >> 15) & 1) != cpu->c;
  } else {
    while (count > 0) {
      cpu->c = (input >> 15);
      input <<= 1;
      input |= cpu->c;
      count--;
    }
  }
  return input;
}



static uint8_t i8088_ror_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  if (count == 0) {
    return input;
  } else if (count == 1) {
    cpu->c = input & 1;
    input >>= 1;
    input |= (cpu->c << 7);
    cpu->o = ((input >> 7) & 1) ^ ((input >> 6) & 1);
  } else {
    while (count > 0) {
      cpu->c = input & 1;
      input >>= 1;
      input |= (cpu->c << 7);
      count--;
    }
  }
  return input;
}



static uint16_t i8088_ror_16(i8088_t *cpu, uint16_t input, uint8_t count)
{
  if (count == 0) {
    return input;
  } else if (count == 1) {
    cpu->c = input & 1;
    input >>= 1;
    input |= (cpu->c << 15);
    cpu->o = ((input >> 15) & 1) ^ ((input >> 14) & 1);
  } else {
    while (count > 0) {
      cpu->c = input & 1;
      input >>= 1;
      input |= (cpu->c << 15);
      count--;
    }
  }
  return input;
}



static uint8_t i8088_sar_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  uint8_t result = input;
  if (count == 0) {
    return result;
  } else if (count == 1) {
    cpu->c = result & 1;
    result >>= 1;
    result |= (input & 0x80);
    cpu->o = ((input & 0x80) && !(result & 0x80));
  } else {
    while (count > 0) {
      cpu->c = result & 1;
      result >>= 1;
      result |= (input & 0x80);
      count--;
    }
  }
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_sar_16(i8088_t *cpu, uint16_t input, uint8_t count)
{
  uint16_t result = input;
  if (count == 0) {
    return result;
  } else if (count == 1) {
    cpu->c = result & 1;
    result >>= 1;
    result |= (input & 0x8000);
    cpu->o = ((input & 0x8000) && !(result & 0x8000));
  } else {
    while (count > 0) {
      cpu->c = result & 1;
      result >>= 1;
      result |= (input & 0x8000);
      count--;
    }
  }
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static void i8088_scasb(i8088_t *cpu, mem_t *mem)
{
  i8088_cmp_8(cpu, cpu->al, mem_read_by_segment(mem, cpu->es, cpu->di));
  if (cpu->d) {
    cpu->di -= 1;
  } else {
    cpu->di += 1;
  }
}



static void i8088_scasw(i8088_t *cpu, mem_t *mem)
{
  uint16_t data;
  data  = mem_read_by_segment(mem, cpu->es, cpu->di);
  data += mem_read_by_segment(mem, cpu->es, cpu->di+1) * 0x100;
  i8088_cmp_16(cpu, cpu->ax, data);
  if (cpu->d) {
    cpu->di -= 2;
  } else {
    cpu->di += 2;
  }
}



static uint8_t i8088_shl_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  if (count == 0) {
    return input;
  } else if (count == 1) {
    cpu->c = input >> 7;
    input <<= 1;
    cpu->o = cpu->c ^ (input >> 7);
  } else {
    while (count > 0) {
      cpu->c = input >> 7;
      input <<= 1;
      count--;
    }
  }
  cpu->p = parity_even(input);
  cpu->s = input >> 7;
  cpu->z = input == 0;
  return input;
}



static uint16_t i8088_shl_16(i8088_t *cpu, uint16_t input, uint16_t count)
{
  if (count == 0) {
    return input;
  } else if (count == 1) {
    cpu->c = input >> 15;
    input <<= 1;
    cpu->o = cpu->c ^ (input >> 15);
  } else {
    while (count > 0) {
      cpu->c = input >> 15;
      input <<= 1;
      count--;
    }
  }
  cpu->p = parity_even(input);
  cpu->s = input >> 15;
  cpu->z = input == 0;
  return input;
}



static uint8_t i8088_shr_8(i8088_t *cpu, uint8_t input, uint8_t count)
{
  uint8_t result = input;
  if (count == 0) {
    return result;
  } else if (count == 1) {
    cpu->c = result & 1;
    result >>= 1;
    cpu->o = ((input & 0x80) && !(result & 0x80));
  } else {
    while (count > 0) {
      cpu->c = result & 1;
      result >>= 1;
      count--;
    }
  }
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_shr_16(i8088_t *cpu, uint16_t input, uint8_t count)
{
  uint16_t result = input;
  if (count == 0) {
    return result;
  } else if (count == 1) {
    cpu->c = result & 1;
    result >>= 1;
    cpu->o = ((input & 0x8000) && !(result & 0x8000));
  } else {
    while (count > 0) {
      cpu->c = result & 1;
      result >>= 1;
      count--;
    }
  }
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static uint8_t i8088_sbb_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 - input2 - cpu->c;
  cpu->a = (((input1 & 0xF) - (input2 & 0xF) - cpu->c) & 0x10) > 0;
  cpu->c = ((uint16_t)(input1 - input2 - cpu->c) & 0x100) > 0;
  cpu->o = (((input1 & 0x80) && !(input2 & 0x80) && !(result & 0x80)) ||
           (!(input1 & 0x80) && (input2 & 0x80) && (result & 0x80)));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_sbb_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 - input2 - cpu->c;
  cpu->a = (((input1 & 0xF) - (input2 & 0xF) - cpu->c) & 0x10) > 0;
  cpu->c = ((uint32_t)(input1 - input2 - cpu->c) & 0x10000) > 0;
  cpu->o = (((input1 & 0x8000) && !(input2 & 0x8000) && !(result & 0x8000)) ||
           (!(input1 & 0x8000) && (input2 & 0x8000) && (result & 0x8000)));
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static void i8088_stosb(i8088_t *cpu, mem_t *mem)
{
  mem_write_by_segment(mem, cpu->es, cpu->di, cpu->al);
  if (cpu->d) {
    cpu->di -= 1;
  } else {
    cpu->di += 1;
  }
}



static void i8088_stosw(i8088_t *cpu, mem_t *mem)
{
  mem_write_by_segment(mem, cpu->es, cpu->di,   cpu->al);
  mem_write_by_segment(mem, cpu->es, cpu->di+1, cpu->ah);
  if (cpu->d) {
    cpu->di -= 2;
  } else {
    cpu->di += 2;
  }
}



static uint8_t i8088_sub_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 - input2;
  cpu->a = (((input1 & 0xF) - (input2 & 0xF)) & 0x10) > 0;
  cpu->c = ((uint16_t)(input1 - input2) & 0x100) > 0;
  cpu->o = (((input1 & 0x80) && !(input2 & 0x80) && !(result & 0x80)) ||
           (!(input1 & 0x80) && (input2 & 0x80) && (result & 0x80)));
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_sub_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 - input2;
  cpu->a = (((input1 & 0xF) - (input2 & 0xF)) & 0x10) > 0;
  cpu->c = ((uint32_t)(input1 - input2) & 0x10000) > 0;
  cpu->o = (((input1 & 0x8000) && !(input2 & 0x8000) && !(result & 0x8000)) ||
           (!(input1 & 0x8000) && (input2 & 0x8000) && (result & 0x8000)));
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static uint8_t i8088_xor_8(i8088_t *cpu, uint8_t input1, uint8_t input2)
{
  uint8_t result = input1 ^ input2;
  cpu->c = 0;
  cpu->o = 0;
  cpu->p = parity_even(result);
  cpu->s = result >> 7;
  cpu->z = result == 0;
  return result;
}



static uint16_t i8088_xor_16(i8088_t *cpu, uint16_t input1, uint16_t input2)
{
  uint16_t result = input1 ^ input2;
  cpu->c = 0;
  cpu->o = 0;
  cpu->p = parity_even(result);
  cpu->s = result >> 15;
  cpu->z = result == 0;
  return result;
}



static void i8088_opcode_80(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint8_t value;
  uint8_t data;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
  data = fetch(cpu, mem);
  i8088_trace_op_src(false, FMT_U, data);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_ADD:
    i8088_trace_op_mnemonic("add");
    value = i8088_add_8(cpu, value, data);
    break;

  case MODRM_OPCODE_OR:
    i8088_trace_op_mnemonic("or");
    value = i8088_or_8(cpu, value, data);
    break;

  case MODRM_OPCODE_ADC:
    i8088_trace_op_mnemonic("adc");
    value = i8088_adc_8(cpu, value, data);
    break;

  case MODRM_OPCODE_SBB:
    i8088_trace_op_mnemonic("sbb");
    value = i8088_sbb_8(cpu, value, data);
    break;

  case MODRM_OPCODE_AND:
    i8088_trace_op_mnemonic("and");
    value = i8088_and_8(cpu, value, data);
    break;

  case MODRM_OPCODE_SUB:
    i8088_trace_op_mnemonic("sub");
    value = i8088_sub_8(cpu, value, data);
    break;

  case MODRM_OPCODE_XOR:
    i8088_trace_op_mnemonic("xor");
    value = i8088_xor_8(cpu, value, data);
    break;

  case MODRM_OPCODE_CMP:
    i8088_trace_op_mnemonic("cmp");
    i8088_cmp_8(cpu, value, data);
    break;

  default:
    panic("Unhandled 0x80 opcode: 0x%x\n", modrm_opcode(modrm));
    break;
  }

  modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_81(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint16_t value;
  uint16_t data;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
  data  = fetch(cpu, mem);
  data += fetch(cpu, mem) * 0x100;
  i8088_trace_op_src(false, FMT_U, data);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_ADD:
    i8088_trace_op_mnemonic("add");
    value = i8088_add_16(cpu, value, data);
    break;

  case MODRM_OPCODE_OR:
    i8088_trace_op_mnemonic("or");
    value = i8088_or_16(cpu, value, data);
    break;

  case MODRM_OPCODE_ADC:
    i8088_trace_op_mnemonic("adc");
    value = i8088_adc_16(cpu, value, data);
    break;

  case MODRM_OPCODE_SBB:
    i8088_trace_op_mnemonic("sbb");
    value = i8088_sbb_16(cpu, value, data);
    break;

  case MODRM_OPCODE_AND:
    i8088_trace_op_mnemonic("and");
    value = i8088_and_16(cpu, value, data);
    break;

  case MODRM_OPCODE_SUB:
    i8088_trace_op_mnemonic("sub");
    value = i8088_sub_16(cpu, value, data);
    break;

  case MODRM_OPCODE_XOR:
    i8088_trace_op_mnemonic("xor");
    value = i8088_xor_16(cpu, value, data);
    break;

  case MODRM_OPCODE_CMP:
    i8088_trace_op_mnemonic("cmp");
    i8088_cmp_16(cpu, value, data);
    break;

  default:
    return;
  }

  modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_83(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint16_t value;
  int8_t data;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
  data = fetch(cpu, mem);
  i8088_trace_op_src(false, FMT_N, data);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_ADD:
    i8088_trace_op_mnemonic("add");
    value = i8088_add_16(cpu, value, data);
    break;

  case MODRM_OPCODE_OR:
    i8088_trace_op_mnemonic("or");
    value = i8088_or_16(cpu, value, data);
    break;

  case MODRM_OPCODE_ADC:
    i8088_trace_op_mnemonic("adc");
    value = i8088_adc_16(cpu, value, data);
    break;

  case MODRM_OPCODE_SBB:
    i8088_trace_op_mnemonic("sbb");
    value = i8088_sbb_16(cpu, value, data);
    break;

  case MODRM_OPCODE_AND:
    i8088_trace_op_mnemonic("and");
    value = i8088_and_16(cpu, value, data);
    break;

  case MODRM_OPCODE_SUB:
    i8088_trace_op_mnemonic("sub");
    value = i8088_sub_16(cpu, value, data);
    break;

  case MODRM_OPCODE_XOR:
    i8088_trace_op_mnemonic("xor");
    value = i8088_xor_16(cpu, value, data);
    break;

  case MODRM_OPCODE_CMP:
    i8088_trace_op_mnemonic("cmp");
    i8088_cmp_16(cpu, value, data);
    break;

  default:
    return;
  }

  modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_d0_d2(i8088_t *cpu, mem_t *mem,
  uint8_t count)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint8_t value;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_8(cpu, mem, modrm, &eaddr);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_ROL:
    i8088_trace_op_mnemonic("rol");
    value = i8088_rol_8(cpu, value, count);
    break;

  case MODRM_OPCODE_ROR:
    i8088_trace_op_mnemonic("ror");
    value = i8088_ror_8(cpu, value, count);
    break;

  case MODRM_OPCODE_RCL:
    i8088_trace_op_mnemonic("rcl");
    value = i8088_rcl_8(cpu, value, count);
    break;

  case MODRM_OPCODE_RCR:
    i8088_trace_op_mnemonic("rcr");
    value = i8088_rcr_8(cpu, value, count);
    break;

  case MODRM_OPCODE_SHL:
    i8088_trace_op_mnemonic("shl");
    value = i8088_shl_8(cpu, value, count);
    break;

  case MODRM_OPCODE_SHR:
    i8088_trace_op_mnemonic("shr");
    value = i8088_shr_8(cpu, value, count);
    break;

  case MODRM_OPCODE_SAR:
    i8088_trace_op_mnemonic("sar");
    value = i8088_sar_8(cpu, value, count);
    break;

  default:
    panic("Unhandled 0xD0/0xD2 opcode: 0x%x\n", modrm_opcode(modrm));
    break;
  }

  modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_d1_d3(i8088_t *cpu, mem_t *mem,
  uint8_t count)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint16_t value;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_16(cpu, mem, modrm, &eaddr);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_ROL:
    i8088_trace_op_mnemonic("rol");
    value = i8088_rol_16(cpu, value, count);
    break;

  case MODRM_OPCODE_ROR:
    i8088_trace_op_mnemonic("ror");
    value = i8088_ror_16(cpu, value, count);
    break;

  case MODRM_OPCODE_RCL:
    i8088_trace_op_mnemonic("rcl");
    value = i8088_rcl_16(cpu, value, count);
    break;

  case MODRM_OPCODE_RCR:
    i8088_trace_op_mnemonic("rcr");
    value = i8088_rcr_16(cpu, value, count);
    break;

  case MODRM_OPCODE_SHL:
    i8088_trace_op_mnemonic("shl");
    value = i8088_shl_16(cpu, value, count);
    break;

  case MODRM_OPCODE_SHR:
    i8088_trace_op_mnemonic("shr");
    value = i8088_shr_16(cpu, value, count);
    break;

  case MODRM_OPCODE_SAR:
    i8088_trace_op_mnemonic("sar");
    value = i8088_sar_16(cpu, value, count);
    break;

  default:
    panic("Unhandled 0xD1/0xD3 opcode: 0x%x\n", modrm_opcode(modrm));
    break;
  }

  modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_f6(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint8_t value;
  uint8_t data;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_8(cpu, mem, modrm, &eaddr);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_TEST:
  case MODRM_OPCODE_TEST_2:
    i8088_trace_op_mnemonic("test");
    data = fetch(cpu, mem);
    (void)i8088_and_8(cpu, data, value);
    i8088_trace_op_src(false, FMT_U, data);
    break;

  case MODRM_OPCODE_NOT:
    i8088_trace_op_mnemonic("not");
    value = ~value;
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_NEG:
    i8088_trace_op_mnemonic("neg");
    value = i8088_sub_8(cpu, 0, value);
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_MUL:
    i8088_trace_op_mnemonic("mul");
    i8088_mul_8(cpu, value);
    i8088_trace_op_dst_modrm_rm(modrm, 8);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_IMUL:
    i8088_trace_op_mnemonic("imul");
    i8088_imul_8(cpu, value);
    i8088_trace_op_dst_modrm_rm(modrm, 8);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_DIV:
    i8088_trace_op_mnemonic("div");
    i8088_div_8(cpu, mem, value);
    i8088_trace_op_dst_modrm_rm(modrm, 8);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_IDIV:
    i8088_trace_op_mnemonic("idiv");
    i8088_idiv_8(cpu, mem, value);
    i8088_trace_op_dst_modrm_rm(modrm, 8);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  default:
    break;
  }

  modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_f7(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint16_t value;
  uint16_t data;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_16(cpu, mem, modrm, &eaddr);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_TEST:
  case MODRM_OPCODE_TEST_2:
    i8088_trace_op_mnemonic("test");
    data  = fetch(cpu, mem);
    data += fetch(cpu, mem) * 0x100;
    (void)i8088_and_16(cpu, data, value);
    i8088_trace_op_src(false, FMT_U, data);
    break;

  case MODRM_OPCODE_NOT:
    i8088_trace_op_mnemonic("not");
    value = ~value;
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_NEG:
    i8088_trace_op_mnemonic("neg");
    value = i8088_sub_16(cpu, 0, value);
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_MUL:
    i8088_trace_op_mnemonic("mul");
    i8088_mul_16(cpu, value);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_IMUL:
    i8088_trace_op_mnemonic("imul");
    i8088_imul_16(cpu, value);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_DIV:
    i8088_trace_op_mnemonic("div");
    i8088_div_16(cpu, mem, value);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_IDIV:
    i8088_trace_op_mnemonic("idiv");
    i8088_idiv_16(cpu, mem, value);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  default:
    break;
  }

  modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_fe(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint8_t value;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_8(cpu, mem, modrm, &eaddr);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_INC:
    i8088_trace_op_mnemonic("inc");
    value = i8088_inc_8(cpu, value);
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_DEC:
    i8088_trace_op_mnemonic("dec");
    value = i8088_dec_8(cpu, value);
    i8088_trace_op_src(false, "");
    break;

  default:
    panic("Unhandled 0xFE opcode: 0x%x\n", modrm_opcode(modrm));
    break;
  }

  modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr, value);
}



static void i8088_opcode_ff(i8088_t *cpu, mem_t *mem)
{
  uint8_t modrm;
  uint16_t eaddr;
  uint16_t value;

  modrm = fetch(cpu, mem);
  value = modrm_get_rm_16(cpu, mem, modrm, &eaddr);

  switch (modrm_opcode(modrm)) {
  case MODRM_OPCODE_INC:
    i8088_trace_op_mnemonic("inc");
    value = i8088_inc_16(cpu, value);
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_DEC:
    i8088_trace_op_mnemonic("dec");
    value = i8088_dec_16(cpu, value);
    i8088_trace_op_src(false, "");
    break;

  case MODRM_OPCODE_CALL:
    i8088_trace_op_mnemonic("call");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ip % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ip / 0x100);
    cpu->ip = value;
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_CALL_FAR:
    i8088_trace_op_mnemonic("callf");
    i8088_trace_op_bit_size(16);
    cpu->sp -= 4;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ip % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ip / 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+2, cpu->cs % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+3, cpu->cs / 0x100);
    cpu->ip = value;
    cpu->cs = modrm_get_rm_eaddr_16(cpu, mem, modrm, eaddr+2);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_JMP:
    i8088_trace_op_mnemonic("jmp");
    cpu->ip = value;
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_JMP_FAR:
    i8088_trace_op_mnemonic("jmpf");
    cpu->ip = value;
    cpu->cs = modrm_get_rm_eaddr_16(cpu, mem, modrm, eaddr+2);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  case MODRM_OPCODE_PUSH:
  case MODRM_OPCODE_PUSH_2:
    i8088_trace_op_mnemonic("push");
    if ((modrm_mod(modrm) == MOD_REGISTER) && (modrm_rm(modrm) == REG16_SP)) {
      /* Handle the unusual case of pushing the stack pointer. */
      value -= 2;
    }
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   value % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, value / 0x100);
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    i8088_trace_op_src(false, "");
    return; /* Do NOT write back to memory! */

  default:
    break;
  }

  modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, value);
}



void i8088_reset(i8088_t *cpu)
{
  cpu->flags = 0x0000;
  cpu->ip = 0x0000;
  cpu->cs = 0xFFFF;
  cpu->ds = 0x0000;
  cpu->ss = 0x0000;
  cpu->es = 0x0000;
}



bool i8088_irq(i8088_t *cpu, mem_t *mem, int irq_no)
{
  cpu->halt = false;
  if (cpu->i == 0) {
    return true;
  }
  i8088_interrupt(cpu, mem, irq_no + 8);
  cpu->i = 0;
  return false;
}



void i8088_init(i8088_t *cpu, io_t *io)
{
  memset(cpu, 0, sizeof(i8088_t));
  cpu->io = io;
}



void i8088_execute(i8088_t *cpu, mem_t *mem)
{
  uint8_t opcode;

  if (cpu->halt) {
    return; /* Waiting for IRQ. */
  }

  cpu->segment_override = SEGMENT_NONE;
  cpu->repeat = REPEAT_NONE;

  i8088_trace_start(cpu);
  opcode = fetch(cpu, mem);

  /* Prefix: */
i8088_execute_segment_override:
  if (opcode == 0x26) { /* ES segment override. */
    i8088_trace_op_seg_override("es");
    cpu->segment_override = SEGMENT_ES;
    opcode = fetch(cpu, mem);
  } else if (opcode == 0x2E) { /* CS segment override. */
    i8088_trace_op_seg_override("cs");
    cpu->segment_override = SEGMENT_CS;
    opcode = fetch(cpu, mem);
  } else if (opcode == 0x36) { /* SS segment override. */
    i8088_trace_op_seg_override("ss");
    cpu->segment_override = SEGMENT_SS;
    opcode = fetch(cpu, mem);
  } else if (opcode == 0x3E) { /* DS segment override. */
    i8088_trace_op_seg_override("ds");
    cpu->segment_override = SEGMENT_DS;
    opcode = fetch(cpu, mem);
  } else if (opcode == 0xF0) { /* LOCK */
    panic("LOCK not implemented!\n");
    opcode = fetch(cpu, mem);
  }

  if (opcode == 0xF2) { /* REPNE/REPNZ */
    i8088_trace_op_prefix("repne");
    cpu->repeat = REPEAT_NENZ;
    opcode = fetch(cpu, mem);
  } else if (opcode == 0xF3) { /* REPE/REPZ */
    i8088_trace_op_prefix("repe");
    cpu->repeat = REPEAT_EZ;
    opcode = fetch(cpu, mem);
  }

  /* Variables used during opcode decoding: */
  uint16_t offset;
  uint16_t segment;
  uint16_t data_16;
  uint16_t eaddr;
  uint8_t data_8;
  uint8_t modrm;
  int8_t disp;

  /* Opcode: */
  switch (opcode) {
  case 0x00: /* ADD */
    i8088_trace_op_mnemonic("add");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_add_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x01: /* ADD */
    i8088_trace_op_mnemonic("add");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_add_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x02: /* ADD */
    i8088_trace_op_mnemonic("add");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_add_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x03: /* ADD */
    i8088_trace_op_mnemonic("add");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_add_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x04: /* ADD AL,imm */
    i8088_trace_op_mnemonic("add");
    i8088_trace_op_dst(false, "al");
    data_8  = fetch(cpu, mem);
    cpu->al = i8088_add_8(cpu, cpu->ax, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x05: /* ADD AX,imm */
    i8088_trace_op_mnemonic("add");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_add_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x06: /* PUSH ES */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "es");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->es % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->es / 0x100);
    break;

  case 0x07: /* POP ES */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "es");
    cpu->es  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->es += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x08: /* OR */
    i8088_trace_op_mnemonic("or");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_or_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x09: /* OR */
    i8088_trace_op_mnemonic("or");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_or_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x0A: /* OR */
    i8088_trace_op_mnemonic("or");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_or_8(cpu, data_8,  modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x0B: /* OR */
    i8088_trace_op_mnemonic("or");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_or_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x0C: /* OR AL,imm */
    i8088_trace_op_mnemonic("or");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    cpu->al = i8088_or_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x0D: /* OR AX,imm */
    i8088_trace_op_mnemonic("or");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_or_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x0E: /* PUSH CS */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "cs");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->cs % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->cs / 0x100);
    break;

  case 0x0F: /* POP CS */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "cs");
    cpu->cs  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->cs += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x10: /* ADC */
    i8088_trace_op_mnemonic("adc");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_adc_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x11: /* ADC */
    i8088_trace_op_mnemonic("adc");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_adc_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x12: /* ADC */
    i8088_trace_op_mnemonic("adc");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_adc_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x13: /* ADC */
    i8088_trace_op_mnemonic("adc");
    modrm = fetch(cpu, mem);
    data_16 =  modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_adc_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x14: /* ADC AL,imm */
    i8088_trace_op_mnemonic("adc");
    i8088_trace_op_dst(false, "al");
    data_8  = fetch(cpu, mem);
    cpu->al = i8088_adc_8(cpu, cpu->ax, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x15: /* ADC AX,imm */
    i8088_trace_op_mnemonic("adc");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_adc_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x16: /* PUSH SS */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "ss");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ss % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ss / 0x100);
    break;

  case 0x17: /* POP SS */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "ss");
    data_16  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    data_16 += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    cpu->ss = data_16;
    break;

  case 0x18: /* SBB */
    i8088_trace_op_mnemonic("sbb");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_sbb_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x19: /* SBB */
    i8088_trace_op_mnemonic("sbb");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_sbb_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x1A: /* SBB */
    i8088_trace_op_mnemonic("sbb");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_sbb_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x1B: /* SBB */
    i8088_trace_op_mnemonic("sbb");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_sbb_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x1C: /* SBB AL,imm */
    i8088_trace_op_mnemonic("sbb");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    cpu->al = i8088_sbb_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x1D: /* SBB AX,imm */
    i8088_trace_op_mnemonic("sbb");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_sbb_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x1E: /* PUSH DS */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "ds");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ds % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ds / 0x100);
    break;

  case 0x1F: /* POP DS */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "ds");
    cpu->ds  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ds += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x20: /* AND */
    i8088_trace_op_mnemonic("and");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_and_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x21: /* AND */
    i8088_trace_op_mnemonic("and");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_and_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x22: /* AND */
    i8088_trace_op_mnemonic("and");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_and_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x23: /* AND */
    i8088_trace_op_mnemonic("and");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_and_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x24: /* AND AL,imm */
    i8088_trace_op_mnemonic("and");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    cpu->al = i8088_and_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x25: /* AND AX,imm */
    i8088_trace_op_mnemonic("and");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_and_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x26:
    goto i8088_execute_segment_override;

  case 0x27: /* DAA */
    i8088_trace_op_mnemonic("daa");
    i8088_daa(cpu);
    break;

  case 0x28: /* SUB */
    i8088_trace_op_mnemonic("sub");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_sub_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x29: /* SUB */
    i8088_trace_op_mnemonic("sub");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_sub_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x2A: /* SUB */
    i8088_trace_op_mnemonic("sub");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_sub_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x2B: /* SUB */
    i8088_trace_op_mnemonic("sub");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_sub_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x2C: /* SUB AL,imm */
    i8088_trace_op_mnemonic("sub");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    cpu->al = i8088_sub_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x2D: /* SUB AX,imm */
    i8088_trace_op_mnemonic("sub");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_sub_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x2E:
    goto i8088_execute_segment_override;

  case 0x2F: /* DAS */
    i8088_trace_op_mnemonic("das");
    i8088_das(cpu);
    break;

  case 0x30: /* XOR */
    i8088_trace_op_mnemonic("xor");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr,
      i8088_xor_8(cpu, data_8, modrm_get_reg_8(cpu, modrm)));
    break;

  case 0x31: /* XOR */
    i8088_trace_op_mnemonic("xor");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr,
      i8088_xor_16(cpu, data_16, modrm_get_reg_16(cpu, modrm)));
    break;

  case 0x32: /* XOR */
    i8088_trace_op_mnemonic("xor");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm,
      i8088_xor_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL)));
    break;

  case 0x33: /* XOR */
    i8088_trace_op_mnemonic("xor");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm,
      i8088_xor_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL)));
    break;

  case 0x34: /* XOR AL,imm */
    i8088_trace_op_mnemonic("xor");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    cpu->al = i8088_xor_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x35: /* XOR AX,imm */
    i8088_trace_op_mnemonic("xor");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = i8088_xor_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x36:
    goto i8088_execute_segment_override;

  case 0x37: /* AAA */
    i8088_trace_op_mnemonic("aaa");
    i8088_aaa(cpu);
    break;

  case 0x38: /* CMP */
    i8088_trace_op_mnemonic("cmp");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    i8088_cmp_8(cpu, data_8, modrm_get_reg_8(cpu, modrm));
    i8088_trace_op_dst_modrm_rm(modrm, 8);
    break;

  case 0x39: /* CMP */
    i8088_trace_op_mnemonic("cmp");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    i8088_cmp_16(cpu, data_16, modrm_get_reg_16(cpu, modrm));
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    break;

  case 0x3A: /* CMP */
    i8088_trace_op_mnemonic("cmp");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    i8088_cmp_8(cpu, data_8, modrm_get_rm_8(cpu, mem, modrm, NULL));
    i8088_trace_op_dst_modrm_reg(modrm, 8);
    break;

  case 0x3B: /* CMP */
    i8088_trace_op_mnemonic("cmp");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    i8088_cmp_16(cpu, data_16, modrm_get_rm_16(cpu, mem, modrm, NULL));
    i8088_trace_op_dst_modrm_reg(modrm, 16);
    break;

  case 0x3C: /* CMP AL,imm */
    i8088_trace_op_mnemonic("cmp");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    i8088_cmp_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0x3D: /* CMP AX,imm */
    i8088_trace_op_mnemonic("cmp");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    i8088_cmp_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0x3E:
    goto i8088_execute_segment_override;

  case 0x3F: /* AAS */
    i8088_trace_op_mnemonic("aas");
    i8088_aas(cpu);
    break;

  case 0x40: /* INC AX */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "ax");
    cpu->ax = i8088_inc_16(cpu, cpu->ax);
    break;

  case 0x41: /* INC CX */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "cx");
    cpu->cx = i8088_inc_16(cpu, cpu->cx);
    break;

  case 0x42: /* INC DX */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "dx");
    cpu->dx = i8088_inc_16(cpu, cpu->dx);
    break;

  case 0x43: /* INC BX */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "bx");
    cpu->bx = i8088_inc_16(cpu, cpu->bx);
    break;

  case 0x44: /* INC SP */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "sp");
    cpu->sp = i8088_inc_16(cpu, cpu->sp);
    break;

  case 0x45: /* INC BP */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "bp");
    cpu->bp = i8088_inc_16(cpu, cpu->bp);
    break;

  case 0x46: /* INC SI */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "si");
    cpu->si = i8088_inc_16(cpu, cpu->si);
    break;

  case 0x47: /* INC DI */
    i8088_trace_op_mnemonic("inc");
    i8088_trace_op_dst(false, "di");
    cpu->di = i8088_inc_16(cpu, cpu->di);
    break;

  case 0x48: /* DEC AX */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "ax");
    cpu->ax = i8088_dec_16(cpu, cpu->ax);
    break;

  case 0x49: /* DEC CX */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "cx");
    cpu->cx = i8088_dec_16(cpu, cpu->cx);
    break;

  case 0x4A: /* DEC DX */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "dx");
    cpu->dx = i8088_dec_16(cpu, cpu->dx);
    break;

  case 0x4B: /* DEC BX */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "bx");
    cpu->bx = i8088_dec_16(cpu, cpu->bx);
    break;

  case 0x4C: /* DEC SP */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "sp");
    cpu->sp = i8088_dec_16(cpu, cpu->sp);
    break;

  case 0x4D: /* DEC BP */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "bp");
    cpu->bp = i8088_dec_16(cpu, cpu->bp);
    break;

  case 0x4E: /* DEC SI */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "si");
    cpu->si = i8088_dec_16(cpu, cpu->si);
    break;

  case 0x4F: /* DEC DI */
    i8088_trace_op_mnemonic("dec");
    i8088_trace_op_dst(false, "di");
    cpu->di = i8088_dec_16(cpu, cpu->di);
    break;

  case 0x50: /* PUSH AX */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "ax");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ax % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ax / 0x100);
    break;

  case 0x51: /* PUSH CX */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "cx");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->cx % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->cx / 0x100);
    break;

  case 0x52: /* PUSH DX */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "dx");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->dx % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->dx / 0x100);
    break;

  case 0x53: /* PUSH BX */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "bx");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->bx % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->bx / 0x100);
    break;

  case 0x54: /* PUSH SP */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "sp");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->sp % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->sp / 0x100);
    break;

  case 0x55: /* PUSH BP */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "bp");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->bp % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->bp / 0x100);
    break;

  case 0x56: /* PUSH SI */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "si");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->si % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->si / 0x100);
    break;

  case 0x57: /* PUSH DI */
    i8088_trace_op_mnemonic("push");
    i8088_trace_op_dst(false, "di");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->di % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->di / 0x100);
    break;

  case 0x58: /* POP AX */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "ax");
    cpu->ax  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ax += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x59: /* POP CX */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "cx");
    cpu->cx  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->cx += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x5A: /* POP DX */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "dx");
    cpu->dx  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->dx += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x5B: /* POP BX */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "bx");
    cpu->bx  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->bx += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x5C: /* POP SP */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "sp");
    data_16  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    data_16 += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp = data_16;
    break;

  case 0x5D: /* POP BP */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "bp");
    cpu->bp  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->bp += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x5E: /* POP SI */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "si");
    cpu->si  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->si += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x5F: /* POP DI */
    i8088_trace_op_mnemonic("pop");
    i8088_trace_op_dst(false, "di");
    cpu->di  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->di += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0x70: /* JO */
    i8088_trace_op_mnemonic("jo");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->o == 1) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x71: /* JNO */
    i8088_trace_op_mnemonic("jno");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->o == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x72: /* JB/JNAE/JC */
    i8088_trace_op_mnemonic("jb");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->c == 1) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x73: /* JNB/JAE/JNC */
    i8088_trace_op_mnemonic("jnb");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->c == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x74: /* JZ/JE */
    i8088_trace_op_mnemonic("jz");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->z == 1) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x75: /* JNZ/JNE */
    i8088_trace_op_mnemonic("jnz");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->z == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x76: /* JBE/JBA */
    i8088_trace_op_mnemonic("jbe");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->c == 1 || cpu->z == 1) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x77: /* JNBE/JA */
    i8088_trace_op_mnemonic("jnbe");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->c == 0 && cpu->z == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x78: /* JS */
    i8088_trace_op_mnemonic("js");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->s == 1) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x79: /* JNS */
    i8088_trace_op_mnemonic("jns");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->s == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x7A: /* JP/JPE */
    i8088_trace_op_mnemonic("jp");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->p == 1) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x7B: /* JNP/JPO */
    i8088_trace_op_mnemonic("jnp");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->p == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x7C: /* JL/JNGE */
    i8088_trace_op_mnemonic("jl");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->s != cpu->o) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x7D: /* JNL/JGE */
    i8088_trace_op_mnemonic("jnl");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->s == cpu->o) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x7E: /* JLE/JNG */
    i8088_trace_op_mnemonic("jle");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if ((cpu->z == 1) || (cpu->s != cpu->o)) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x7F: /* JNLE/JG */
    i8088_trace_op_mnemonic("jnle");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if ((cpu->z == 0) && (cpu->s == cpu->o)) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0x80:
    i8088_opcode_80(cpu, mem);
    break;

  case 0x81:
    i8088_opcode_81(cpu, mem);
    break;

  case 0x82:
    i8088_opcode_80(cpu, mem); /* 0x82 are identical to 0x80 opcodes. */
    break;

  case 0x83:
    i8088_opcode_83(cpu, mem);
    break;

  case 0x84: /* TEST */
    i8088_trace_op_mnemonic("test");
    modrm = fetch(cpu, mem);
    (void)i8088_and_8(cpu,
      modrm_get_reg_8(cpu, modrm),
      modrm_get_rm_8(cpu, mem, modrm, NULL));
    i8088_trace_op_dst_modrm_rm(modrm, 8);
    break;

  case 0x85: /* TEST */
    i8088_trace_op_mnemonic("test");
    modrm = fetch(cpu, mem);
    (void)i8088_and_16(cpu,
      modrm_get_reg_16(cpu, modrm),
      modrm_get_rm_16(cpu, mem, modrm, NULL));
    i8088_trace_op_dst_modrm_rm(modrm, 16);
    break;

  case 0x86: /* XCHG */
    i8088_trace_op_mnemonic("xchg");
    modrm = fetch(cpu, mem);
    data_8 = modrm_get_reg_8(cpu, modrm);
    modrm_set_reg_8(cpu, modrm, modrm_get_rm_8(cpu, mem, modrm, &eaddr));
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr, data_8);
    i8088_trace_op_dst_modrm_reg(modrm, 8);
    break;

  case 0x87: /* XCHG */
    i8088_trace_op_mnemonic("xchg");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_reg_16(cpu, modrm);
    modrm_set_reg_16(cpu, modrm, modrm_get_rm_16(cpu, mem, modrm, &eaddr));
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, data_16);
    i8088_trace_op_dst_modrm_reg(modrm, 16);
    break;

  case 0x88: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    modrm_set_rm_8(cpu, mem, modrm, modrm_get_reg_8(cpu, modrm));
    break;

  case 0x89: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    modrm_set_rm_16(cpu, mem, modrm, modrm_get_reg_16(cpu, modrm));
    break;

  case 0x8A: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    modrm_set_reg_8(cpu, modrm, modrm_get_rm_8(cpu, mem, modrm, NULL));
    break;

  case 0x8B: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    modrm_set_reg_16(cpu, modrm, modrm_get_rm_16(cpu, mem, modrm, NULL));
    break;

  case 0x8C: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    modrm_set_rm_16(cpu, mem, modrm, modrm_get_reg_seg(cpu, modrm));
    break;

  case 0x8D: /* LEA */
    i8088_trace_op_mnemonic("lea");
    modrm = fetch(cpu, mem);
    (void)modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_reg_16(cpu, modrm, eaddr);
    i8088_trace_op_bit_size(0);
    break;

  case 0x8E: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    modrm_set_reg_seg(cpu, modrm, modrm_get_rm_16(cpu, mem, modrm, NULL));
    break;

  case 0x8F: /* POP */
    i8088_trace_op_mnemonic("pop");
    modrm = fetch(cpu, mem);
    (void)modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    data_16  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    data_16 += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, data_16);
    i8088_trace_op_src(false, "");
    break;

  case 0x90: /* NOP */
    i8088_trace_op_mnemonic("nop");
    break;

  case 0x91: /* XCHG CX,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "cx");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->cx;
    cpu->cx = data_16;
    break;

  case 0x92: /* XCHG DX,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "dx");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->dx;
    cpu->dx = data_16;
    break;

  case 0x93: /* XCHG BX,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "bx");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->bx;
    cpu->bx = data_16;
    break;

  case 0x94: /* XCHG SP,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "sp");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->sp;
    cpu->sp = data_16;
    break;

  case 0x95: /* XCHG BP,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "bp");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->bp;
    cpu->bp = data_16;
    break;

  case 0x96: /* XCHG SI,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "si");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->si;
    cpu->si = data_16;
    break;

  case 0x97: /* XCHG DI,AX */
    i8088_trace_op_mnemonic("xchg");
    i8088_trace_op_dst(false, "di");
    i8088_trace_op_src(false, "ax");
    data_16 = cpu->ax;
    cpu->ax = cpu->di;
    cpu->di = data_16;
    break;

  case 0x98: /* CBW */
    i8088_trace_op_mnemonic("cbw");
    if (cpu->al < 0x80) {
      cpu->ah = 0;
    } else {
      cpu->ah = 0xFF;
    }
    break;

  case 0x99: /* CWD */
    i8088_trace_op_mnemonic("cwd");
    if (cpu->ax < 0x8000) {
      cpu->dx = 0;
    } else {
      cpu->dx = 0xFFFF;
    }
    break;

  case 0x9A: /* CALL FAR */
    i8088_trace_op_mnemonic("callf");
    offset  = fetch(cpu, mem);
    offset += fetch(cpu, mem) * 0x100;
    segment  = fetch(cpu, mem);
    segment += fetch(cpu, mem) * 0x100;
    cpu->sp -= 4;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ip % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ip / 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+2, cpu->cs % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+3, cpu->cs / 0x100);
    cpu->ip = offset;
    cpu->cs = segment;
    i8088_trace_op_dst(false, FMT_S ":" FMT_S, segment, offset);
    break;

  case 0x9B: /* WAIT */
    i8088_trace_op_mnemonic("wait");
    panic("WAIT not implemented!\n");
    break;

  case 0x9C: /* PUSHF */
    i8088_trace_op_mnemonic("pushf");
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->flags % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->flags / 0x100);
    break;

  case 0x9D: /* POPF */
    i8088_trace_op_mnemonic("popf");
    cpu->flags  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->flags += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    cpu->flags |=  0b1111000000000010; /* Set some unused flags. */
    cpu->flags &= ~0b0000000000101000; /* Reset some unused flags. */
    break;

  case 0x9E: /* SAHF */
    i8088_trace_op_mnemonic("sahf");
    cpu->s = (cpu->ah >> 7) & 1;
    cpu->z = (cpu->ah >> 6) & 1;
    cpu->a = (cpu->ah >> 4) & 1;
    cpu->p = (cpu->ah >> 2) & 1;
    cpu->c =  cpu->ah       & 1;
    break;

  case 0x9F: /* LAHF */
    i8088_trace_op_mnemonic("lahf");
    cpu->ah = (cpu->s << 7) |
              (cpu->z << 6) |
              (cpu->a << 4) |
              (cpu->p << 2) |
              (     1 << 1) |
              (cpu->c);
    break;

  case 0xA0: /* MOV */
    i8088_trace_op_mnemonic("mov");
    eaddr  = fetch(cpu, mem);
    eaddr += fetch(cpu, mem) * 0x100;
    cpu->al = eaddr_read_8(cpu, mem, cpu->ds, eaddr, NULL);
    i8088_trace_op_bit_size(8);
    i8088_trace_op_seg_default("ds");
    i8088_trace_op_dst(false, "al");
    i8088_trace_op_src(true, FMT_U, eaddr);
    break;

  case 0xA1: /* MOV */
    i8088_trace_op_mnemonic("mov");
    eaddr  = fetch(cpu, mem);
    eaddr += fetch(cpu, mem) * 0x100;
    cpu->ax = eaddr_read_16(cpu, mem, cpu->ds, eaddr, NULL);
    i8088_trace_op_bit_size(16);
    i8088_trace_op_seg_default("ds");
    i8088_trace_op_dst(false, "ax");
    i8088_trace_op_src(true, FMT_U, eaddr);
    break;

  case 0xA2: /* MOV */
    i8088_trace_op_mnemonic("mov");
    eaddr  = fetch(cpu, mem);
    eaddr += fetch(cpu, mem) * 0x100;
    eaddr_write_8(cpu, mem, cpu->ds, eaddr, cpu->al);
    i8088_trace_op_bit_size(8);
    i8088_trace_op_seg_default("ds");
    i8088_trace_op_dst(true, FMT_U, eaddr);
    i8088_trace_op_src(false, "al");
    break;

  case 0xA3: /* MOV */
    i8088_trace_op_mnemonic("mov");
    eaddr  = fetch(cpu, mem);
    eaddr += fetch(cpu, mem) * 0x100;
    eaddr_write_16(cpu, mem, cpu->ds, eaddr, cpu->ax);
    i8088_trace_op_bit_size(16);
    i8088_trace_op_seg_default("ds");
    i8088_trace_op_dst(true, FMT_U, eaddr);
    i8088_trace_op_src(false, "ax");
    break;

  case 0xA4: /* MOVSB */
    i8088_trace_op_mnemonic("movsb");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_movsb(cpu, mem);
      break;
    case REPEAT_EZ:
    case REPEAT_NENZ:
      i8088_trace_op_prefix("rep");
      while (cpu->cx != 0) {
        i8088_movsb(cpu, mem);
        cpu->cx--;
      }
      break;
    }
    break;

  case 0xA5: /* MOVSW */
    i8088_trace_op_mnemonic("movsw");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_movsw(cpu, mem);
      break;
    case REPEAT_EZ:
    case REPEAT_NENZ:
      i8088_trace_op_prefix("rep");
      while (cpu->cx != 0) {
        i8088_movsw(cpu, mem);
        cpu->cx--;
      }
      break;
    }
    break;

  case 0xA6: /* CMPSB */
    i8088_trace_op_mnemonic("cmpsb");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_cmpsb(cpu, mem);
      break;
    case REPEAT_EZ:
      if (cpu->cx != 0) {
        i8088_cmpsb(cpu, mem);
        cpu->cx--;
        if (cpu->z == 1) {
          while (cpu->cx != 0 && cpu->z == 1) {
            i8088_cmpsb(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    case REPEAT_NENZ:
      if (cpu->cx != 0) {
        i8088_cmpsb(cpu, mem);
        cpu->cx--;
        if (cpu->z == 0) {
          while (cpu->cx != 0 && cpu->z == 0) {
            i8088_cmpsb(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    }
    break;

  case 0xA7: /* CMPSW */
    i8088_trace_op_mnemonic("cmpsw");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_cmpsw(cpu, mem);
      break;
    case REPEAT_EZ:
      if (cpu->cx != 0) {
        i8088_cmpsw(cpu, mem);
        cpu->cx--;
        if (cpu->z == 1) {
          while (cpu->cx != 0 && cpu->z == 1) {
            i8088_cmpsw(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    case REPEAT_NENZ:
      if (cpu->cx != 0) {
        i8088_cmpsw(cpu, mem);
        cpu->cx--;
        if (cpu->z == 0) {
          while (cpu->cx != 0 && cpu->z == 0) {
            i8088_cmpsw(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    }
    break;

  case 0xA8: /* TEST AL,imm */
    i8088_trace_op_mnemonic("test");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    (void)i8088_and_8(cpu, cpu->al, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xA9: /* TEST AX,imm */
    i8088_trace_op_mnemonic("test");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    (void)i8088_and_16(cpu, cpu->ax, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xAA: /* STOSB */
    i8088_trace_op_mnemonic("stosb");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_stosb(cpu, mem);
      break;
    case REPEAT_EZ:
    case REPEAT_NENZ:
      i8088_trace_op_prefix("rep");
      while (cpu->cx != 0) {
        i8088_stosb(cpu, mem);
        cpu->cx--;
      }
      break;
    }
    break;

  case 0xAB: /* STOSW */
    i8088_trace_op_mnemonic("stosw");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_stosw(cpu, mem);
      break;
    case REPEAT_EZ:
    case REPEAT_NENZ:
      i8088_trace_op_prefix("rep");
      while (cpu->cx != 0) {
        i8088_stosw(cpu, mem);
        cpu->cx--;
      }
      break;
    }
    break;

  case 0xAC: /* LODSB */
    i8088_trace_op_mnemonic("lodsb");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_lodsb(cpu, mem);
      break;
    case REPEAT_EZ:
    case REPEAT_NENZ:
      i8088_trace_op_prefix("rep");
      while (cpu->cx != 0) {
        i8088_lodsb(cpu, mem);
        cpu->cx--;
      }
      break;
    }
    break;

  case 0xAD: /* LODSW */
    i8088_trace_op_mnemonic("lodsw");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_lodsw(cpu, mem);
      break;
    case REPEAT_EZ:
    case REPEAT_NENZ:
      i8088_trace_op_prefix("rep");
      while (cpu->cx != 0) {
        i8088_lodsw(cpu, mem);
        cpu->cx--;
      }
      break;
    }
    break;

  case 0xAE: /* SCASB */
    i8088_trace_op_mnemonic("scasb");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_scasb(cpu, mem);
      break;
    case REPEAT_EZ:
      if (cpu->cx != 0) {
        i8088_scasb(cpu, mem);
        cpu->cx--;
        if (cpu->z == 1) {
          while (cpu->cx != 0 && cpu->z == 1) {
            i8088_scasb(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    case REPEAT_NENZ:
      if (cpu->cx != 0) {
        i8088_scasb(cpu, mem);
        cpu->cx--;
        if (cpu->z == 0) {
          while (cpu->cx != 0 && cpu->z == 0) {
            i8088_scasb(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    }
    break;

  case 0xAF: /* SCASW */
    i8088_trace_op_mnemonic("scasw");
    switch (cpu->repeat) {
    case REPEAT_NONE:
      i8088_scasw(cpu, mem);
      break;
    case REPEAT_EZ:
      if (cpu->cx != 0) {
        i8088_scasw(cpu, mem);
        cpu->cx--;
        if (cpu->z == 1) {
          while (cpu->cx != 0 && cpu->z == 1) {
            i8088_scasw(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    case REPEAT_NENZ:
      if (cpu->cx != 0) {
        i8088_scasw(cpu, mem);
        cpu->cx--;
        if (cpu->z == 0) {
          while (cpu->cx != 0 && cpu->z == 0) {
            i8088_scasw(cpu, mem);
            cpu->cx--;
          }
        }
      }
      break;
    }
    break;

  case 0xB0: /* MOV AL,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "al");
    data_8 = fetch(cpu, mem);
    cpu->al = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB1: /* MOV CL,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "cl");
    data_8 = fetch(cpu, mem);
    cpu->cl = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB2: /* MOV DL,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "dl");
    data_8 = fetch(cpu, mem);
    cpu->dl = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB3: /* MOV BL,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "bl");
    data_8 = fetch(cpu, mem);
    cpu->bl = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB4: /* MOV AH,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "ah");
    data_8 = fetch(cpu, mem);
    cpu->ah = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB5: /* MOV CH,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "ch");
    data_8 = fetch(cpu, mem);
    cpu->ch = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB6: /* MOV DH,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "dh");
    data_8 = fetch(cpu, mem);
    cpu->dh = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB7: /* MOV BH,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "bh");
    data_8 = fetch(cpu, mem);
    cpu->bh = data_8;
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xB8: /* MOV AX,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "ax");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ax = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xB9: /* MOV CX,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "cx");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->cx = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xBA: /* MOV DX,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "dx");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->dx = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xBB: /* MOV BX,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "bx");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->bx = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xBC: /* MOV SP,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "sp");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->sp = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xBD: /* MOV BP,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "bp");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->bp = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xBE: /* MOV SI,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "si");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->si = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xBF: /* MOV DI,imm */
    i8088_trace_op_mnemonic("mov");
    i8088_trace_op_dst(false, "di");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->di = data_16;
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xC2: /* RET */
    i8088_trace_op_mnemonic("retn");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ip  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ip += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    cpu->sp += data_16;
    i8088_trace_op_dst(false, FMT_U, data_16);
    break;

  case 0xC3: /* RET */
    i8088_trace_op_mnemonic("retn");
    cpu->ip  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ip += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->sp += 2;
    break;

  case 0xC4: /* LES */
    i8088_trace_op_mnemonic("les");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_reg_16(cpu, modrm, data_16);
    cpu->es = modrm_get_rm_eaddr_16(cpu, mem, modrm, eaddr+2);
    i8088_trace_op_bit_size(32);
    break;

  case 0xC5: /* LDS */
    i8088_trace_op_mnemonic("lds");
    modrm = fetch(cpu, mem);
    data_16 = modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    modrm_set_reg_16(cpu, modrm, data_16);
    cpu->ds = modrm_get_rm_eaddr_16(cpu, mem, modrm, eaddr+2);
    i8088_trace_op_bit_size(32);
    break;

  case 0xC6: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    (void)modrm_get_rm_8(cpu, mem, modrm, &eaddr);
    data_8 = fetch(cpu, mem);
    modrm_set_rm_eaddr_8(cpu, mem, modrm, eaddr, data_8);
    i8088_trace_op_src(false, FMT_U, data_8);
    break;

  case 0xC7: /* MOV */
    i8088_trace_op_mnemonic("mov");
    modrm = fetch(cpu, mem);
    (void)modrm_get_rm_16(cpu, mem, modrm, &eaddr);
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    modrm_set_rm_eaddr_16(cpu, mem, modrm, eaddr, data_16);
    i8088_trace_op_src(false, FMT_U, data_16);
    break;

  case 0xCA: /* RET */
    i8088_trace_op_mnemonic("retf");
    data_16  = fetch(cpu, mem);
    data_16 += fetch(cpu, mem) * 0x100;
    cpu->ip  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ip += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->cs  = mem_read_by_segment(mem, cpu->ss, cpu->sp+2);
    cpu->cs += mem_read_by_segment(mem, cpu->ss, cpu->sp+3) * 0x100;
    cpu->sp += 4;
    cpu->sp += data_16;
    i8088_trace_op_dst(false, FMT_U, data_16);
    break;

  case 0xCB: /* RET */
    i8088_trace_op_mnemonic("retf");
    cpu->ip  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ip += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->cs  = mem_read_by_segment(mem, cpu->ss, cpu->sp+2);
    cpu->cs += mem_read_by_segment(mem, cpu->ss, cpu->sp+3) * 0x100;
    cpu->sp += 4;
    break;

  case 0xCC: /* INT 3 */
    i8088_trace_op_mnemonic("int3");
    i8088_interrupt(cpu, mem, INT_1_BYTE);
    break;

  case 0xCD: /* INT */
    i8088_trace_op_mnemonic("int");
    data_8 = fetch(cpu, mem);
    i8088_interrupt(cpu, mem, data_8);
    i8088_trace_op_dst(false, FMT_U, data_8);
    break;

  case 0xCE: /* INTO */
    i8088_trace_op_mnemonic("into");
    if (cpu->o) {
      i8088_interrupt(cpu, mem, INT_OVERFLOW);
    }
    break;

  case 0xCF: /* IRET */
    i8088_trace_op_mnemonic("iret");
    cpu->ip  = mem_read_by_segment(mem, cpu->ss, cpu->sp);
    cpu->ip += mem_read_by_segment(mem, cpu->ss, cpu->sp+1) * 0x100;
    cpu->cs  = mem_read_by_segment(mem, cpu->ss, cpu->sp+2);
    cpu->cs += mem_read_by_segment(mem, cpu->ss, cpu->sp+3) * 0x100;
    cpu->flags  = mem_read_by_segment(mem, cpu->ss, cpu->sp+4);
    cpu->flags += mem_read_by_segment(mem, cpu->ss, cpu->sp+5) * 0x100;
    cpu->sp += 6;
    cpu->flags |=  0b1111000000000010; /* Set some unused flags. */
    cpu->flags &= ~0b0000000000101000; /* Reset some unused flags. */
    break;

  case 0xD0:
    i8088_opcode_d0_d2(cpu, mem, 1);
    i8088_trace_op_src(false, "");
    break;

  case 0xD1:
    i8088_opcode_d1_d3(cpu, mem, 1);
    i8088_trace_op_src(false, "");
    break;

  case 0xD2:
    i8088_opcode_d0_d2(cpu, mem, cpu->cl);
    i8088_trace_op_src(false, "cl");
    break;

  case 0xD3:
    i8088_opcode_d1_d3(cpu, mem, cpu->cl);
    i8088_trace_op_src(false, "cl");
    break;

  case 0xD4: /* AAM */
    i8088_trace_op_mnemonic("aam");
    data_8 = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_U, data_8);
    if (data_8 == 0) {
      i8088_interrupt(cpu, mem, INT_DIVIDE_ERROR);
      return;
    }
    cpu->ah = cpu->al / data_8;
    cpu->al = cpu->al % data_8;
    cpu->p = parity_even(cpu->al);
    cpu->s = cpu->al >> 7;
    cpu->z = cpu->al == 0;
    break;

  case 0xD5: /* AAD */
    i8088_trace_op_mnemonic("aad");
    data_8 = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_U, data_8);
    cpu->al = (cpu->ah * data_8) + cpu->al;
    cpu->ah = 0;
    cpu->p = parity_even(cpu->al);
    cpu->s = cpu->al >> 7;
    cpu->z = cpu->al == 0;
    break;

  case 0xD7:
    i8088_trace_op_mnemonic("xlat");
    cpu->al = eaddr_read_8(cpu, mem, cpu->ds, cpu->bx + cpu->al, NULL);
    break;

  case 0xD8:
  case 0xD9:
  case 0xDA:
  case 0xDB:
  case 0xDC:
  case 0xDD:
  case 0xDE:
  case 0xDF:
    i8088_trace_op_mnemonic("esc");
    /* This is needed to advance the instruction pointer correctly. */
    modrm = fetch(cpu, mem);
    modrm_void_rm_16(cpu, mem, modrm);
    break;

  case 0xE0: /* LOOPNE imm */
    i8088_trace_op_mnemonic("loopne");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    cpu->cx--;
    if ((cpu->z == 0) && (cpu->cx != 0)) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0xE1: /* LOOPE imm */
    i8088_trace_op_mnemonic("loope");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    cpu->cx--;
    if ((cpu->z == 1) && (cpu->cx != 0)) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0xE2: /* LOOP imm */
    i8088_trace_op_mnemonic("loop");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    cpu->cx--;
    if (cpu->cx != 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0xE3: /* JCXZ */
    i8088_trace_op_mnemonic("jcxz");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    if (cpu->cx == 0) {
      cpu->ip = cpu->ip + disp;
    }
    break;

  case 0xE4: /* IN AL,imm */
    i8088_trace_op_mnemonic("in");
    data_8 = fetch(cpu, mem);
    i8088_trace_op_dst(false, "al");
    i8088_trace_op_src(false, FMT_U, data_8);
    cpu->al = io_read(cpu->io, data_8);
    break;

  case 0xE5: /* IN AX,imm */
    i8088_trace_op_mnemonic("in");
    data_8 = fetch(cpu, mem);
    i8088_trace_op_dst(false, "ax");
    i8088_trace_op_src(false, FMT_U, data_8);
    cpu->al = io_read(cpu->io, data_8);
    cpu->ah = io_read(cpu->io, data_8+1);
    break;

  case 0xE6: /* OUT imm,AL */
    i8088_trace_op_mnemonic("out");
    data_8 = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_U, data_8);
    i8088_trace_op_src(false, "al");
    io_write(cpu->io, data_8, cpu->al);
    break;

  case 0xE7: /* OUT imm,AX */
    i8088_trace_op_mnemonic("out");
    data_8 = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_U, data_8);
    i8088_trace_op_src(false, "ax");
    io_write(cpu->io, data_8, cpu->al);
    io_write(cpu->io, data_8+1, cpu->ah);
    break;

  case 0xE8: /* CALL */
    i8088_trace_op_mnemonic("call");
    offset  = fetch(cpu, mem);
    offset += fetch(cpu, mem) * 0x100;
    cpu->sp -= 2;
    mem_write_by_segment(mem, cpu->ss, cpu->sp,   cpu->ip % 0x100);
    mem_write_by_segment(mem, cpu->ss, cpu->sp+1, cpu->ip / 0x100);
    cpu->ip += offset;
    i8088_trace_op_dst(false, FMT_S,
      offset + 3 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    break;

  case 0xE9: /* JMP */
    i8088_trace_op_mnemonic("jmp");
    offset  = fetch(cpu, mem);
    offset += fetch(cpu, mem) * 0x100;
    cpu->ip += offset;
    i8088_trace_op_dst(false, FMT_S,
      offset + 3 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    break;

  case 0xEA: /* JMP */
    i8088_trace_op_mnemonic("jmpf");
    offset  = fetch(cpu, mem);
    offset += fetch(cpu, mem) * 0x100;
    segment  = fetch(cpu, mem);
    segment += fetch(cpu, mem) * 0x100;
    cpu->ip = offset;
    cpu->cs = segment;
    i8088_trace_op_dst(false, FMT_S ":" FMT_S, segment, offset);
    break;

  case 0xEB: /* JMP */
    i8088_trace_op_mnemonic("jmp");
    disp = fetch(cpu, mem);
    i8088_trace_op_dst(false, FMT_S,
      disp + 2 + ((cpu->segment_override == SEGMENT_NONE) ? 0 : 1));
    cpu->ip = cpu->ip + disp;
    break;

  case 0xEC: /* IN AL,DX */
    i8088_trace_op_mnemonic("in");
    i8088_trace_op_dst(false, "al");
    i8088_trace_op_src(false, "dx");
    cpu->al = io_read(cpu->io, cpu->dx);
    break;

  case 0xED: /* IN AX,DX */
    i8088_trace_op_mnemonic("in");
    i8088_trace_op_dst(false, "ax");
    i8088_trace_op_src(false, "dx");
    cpu->al = io_read(cpu->io, cpu->dx);
    cpu->ah = io_read(cpu->io, cpu->dx+1);
    break;

  case 0xEE: /* OUT DX,AL */
    i8088_trace_op_mnemonic("out");
    i8088_trace_op_dst(false, "dx");
    i8088_trace_op_src(false, "al");
    io_write(cpu->io, cpu->dx, cpu->al);
    break;

  case 0xEF: /* OUT DX,AX */
    i8088_trace_op_mnemonic("out");
    i8088_trace_op_dst(false, "dx");
    i8088_trace_op_src(false, "ax");
    io_write(cpu->io, cpu->dx, cpu->al);
    io_write(cpu->io, cpu->dx+1, cpu->ah);
    break;

  case 0xF4: /* HLT */
    i8088_trace_op_mnemonic("hlt");
    cpu->halt = true;
    break;

  case 0xF5: /* CMC */
    i8088_trace_op_mnemonic("cmc");
    cpu->c = !cpu->c;
    break;

  case 0xF6:
    i8088_opcode_f6(cpu, mem);
    break;

  case 0xF7:
    i8088_opcode_f7(cpu, mem);
    break;

  case 0xF8: /* CLC */
    i8088_trace_op_mnemonic("clc");
    cpu->c = 0;
    break;

  case 0xF9: /* STC */
    i8088_trace_op_mnemonic("stc");
    cpu->c = 1;
    break;

  case 0xFA: /* CLI */
    i8088_trace_op_mnemonic("cli");
    cpu->i = 0;
    break;

  case 0xFB: /* STI */
    i8088_trace_op_mnemonic("sti");
    cpu->i = 1;
    break;

  case 0xFC: /* CLD */
    i8088_trace_op_mnemonic("cld");
    cpu->d = 0;
    break;

  case 0xFD: /* STD */
    i8088_trace_op_mnemonic("std");
    cpu->d = 1;
    break;

  case 0xFE:
    i8088_opcode_fe(cpu, mem);
    break;

  case 0xFF:
    i8088_opcode_ff(cpu, mem);
    break;

  default:
    panic("Unhandled opcode: 0x%02x\n", opcode);
    break;
  }

  i8088_trace_end();
}



