#include "m6242.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "io.h"

#define M6242_S1   0x2C0
#define M6242_S10  0x2C1
#define M6242_MI1  0x2C2
#define M6242_MI10 0x2C3
#define M6242_H1   0x2C4
#define M6242_H10  0x2C5
#define M6242_D1   0x2C6
#define M6242_D10  0x2C7
#define M6242_MO1  0x2C8
#define M6242_MO10 0x2C9
#define M6242_Y1   0x2CA
#define M6242_Y10  0x2CB
#define M6242_W    0x2CC
#define M6242_CD   0x2CD
#define M6242_CE   0x2CE
#define M6242_CF   0x2CF



static uint8_t m6242_register_read(void *m6242, uint16_t port)
{
  struct timespec tp;
  struct tm tm;

  clock_gettime(CLOCK_REALTIME, &tp);
  localtime_r(&tp.tv_sec, &tm);

  switch (port) {
  case M6242_S1:
    /* NOTE: This is a hack to make the BIOS detect the RTC. */
    /* It expects the least significant digit of the S1 register to change. */
    if (((m6242_t *)m6242)->bios_probe == false) {
      ((m6242_t *)m6242)->bios_probe = true;
      return (tm.tm_sec - 2) % 10;
    } else {
      return tm.tm_sec % 10;
    }
  case M6242_S10:
    return tm.tm_sec / 10;
  case M6242_MI1:
    return tm.tm_min % 10;
  case M6242_MI10:
    return tm.tm_min / 10;
  case M6242_H1:
    return tm.tm_hour % 10;
  case M6242_H10:
    return tm.tm_hour / 10;
  case M6242_D1:
    return tm.tm_mday % 10;
  case M6242_D10:
    return tm.tm_mday / 10;
  case M6242_MO1:
    return (tm.tm_mon + 1) % 10;
  case M6242_MO10:
    return (tm.tm_mon + 1) / 10;
  case M6242_Y1:
    return tm.tm_year % 10;
  case M6242_Y10:
    return ((tm.tm_year / 10) + 2) % 10; /* Counts from 1980. */
  case M6242_W:
    return tm.tm_wday;
  case M6242_CD:
    return ((m6242_t *)m6242)->control_d & 0b1101; /* Clear BUSY bit. */
  case M6242_CE: 
    return ((m6242_t *)m6242)->control_e;
  case M6242_CF:
    return ((m6242_t *)m6242)->control_f;
  default:
    break;
  }

  return 0;
}



static void m6242_register_write(void *m6242, uint16_t port, uint8_t value)
{
  switch (port) {
  case M6242_CD:
    ((m6242_t *)m6242)->control_d = value;
    break;
  case M6242_CE: 
    ((m6242_t *)m6242)->control_e = value;
    break;
  case M6242_CF:
    ((m6242_t *)m6242)->control_f = value;
    break;
  default:
    break;
  }
}



void m6242_init(m6242_t *m6242, io_t *io)
{
  int i;

  memset(m6242, 0, sizeof(m6242_t));

  for (i = M6242_S1; i <= M6242_CF; i++) {
    io->read[i].func = m6242_register_read;
    io->read[i].cookie = m6242;
    io->write[i].func = m6242_register_write;
    io->write[i].cookie = m6242;
  }
}



