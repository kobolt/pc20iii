#ifndef _M6242_H
#define _M6242_H

#include <stdbool.h>
#include <stdint.h>
#include "io.h"

typedef struct m6242_s {
  bool bios_probe;
  uint8_t control_d;
  uint8_t control_e;
  uint8_t control_f;
} m6242_t;

void m6242_init(m6242_t *m6242, io_t *io);

#endif /* _M6242_H */
