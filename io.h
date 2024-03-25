#ifndef _IO_H
#define _IO_H

#include <stdint.h>

typedef struct io_read_hook_s {
  void *cookie;
  uint8_t (*func)(void *, uint16_t);
} io_read_hook_t;

typedef struct io_write_hook_s {
  void *cookie;
  void (*func)(void *, uint16_t, uint8_t);
} io_write_hook_t;

typedef struct io_s {
  io_read_hook_t  read[UINT16_MAX + 1];
  io_write_hook_t write[UINT16_MAX + 1];
} io_t;

uint8_t io_read(io_t *io, uint16_t port);
void io_write(io_t *io, uint16_t port, uint8_t value);
void io_init(io_t *io);

#endif /* _IO_H */
