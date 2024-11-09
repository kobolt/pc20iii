#ifndef _FE2010_H
#define _FE2010_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "i8088.h"
#include "mem.h"
#include "io.h"

typedef struct pit_s {
  union {
    struct {
      uint8_t bcd  : 1;
      uint8_t mode : 3;
      uint8_t rl   : 2;
      uint8_t sc   : 2;
    };
    uint8_t control;
  };

  union {
    struct {
      uint8_t counter_lsb;
      uint8_t counter_msb;
    };
    uint16_t counter;
  };

  union {
    struct {
      uint8_t latch_lsb;
      uint8_t latch_msb;
    };
    uint16_t latch;
  };

  bool flip_flop;
  bool timer_hack;
} pit_t;

typedef struct fe2010_s {
  uint8_t ctrl; /* Control Register */
  uint8_t conf; /* Configuration Register */
  uint8_t scancode;
  uint8_t switches;
  bool timer_2_output;

  uint16_t dma_reg[8];
  bool dma_flip_flop;
  uint8_t dma_page[4];
  uint8_t dma_mode[4];

  uint8_t irq_mask;
  uint8_t nmi_mask;
  bool irq_pending[8];

  pit_t pit[3];

  i8088_t *cpu;
  mem_t *mem;
} fe2010_t;

#define FE2010_IRQ_TIMER       0
#define FE2010_IRQ_KEYBOARD    1
#define FE2010_IRQ_MOUSE       2
#define FE2010_IRQ_COM2        3
#define FE2010_IRQ_COM1        4
#define FE2010_IRQ_HARD_DISK   5
#define FE2010_IRQ_FLOPPY_DISK 6
#define FE2010_IRQ_LPT1        7

#define FE2010_DMA_FLOPPY_DISK 2
#define FE2010_DMA_HARD_DISK   3

void fe2010_init(fe2010_t *fe2010, io_t *io, i8088_t *cpu, mem_t *mem);
void fe2010_execute(fe2010_t *fe2010);
void fe2010_irq(fe2010_t *fe2010, int irq_no);
void fe2010_dma_write(fe2010_t *fe2010, int channel_no,
  uint8_t (*callback_func)(void *), void *callback_data);
void fe2010_dma_read(fe2010_t *fe2010, int channel_no,
  void (*callback_func)(void *, uint8_t), void *callback_data);
void fe2010_keyboard_press(fe2010_t *fe2010, int scancode);
void fe2010_dump(FILE *fh, fe2010_t *fe2010);

#endif /* _FE2010_H */
