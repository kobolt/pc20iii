#include "io.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "panic.h"



uint8_t io_read(io_t *io, uint16_t port)
{
  if (io->read[port].func != NULL) {
    return (io->read[port].func)(io->read[port].cookie, port);
  } else {
    return 0xFF;
  }
}



void io_write(io_t *io, uint16_t port, uint8_t value)
{
  if (io->write[port].func != NULL) {
    (io->write[port].func)(io->write[port].cookie, port, value);
  }
}



void io_init(io_t *io)
{
  memset(io, 0, sizeof(io_t));
}



