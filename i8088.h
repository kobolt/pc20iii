#ifndef _I8088_H
#define _I8088_H

#include <stdint.h>
#include <stdbool.h>
#include "mem.h"
#include "io.h"

typedef enum {
  SEGMENT_NONE = 0,
  SEGMENT_CS,
  SEGMENT_DS,
  SEGMENT_ES,
  SEGMENT_SS,
} segment_t;

typedef enum {
  REPEAT_NONE = 0,
  REPEAT_EZ,
  REPEAT_NENZ,
} repeat_t;

typedef struct i8088_s {
  uint16_t es; /* Extra Segment */
  uint16_t cs; /* Code Segment */
  uint16_t ss; /* Stack Segment */
  uint16_t ds; /* Data Segment */

  uint16_t ip; /* Instruction Pointer */
  uint16_t sp; /* Stack Pointer */
  uint16_t bp; /* Base Pointer */
  uint16_t si; /* Source Index */
  uint16_t di; /* Destination Index */

  union {
    struct {
      uint8_t al;
      uint8_t ah;
    };
    uint16_t ax; /* Accumulator */
  };

  union {
    struct {
      uint8_t bl;
      uint8_t bh;
    };
    uint16_t bx; /* Base */
  };

  union {
    struct {
      uint8_t cl;
      uint8_t ch;
    };
    uint16_t cx; /* Counter */
  };

  union {
    struct {
      uint8_t dl;
      uint8_t dh;
    };
    uint16_t dx; /* Data */
  };

  union {
    struct {
      uint16_t c  : 1; /* Carry */
      uint16_t x1 : 1;
      uint16_t p  : 1; /* Parity */
      uint16_t x2 : 1;
      uint16_t a  : 1; /* Auxiliary Carry */
      uint16_t x3 : 1;
      uint16_t z  : 1; /* Zero */
      uint16_t s  : 1; /* Sign */
      uint16_t t  : 1; /* Trap */
      uint16_t i  : 1; /* Interrupt Enable */
      uint16_t d  : 1; /* Direction */
      uint16_t o  : 1; /* Overflow */
      uint16_t x4 : 4;
    };
    uint16_t flags;
  };

  segment_t segment_override;
  repeat_t repeat;
  bool halt;

  io_t *io;
} i8088_t;

#define MOD_DISP_ZERO      0b00
#define MOD_DISP_LO_SIGN   0b01
#define MOD_DISP_HI_LO     0b10
#define MOD_REGISTER       0b11

#define REG8_AL   0b000
#define REG8_CL   0b001
#define REG8_DL   0b010
#define REG8_BL   0b011
#define REG8_AH   0b100
#define REG8_CH   0b101
#define REG8_DH   0b110
#define REG8_BH   0b111

#define REG16_AX   0b000
#define REG16_CX   0b001
#define REG16_DX   0b010
#define REG16_BX   0b011
#define REG16_SP   0b100
#define REG16_BP   0b101
#define REG16_SI   0b110
#define REG16_DI   0b111

#define REGSEG_ES   0b00
#define REGSEG_CS   0b01
#define REGSEG_SS   0b10
#define REGSEG_DS   0b11

#define EADDR_BX_SI   0b000
#define EADDR_BX_DI   0b001
#define EADDR_BP_SI   0b010
#define EADDR_BP_DI   0b011
#define EADDR_SI      0b100
#define EADDR_DI      0b101
#define EADDR_BP      0b110
#define EADDR_BX      0b111

#define modrm_mod(x) (x >> 6)              /* MOD field in ModRM. */
#define modrm_reg(x) ((x >> 3) & 0b111)    /* REG field in ModRM. */
#define modrm_opcode(x) ((x >> 3) & 0b111) /* OPCODE field in ModRM. */
#define modrm_rm(x)  (x & 0b111)           /* R/M field in ModRM. */

void i8088_reset(i8088_t *cpu);
bool i8088_irq(i8088_t *cpu, mem_t *mem, int irq_no);
void i8088_init(i8088_t *cpu, io_t *io);
void i8088_execute(i8088_t *cpu, mem_t *mem);

#endif /* _I8088_H */
