#include "i8250.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>

#include "io.h"
#include "mos5720.h"
#include "panic.h"

#define I8250_TRACE_BUFFER_SIZE 256
#define I8250_TRACE_MAX 80

#define I8250_IO_BASE 0x3F8
#define I8250_THR (I8250_IO_BASE + 0) /* Transmitter Holding Register */
#define I8250_RBR (I8250_IO_BASE + 0) /* Recieve Buffer Register */
#define I8250_IER (I8250_IO_BASE + 1) /* Interrupt Enable Register */
#define I8250_IIR (I8250_IO_BASE + 2) /* Interrupt Identification Register */
#define I8250_FCR (I8250_IO_BASE + 2) /* FIFO Control Register */
#define I8250_LCR (I8250_IO_BASE + 3) /* Line Control Register */
#define I8250_MCR (I8250_IO_BASE + 4) /* Modem Control Register */
#define I8250_LSR (I8250_IO_BASE + 5) /* Line Status Register */
#define I8250_MSR (I8250_IO_BASE + 6) /* Modem Status Register */
#define I8250_SR  (I8250_IO_BASE + 7) /* Scratch Register */

/* Interrupt enable bit offsets. */
#define I8250_IER_RBR 0
#define I8250_IER_THR 1
#define I8250_IER_LSR 2
#define I8250_IER_MSR 3

/* Interrupt pending byte contents. */
#define I8250_IIR_NO_PENDING 1
#define I8250_IIR_MSR 0
#define I8250_IIR_THR 2
#define I8250_IIR_RBR 4
#define I8250_IIR_LSR 6

/* Line status flags. */
#define I8250_LSR_TRANSMIT_SHIFT_EMPTY   0x40
#define I8250_LSR_TRANSMIT_HOLDING_EMPTY 0x20
#define I8250_LSR_DATA_READY             0x01

/* Modem status flags. */
#define I8250_MSR_CARRIER_DETECT 0x80
#define I8250_MSR_DATA_SET_READY 0x20
#define I8250_MSR_CLEAR_TO_SEND  0x10

static char i8250_trace_buffer[I8250_TRACE_BUFFER_SIZE][I8250_TRACE_MAX];
static int i8250_trace_buffer_n = 0;



static void i8250_trace(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(i8250_trace_buffer[i8250_trace_buffer_n],
    I8250_TRACE_MAX, format, args);
  va_end(args);

  i8250_trace_buffer_n++;
  if (i8250_trace_buffer_n >= I8250_TRACE_BUFFER_SIZE) {
    i8250_trace_buffer_n = 0;
  }
}



static bool i8250_rx_fifo_read(i8250_t *i8250, uint8_t *byte)
{
  if (i8250->rx_fifo_tail == i8250->rx_fifo_head) {
    return false; /* Empty */
  }

  *byte = i8250->rx_fifo[i8250->rx_fifo_tail];
  i8250->rx_fifo_tail = (i8250->rx_fifo_tail + 1) % I8250_RX_FIFO_SIZE;

  return true;
}



static void i8250_rx_fifo_write(i8250_t *i8250, uint8_t byte)
{
  if (((i8250->rx_fifo_head + 1) % I8250_RX_FIFO_SIZE)
    == i8250->rx_fifo_tail) {
    return; /* Full */
  }

  i8250->rx_fifo[i8250->rx_fifo_head] = byte;
  i8250->rx_fifo_head = (i8250->rx_fifo_head + 1) % I8250_RX_FIFO_SIZE;
}



static bool i8250_tx_fifo_read(i8250_t *i8250, uint8_t *byte)
{
  if (i8250->tx_fifo_tail == i8250->tx_fifo_head) {
    return false; /* Empty */
  }

  *byte = i8250->tx_fifo[i8250->tx_fifo_tail];
  i8250->tx_fifo_tail = (i8250->tx_fifo_tail + 1) % I8250_TX_FIFO_SIZE;

  return true;
}



static void i8250_tx_fifo_write(i8250_t *i8250, uint8_t byte)
{
  if (((i8250->tx_fifo_head + 1) % I8250_TX_FIFO_SIZE)
    == i8250->tx_fifo_tail) {
    return; /* Full */
  }

  i8250->tx_fifo[i8250->tx_fifo_head] = byte;
  i8250->tx_fifo_head = (i8250->tx_fifo_head + 1) % I8250_TX_FIFO_SIZE;
}



static void i8250_update_tty_settings(i8250_t *i8250)
{
  struct termios tios;
  speed_t speed;

  switch (i8250->divisor) {
  case 2304:
    speed = B50;
    break;
  case 1047:
    speed = B110;
    break;
  case 384:
    speed = B300;
    break;
  case 192:
    speed = B600;
    break;
  case 96:
    speed = B1200;
    break;
  case 48:
    speed = B2400;
    break;
  case 24:
    speed = B4800;
    break;
  case 12:
    speed = B9600;
    break;
  case 6:
    speed = B19200;
    break;
  case 3:
    speed = B38400;
    break;
  case 2:
    speed = B57600;
    break;
  case 1:
    speed = B115200;
    break;
  default:
    /* Invalid baudrate, just ignore it. */
    return;
  }

  if (tcgetattr(i8250->tty_fd, &tios) == -1) {
    panic("tcgetattr() failed with errno: %d\n", errno);
    return;
  }

  cfmakeraw(&tios);
  tios.c_cflag &= ~(CSIZE | PARENB | PARODD);

  /* Data Bits */
  switch (i8250->lcr & 0x3) {
  case 0b00:
    tios.c_cflag |= CS5;
    break;
  case 0b01:
    tios.c_cflag |= CS6;
    break;
  case 0b10:
    tios.c_cflag |= CS7;
    break;
  case 0b11:
  default:
    tios.c_cflag |= CS8;
    break;
  }

  /* Parity */
  switch ((i8250->lcr >> 3) & 0x3) {
  case 0b01:
    tios.c_cflag |= PARENB;
    tios.c_cflag |= PARODD;
    break;
  case 0b11:
    tios.c_cflag |= PARENB;
    break;
  case 0b10:
  case 0b00:
  default:
    break;
  }

  cfsetispeed(&tios, speed);
  cfsetospeed(&tios, speed);

  if (tcsetattr(i8250->tty_fd, TCSANOW, &tios) == -1) {
    panic("tcsetattr() failed with errno: %d\n", errno);
    return;
  }
}



static uint8_t i8250_register_read(void *i8250, uint16_t port)
{
  uint8_t value;

  if (mos5720_uart_chip_select(((i8250_t *)i8250)->mos5720) == false) {
    return false;
  }

  switch (port) {
  case I8250_RBR:
    if (((i8250_t *)i8250)->lcr >> 7) { /* Divisor Latch Access Bit. */
      i8250_trace("DLL read:  0x%02x\n", ((i8250_t *)i8250)->divisor_low);
      return ((i8250_t *)i8250)->divisor_low;

    } else {
      if (((i8250_t *)i8250)->iir == I8250_IIR_RBR) {
        /* Clear RBR interrupt when reading RBR register. */
        ((i8250_t *)i8250)->iir = I8250_IIR_NO_PENDING;
      }
      ((i8250_t *)i8250)->lsr &= ~I8250_LSR_DATA_READY;
      if (i8250_rx_fifo_read(i8250, &value)) {
        i8250_trace("<<< %02x\n", value);
        return value;
      } else {
        i8250_trace("RBR read:  empty\n");
        return 0;
      }
    }

  case I8250_IER:
    if (((i8250_t *)i8250)->lcr >> 7) { /* Divisor Latch Access Bit. */
      i8250_trace("DLH read:  0x%02x\n", ((i8250_t *)i8250)->divisor_high);
      return ((i8250_t *)i8250)->divisor_high;

    } else {
      i8250_trace("IER read:  0x%02x\n", ((i8250_t *)i8250)->ier);
      return ((i8250_t *)i8250)->ier;
    }

  case I8250_IIR:
    value = ((i8250_t *)i8250)->iir;
    if (((i8250_t *)i8250)->iir == I8250_IIR_THR) {
      /* Clear THR interrupt when reading IIR register. */
      ((i8250_t *)i8250)->iir = I8250_IIR_NO_PENDING;
    }
    i8250_trace("IIR read:  0x%02x\n", value);
    return value;

  case I8250_LCR:
    i8250_trace("LCR read:  0x%02x\n", ((i8250_t *)i8250)->lcr);
    return ((i8250_t *)i8250)->lcr;

  case I8250_MCR:
    i8250_trace("MCR read:  0x%02x\n", ((i8250_t *)i8250)->mcr);
    return ((i8250_t *)i8250)->mcr;

  case I8250_SR:
    return ((i8250_t *)i8250)->scratch;

  case I8250_LSR:
    i8250_trace("LSR read:  0x%02x\n", ((i8250_t *)i8250)->lsr);
    return ((i8250_t *)i8250)->lsr;

  case I8250_MSR:
    i8250_trace("MSR read:  0x%02x\n", ((i8250_t *)i8250)->msr);
    return ((i8250_t *)i8250)->msr;

  default:
    break;
  }

  return 0;
}



static void i8250_register_write(void *i8250, uint16_t port, uint8_t value)
{
  if (mos5720_uart_chip_select(((i8250_t *)i8250)->mos5720) == false) {
    return;
  }

  switch (port) {
  case I8250_THR:
    if (((i8250_t *)i8250)->lcr >> 7) { /* Divisor Latch Access Bit */
      i8250_trace("DLL write: 0x%02x\n", value);
      ((i8250_t *)i8250)->divisor_low = value;
      i8250_update_tty_settings(i8250);

    } else {
      i8250_trace(">>> %02x\n", value);
      i8250_tx_fifo_write(i8250, value);

      /* Send interrupt for THR if enabled. */
      if ((((i8250_t *)i8250)->ier >> I8250_IER_THR) & 1) {
        ((i8250_t *)i8250)->iir = I8250_IIR_THR;
        fe2010_irq(((i8250_t *)i8250)->fe2010, FE2010_IRQ_COM1);
      }
    }
    break;

  case I8250_IER:
    if (((i8250_t *)i8250)->lcr >> 7) { /* Divisor Latch Access Bit */
      i8250_trace("DLH write: 0x%02x\n", value);
      ((i8250_t *)i8250)->divisor_high = value;
      i8250_update_tty_settings(i8250);

    } else {
      i8250_trace("IER write: 0x%02x\n", value);
      ((i8250_t *)i8250)->ier = value;

      /* Issue immedate interrupt for THR if enabled. */
      if ((((i8250_t *)i8250)->ier >> I8250_IER_THR) & 1) {
        ((i8250_t *)i8250)->iir = I8250_IIR_THR;
        fe2010_irq(((i8250_t *)i8250)->fe2010, FE2010_IRQ_COM1);
      }
    }
    break;

  case I8250_FCR:
    i8250_trace("FCR write: 0x%02x\n", value);
    /* Not used on 8250 UART. */
    break;

  case I8250_LCR:
    i8250_trace("LCR write: 0x%02x\n", value);
    ((i8250_t *)i8250)->lcr = value;
    i8250_update_tty_settings(i8250);
    break;

  case I8250_MCR:
    i8250_trace("MCR write: 0x%02x\n", value);
    ((i8250_t *)i8250)->mcr = value;
    break;

  case I8250_SR:
    ((i8250_t *)i8250)->scratch = value;
    break;

  default:
    break;
  }
}



int i8250_init(i8250_t *i8250, io_t *io, fe2010_t *fe2010,
  mos5720_t *mos5720, const char *tty_device)
{
  int i;

  memset(i8250, 0, sizeof(i8250_t));
  i8250->fe2010 = fe2010;
  i8250->mos5720 = mos5720;

  /* Initial values after reset. */
  i8250->iir = I8250_IIR_NO_PENDING;
  i8250->lsr = I8250_LSR_TRANSMIT_SHIFT_EMPTY |
               I8250_LSR_TRANSMIT_HOLDING_EMPTY;
  i8250->msr = I8250_MSR_CARRIER_DETECT |
               I8250_MSR_DATA_SET_READY |
               I8250_MSR_CLEAR_TO_SEND;

  for (i = I8250_IO_BASE; i <= I8250_SR; i++) {
    io->read[i].func = i8250_register_read;
    io->read[i].cookie = i8250;
    io->write[i].func = i8250_register_write;
    io->write[i].cookie = i8250;
  }

  for (i = 0; i < I8250_TRACE_BUFFER_SIZE; i++) {
    i8250_trace_buffer[i][0] = '\0';
  }
  i8250_trace_buffer_n = 0;

  i8250->tty_fd = open(tty_device, O_RDWR | O_NOCTTY);
  if (i8250->tty_fd == -1) {
    fprintf(stderr, "open() for '%s' failed with errno: %d\n",
      tty_device, errno);
    return -1;
  }
  /* NOTE: i8250->tty_fd is never closed! */

  return 0;
}



void i8250_execute(i8250_t *i8250)
{
  struct pollfd fds[1];
  uint8_t byte;

  fds[0].fd = i8250->tty_fd;
  fds[0].events = POLLIN;

  /* Non-blocking read. */
  if (poll(fds, 1, 0) > 0) {
    if (read(i8250->tty_fd, &byte, 1) == 1) {
      i8250_rx_fifo_write(i8250, byte);
      i8250->lsr |= I8250_LSR_DATA_READY;

      /* Send interrupt for RBR if enabled. */
      if ((i8250->ier >> I8250_IER_RBR) & 1) {
        i8250->iir = I8250_IIR_RBR;
        fe2010_irq(i8250->fe2010, FE2010_IRQ_COM1);
      }
    }
  }

  /* Blocking write. */
  if (i8250_tx_fifo_read(i8250, &byte)) {
    write(i8250->tty_fd, &byte, 1);
  }
}



void i8250_trace_dump(FILE *fh)
{
  int i;

  for (i = i8250_trace_buffer_n; i < I8250_TRACE_BUFFER_SIZE; i++) {
    if (i8250_trace_buffer[i][0] != '\0') {
      fprintf(fh, i8250_trace_buffer[i]);
    }
  }
  for (i = 0; i < i8250_trace_buffer_n; i++) {
    if (i8250_trace_buffer[i][0] != '\0') {
      fprintf(fh, i8250_trace_buffer[i]);
    }
  }
}



