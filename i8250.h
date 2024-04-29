#ifndef _I8250_H
#define _I8250_H

#include <stdint.h>
#include <stdio.h>
#include "io.h"
#include "fe2010.h"
#include "mos5720.h"

#define I8250_RX_FIFO_SIZE 1024
#define I8250_TX_FIFO_SIZE 1024

typedef struct i8250_s {
  uint8_t ier;
  uint8_t iir;
  uint8_t lcr;
  uint8_t mcr;
  uint8_t lsr;
  uint8_t msr;
  uint8_t scratch;

  union {
    struct {
      uint8_t divisor_low;
      uint8_t divisor_high;
    };
    uint16_t divisor;
  };

  int tty_fd;
  uint8_t rx_fifo[I8250_RX_FIFO_SIZE];
  uint8_t tx_fifo[I8250_TX_FIFO_SIZE];
  int rx_fifo_head;
  int tx_fifo_head;
  int rx_fifo_tail;
  int tx_fifo_tail;

  fe2010_t* fe2010;
  mos5720_t* mos5720;
} i8250_t;

int i8250_init(i8250_t *i8250, io_t *io, fe2010_t *fe2010,
  mos5720_t *mos5720, const char *tty_device);
void i8250_execute(i8250_t *i8250);
void i8250_trace_dump(FILE *fh);

#endif /* _I8250_H */
