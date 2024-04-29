#ifndef _MOS5720_H
#define _MOS5720_H

#include <stdint.h>
#include "io.h"
#include "fe2010.h"

typedef struct mos5720_s {
  uint8_t mode;
  uint8_t mouse_signature;
  uint8_t mouse_control;
  uint8_t mouse_data;

  fe2010_t *fe2010;
} mos5720_t;

void mos5720_init(mos5720_t *mos5720, io_t *io, fe2010_t *fe2010);
void mos5720_mouse_data(mos5720_t *mos5720, uint8_t data);
bool mos5720_uart_chip_select(mos5720_t *mos5720);

#endif /* _MOS5720_H */
