#include "mos5720.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "io.h"
#include "fe2010.h"
#include "panic.h"

#define MOS5720_MODE    0x230
#define MOS5720_REG_232 0x232 /* Undocumented register */

#define MOS5720_MOUSE_DATA      0x23C
#define MOS5720_MOUSE_SIGNATURE 0x23D
#define MOS5720_MOUSE_CONTROL   0x23E
#define MOS5720_MOUSE_CONFIG    0x23F



static uint8_t mos5720_mode_read(void *mos5720, uint16_t port)
{
  (void)port;
  return ((mos5720_t *)mos5720)->mode;
}



static void mos5720_mode_write(void *mos5720, uint16_t port, uint8_t value)
{
  (void)port;
  ((mos5720_t *)mos5720)->mode = value;
}



static void mos5720_reg_232_write(void *mos5720, uint16_t port, uint8_t value)
{
  (void)port;
  /* Hack to detect onboard mouse in BIOS startup. */
  if (value == 0x99 && ((mos5720_t *)mos5720)->mode == 0x89) {
    ((mos5720_t *)mos5720)->mouse_signature = 0;
  }
}



static uint8_t mos5720_mouse_read(void *mos5720, uint16_t port)
{
  switch (port) {
  case MOS5720_MOUSE_DATA:
    return ((mos5720_t *)mos5720)->mouse_data;

  case MOS5720_MOUSE_SIGNATURE:
    return ((mos5720_t *)mos5720)->mouse_signature;

  case MOS5720_MOUSE_CONTROL:
    /* Toggle between 0x07 and 0x0F for MOS5720 behavior. */
    if (((mos5720_t *)mos5720)->mouse_control == 0x07) {
      ((mos5720_t *)mos5720)->mouse_control = 0x0F;
    } else {
      ((mos5720_t *)mos5720)->mouse_control = 0x07;
    }
    return ((mos5720_t *)mos5720)->mouse_control;

  case MOS5720_MOUSE_CONFIG:
    return 0xFF;

  default:
    break;
  }

  return 0xFF;
}



static void mos5720_mouse_write(void *mos5720, uint16_t port,
  uint8_t value)
{
  switch (port) {
  case MOS5720_MOUSE_DATA:
    break;

  case MOS5720_MOUSE_SIGNATURE:
    ((mos5720_t *)mos5720)->mouse_signature = value;
    break;

  case MOS5720_MOUSE_CONTROL:
    break;

  case MOS5720_MOUSE_CONFIG:
    break;

  default:
    break;
  }
}



void mos5720_init(mos5720_t *mos5720, io_t *io, fe2010_t *fe2010)
{
  memset(mos5720, 0, sizeof(mos5720_t));
  mos5720->fe2010 = fe2010;

  io->read[MOS5720_MODE].func = mos5720_mode_read;
  io->read[MOS5720_MODE].cookie = mos5720;
  io->write[MOS5720_MODE].func = mos5720_mode_write;
  io->write[MOS5720_MODE].cookie = mos5720;

  io->write[MOS5720_REG_232].func = mos5720_reg_232_write;
  io->write[MOS5720_REG_232].cookie = mos5720;

  io->read[MOS5720_MOUSE_DATA].func = mos5720_mouse_read;
  io->read[MOS5720_MOUSE_DATA].cookie = mos5720;
  io->read[MOS5720_MOUSE_SIGNATURE].func = mos5720_mouse_read;
  io->read[MOS5720_MOUSE_SIGNATURE].cookie = mos5720;
  io->read[MOS5720_MOUSE_CONTROL].func = mos5720_mouse_read;
  io->read[MOS5720_MOUSE_CONTROL].cookie = mos5720;
  io->read[MOS5720_MOUSE_CONFIG].func = mos5720_mouse_read;
  io->read[MOS5720_MOUSE_CONFIG].cookie = mos5720;

  io->write[MOS5720_MOUSE_DATA].func = mos5720_mouse_write;
  io->write[MOS5720_MOUSE_DATA].cookie = mos5720;
  io->write[MOS5720_MOUSE_SIGNATURE].func = mos5720_mouse_write;
  io->write[MOS5720_MOUSE_SIGNATURE].cookie = mos5720;
  io->write[MOS5720_MOUSE_CONTROL].func = mos5720_mouse_write;
  io->write[MOS5720_MOUSE_CONTROL].cookie = mos5720;
  io->write[MOS5720_MOUSE_CONFIG].func = mos5720_mouse_write;
  io->write[MOS5720_MOUSE_CONFIG].cookie = mos5720;
}



void mos5720_mouse_data(mos5720_t *mos5720, uint8_t data)
{
  mos5720->mouse_data = data;
  fe2010_irq(mos5720->fe2010, FE2010_IRQ_MOUSE);
}



bool mos5720_uart_chip_select(mos5720_t *mos5720)
{
  if (((mos5720_t *)mos5720)->mode == 0x89) { /* Enabled by BIOS. */
    return true;
  } else if (((mos5720_t *)mos5720)->mode == 0x81) { /* Disabled by BIOS. */
    return false;
  } else if (((mos5720_t *)mos5720)->mode == 0xD9) { /* Enabled after boot. */
    return true;
  } else { /* Unknown behaviour. */
    return false;
  }
}



