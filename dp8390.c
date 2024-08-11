#include "dp8390.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "net.h"
#include "fe2010.h"
#include "io.h"

#define DP8390_TRACE_BUFFER_SIZE 2048
#define DP8390_TRACE_MAX 80

#define DP8390_PAGE_SIZE 256

#define DP8390_IO_BASE 0x300
#define DP8390_DATA    (DP8390_IO_BASE + 0x10)
#define DP8390_DATA_16 (DP8390_IO_BASE + 0x11) /* 16-bit access */
#define DP8390_RESET   (DP8390_IO_BASE + 0x1F)

/* Page 0, Read */
#define DP8390_CR     (DP8390_IO_BASE + 0x0) /* Command */
#define DP8390_CLDA0  (DP8390_IO_BASE + 0x1) /* Current Local DMA Addr 0 */
#define DP8390_CLDA1  (DP8390_IO_BASE + 0x2) /* Current Local DMA Addr 1 */
#define DP8390_BNRY   (DP8390_IO_BASE + 0x3) /* Boundary Pointer */
#define DP8390_TSR    (DP8390_IO_BASE + 0x4) /* Transmit Status */
#define DP8390_NCR    (DP8390_IO_BASE + 0x5) /* Number of Collisions */
#define DP8390_FIFO   (DP8390_IO_BASE + 0x6) /* FIFO */
#define DP8390_ISR    (DP8390_IO_BASE + 0x7) /* Interrupt Status */
#define DP8390_CRDA0  (DP8390_IO_BASE + 0x8) /* Current Remote DMA Addr 0 */
#define DP8390_CRDA1  (DP8390_IO_BASE + 0x9) /* Current Remote DMA Addr 1 */
#define DP8390_RSR    (DP8390_IO_BASE + 0xC) /* Receive Status */
#define DP8390_CNTR0  (DP8390_IO_BASE + 0xD) /* Tally Counter 0 */
#define DP8390_CNTR1  (DP8390_IO_BASE + 0xE) /* Tally Counter 1 */
#define DP8390_CNTR2  (DP8390_IO_BASE + 0xF) /* Tally Counter 2 */

/* Page 0, Write */
#define DP8390_PSTART (DP8390_IO_BASE + 0x1) /* Page Start Register */
#define DP8390_PSTOP  (DP8390_IO_BASE + 0x2) /* Page Stop Register */
#define DP8390_TPSR   (DP8390_IO_BASE + 0x4) /* Transmit Page Start Addr */
#define DP8390_TBCR0  (DP8390_IO_BASE + 0x5) /* Transmit Byte Count 0 */
#define DP8390_TBCR1  (DP8390_IO_BASE + 0x6) /* Transmit Byte Count 1 */
#define DP8390_RSAR0  (DP8390_IO_BASE + 0x8) /* Remote Start Addr 0 */
#define DP8390_RSAR1  (DP8390_IO_BASE + 0x9) /* Remote Start Addr 1 */
#define DP8390_RBCR0  (DP8390_IO_BASE + 0xA) /* Remote Byte Count 0 */
#define DP8390_RBCR1  (DP8390_IO_BASE + 0xB) /* Remote Byte Count 1 */
#define DP8390_RCR    (DP8390_IO_BASE + 0xC) /* Receive Configuration */
#define DP8390_TCR    (DP8390_IO_BASE + 0xD) /* Transmit Configuration */
#define DP8390_DCR    (DP8390_IO_BASE + 0xE) /* Data Configuration */
#define DP8390_IMR    (DP8390_IO_BASE + 0xF) /* Interrupt Mask */

/* Page 1 */
#define DP8390_PAR0   (DP8390_IO_BASE + 0x1) /* Physical Addr 0 */
#define DP8390_PAR1   (DP8390_IO_BASE + 0x2) /* Physical Addr 1 */
#define DP8390_PAR2   (DP8390_IO_BASE + 0x3) /* Physical Addr 2 */
#define DP8390_PAR3   (DP8390_IO_BASE + 0x4) /* Physical Addr 3 */
#define DP8390_PAR4   (DP8390_IO_BASE + 0x5) /* Physical Addr 4 */
#define DP8390_PAR5   (DP8390_IO_BASE + 0x6) /* Physical Addr 5 */
#define DP8390_CURR   (DP8390_IO_BASE + 0x7) /* Current Page */
#define DP8390_MAR0   (DP8390_IO_BASE + 0x8) /* Multicast Addr 0 */
#define DP8390_MAR1   (DP8390_IO_BASE + 0x9) /* Multicast Addr 1 */
#define DP8390_MAR2   (DP8390_IO_BASE + 0xA) /* Multicast Addr 2 */
#define DP8390_MAR3   (DP8390_IO_BASE + 0xB) /* Multicast Addr 3 */
#define DP8390_MAR4   (DP8390_IO_BASE + 0xC) /* Multicast Addr 4 */
#define DP8390_MAR5   (DP8390_IO_BASE + 0xD) /* Multicast Addr 5 */
#define DP8390_MAR6   (DP8390_IO_BASE + 0xE) /* Multicast Addr 6 */
#define DP8390_MAR7   (DP8390_IO_BASE + 0xF) /* Multicast Addr 7 */

#define dp8390_page(x) (((dp8390_t *)x)->cr >> 6)

static char dp8390_trace_buffer[DP8390_TRACE_BUFFER_SIZE][DP8390_TRACE_MAX];
static int dp8390_trace_buffer_n = 0;



static void dp8390_trace(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(dp8390_trace_buffer[dp8390_trace_buffer_n],
    DP8390_TRACE_MAX, format, args);
  va_end(args);

  dp8390_trace_buffer_n++;
  if (dp8390_trace_buffer_n >= DP8390_TRACE_BUFFER_SIZE) {
    dp8390_trace_buffer_n = 0;
  }
}



static void dp8390_transmit_packet(dp8390_t *dp8390)
{
  uint16_t i;
  uint8_t tx_frame[NET_MTU];

  dp8390->tsr = 0x1; /* Packet transmitted. */

  /* Issue packet transmitted interrupt. */
  dp8390->isr |= 0x02;
  if ((dp8390->imr >> 1) & 1) {
    fe2010_irq(dp8390->fe2010, FE2010_IRQ_COM2);
  }

  for (i = 0; i < dp8390->tbcr; i++) {
    tx_frame[i] = dp8390->ring[(dp8390->tpsr + i) & 0xFFFF];
  }

  net_tx_frame(dp8390->net, tx_frame, dp8390->tbcr);
}



static void dp8390_register_write(void *dp8390, uint16_t port, uint8_t value)
{
  switch (port) {
  case DP8390_CR:
    dp8390_trace("Write: CR     < 0x%02x\n", value);
    ((dp8390_t *)dp8390)->cr = value & 0xFB; /* Do not set TXP bit. */

    /* Stop */
    if (value & 1) {
      ((dp8390_t *)dp8390)->isr |= 0x80; /* Set reset bit. */
    }

    /* Start */
    if ((value >> 1) & 1) {
      ((dp8390_t *)dp8390)->isr &= ~0x80; /* Clear reset bit. */
    }

    /* Transmit Packet */
    if ((value >> 2) & 1) {
      dp8390_transmit_packet(dp8390);
      /* There might be a packet ready already, process just in case! */
      dp8390_execute(dp8390);
    }

    /* Remote DMA Command */
    if ((value >> 3) & 0x7) {
      /* Issue remote DMA complete interrupt. */
      ((dp8390_t *)dp8390)->isr |= 0x40;
      if ((((dp8390_t *)dp8390)->imr >> 6) & 1) {
        fe2010_irq(((dp8390_t *)dp8390)->fe2010, FE2010_IRQ_COM2);
      }
    }
    break;

  case DP8390_PSTART:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: PSTART < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->pstart = value;
    }
    break;

  case DP8390_PSTOP:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: PSTOP  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->pstop = value;
    }
    break;

  case DP8390_BNRY:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: BNRY   < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->bnry = value;
    }
    break;

  case DP8390_ISR:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: ISR    < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->isr &= ~value;
    } else if (dp8390_page(dp8390) == 1) {
      dp8390_trace("Write: CURR   < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->curr = value;
    }
    break;

  case DP8390_TPSR:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: TPSR   < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->tpsr = value << 8;
    }
    break;

  case DP8390_TBCR0:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: TBCR0  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->tbcr = value |
        (((dp8390_t *)dp8390)->tbcr & 0xFF00);
    }
    break;

  case DP8390_TBCR1:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: TBCR1  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->tbcr = (value << 8) |
        (((dp8390_t *)dp8390)->tbcr & 0x00FF);
    }
    break;

  case DP8390_RSAR0:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: RSAR0  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->rsar = value |
        (((dp8390_t *)dp8390)->rsar & 0xFF00);
      ((dp8390_t *)dp8390)->crda = value |
        (((dp8390_t *)dp8390)->crda & 0xFF00);
    }
    break;

  case DP8390_RSAR1:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: RSAR1  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->rsar = (value << 8) |
        (((dp8390_t *)dp8390)->rsar & 0x00FF);
      ((dp8390_t *)dp8390)->crda = (value << 8) |
        (((dp8390_t *)dp8390)->crda & 0x00FF);
    }
    break;

  case DP8390_RBCR0:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: RBCR0  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->rbcr = value |
        (((dp8390_t *)dp8390)->rbcr & 0xFF00);
    }
    break;

  case DP8390_RBCR1:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: RBCR1  < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->rbcr = (value << 8) |
        (((dp8390_t *)dp8390)->rbcr & 0x00FF);
    }
    break;

  case DP8390_TCR:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: TCR    < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->tcr = value;
    }
    break;

  case DP8390_IMR:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Write: IMR    < 0x%02x\n", value);
      ((dp8390_t *)dp8390)->imr = value;
    }
    break;

  case DP8390_DATA:
  case DP8390_DATA_16:
    dp8390_trace("Write: DATA   < 0x%02x\n", value);
    ((dp8390_t *)dp8390)->ring[((dp8390_t *)dp8390)->crda] = value;
    ((dp8390_t *)dp8390)->crda++;
    if (((dp8390_t *)dp8390)->crda == (((dp8390_t *)dp8390)->pstop << 8)) {
      ((dp8390_t *)dp8390)->crda = ((dp8390_t *)dp8390)->pstart << 8;
    }
    break;

  case DP8390_RESET:
    dp8390_trace("Write: RESET  < 0x%02x\n", value);
    ((dp8390_t *)dp8390)->isr |= 0x80; /* Set reset bit. */
    break;

  default:
    dp8390_trace("Write: 0x%04x < 0x%02x\n", port, value);
    break;
  }
}



static uint8_t dp8390_register_read(void *dp8390, uint16_t port)
{
  uint8_t value;

  switch (port) {
  case DP8390_CR:
    dp8390_trace("Read:  CR     > 0x%02x\n", ((dp8390_t *)dp8390)->cr);
    return ((dp8390_t *)dp8390)->cr;

  case DP8390_CLDA0:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  CLDA0  > 0x%02x\n",
        ((dp8390_t *)dp8390)->clda & 0xFF);
      return ((dp8390_t *)dp8390)->clda & 0xFF;
    }
    break;

  case DP8390_CLDA1:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  CLDA1  > 0x%02x\n",
        ((dp8390_t *)dp8390)->clda >> 8);
      return ((dp8390_t *)dp8390)->clda >> 8;
    }
    break;

  case DP8390_BNRY:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  BNRY   > 0x%02x\n", ((dp8390_t *)dp8390)->bnry);
      return ((dp8390_t *)dp8390)->bnry;
    }
    break;

  case DP8390_TSR:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  TSR    > 0x%02x\n", ((dp8390_t *)dp8390)->tsr);
      return ((dp8390_t *)dp8390)->tsr;
    }
    break;

  case DP8390_ISR:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  ISR    > 0x%02x\n", ((dp8390_t *)dp8390)->isr);
      return ((dp8390_t *)dp8390)->isr;
    } else if (dp8390_page(dp8390) == 1) {
      dp8390_trace("Read:  CURR   > 0x%02x\n", ((dp8390_t *)dp8390)->curr);
      return ((dp8390_t *)dp8390)->curr;
    }
    break;

  case DP8390_CRDA0:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  CRDA0  > 0x%02x\n",
        ((dp8390_t *)dp8390)->crda & 0xFF);
      return ((dp8390_t *)dp8390)->crda & 0xFF;
    }
    break;

  case DP8390_CRDA1:
    if (dp8390_page(dp8390) == 0) {
      dp8390_trace("Read:  CRDA1  > 0x%02x\n",
        ((dp8390_t *)dp8390)->crda >> 8);
      return ((dp8390_t *)dp8390)->crda >> 8;
    }
    break;

  case DP8390_DATA:
  case DP8390_DATA_16:
    if (((((dp8390_t *)dp8390)->tcr >> 1) & 0x3) == 1) { /* Loopback Mode 1 */
      /* Return repeated MAC address. */
      value = NET_MAC_LOCAL;
    } else {
      value = ((dp8390_t *)dp8390)->ring[((dp8390_t *)dp8390)->crda];
      ((dp8390_t *)dp8390)->crda++;
      if (((dp8390_t *)dp8390)->crda == (((dp8390_t *)dp8390)->pstop << 8)) {
        ((dp8390_t *)dp8390)->crda = ((dp8390_t *)dp8390)->pstart << 8;
      }
    }
    dp8390_trace("Read:  DATA   > 0x%02x\n", value);
    return value;
  }

  dp8390_trace("Read:  0x%04x\n", port);
  return 0;
}



void dp8390_init(dp8390_t *dp8390, io_t *io, fe2010_t *fe2010,
  net_t *net)
{
  int i;

  memset(dp8390, 0, sizeof(dp8390_t));
  dp8390->fe2010 = fe2010;
  dp8390->net = net;

  for (i = DP8390_IO_BASE; i <= (DP8390_RESET); i++) {
    io->read[i].func = dp8390_register_read;
    io->read[i].cookie = dp8390;
    io->write[i].func = dp8390_register_write;
    io->write[i].cookie = dp8390;
  }

  for (i = 0; i < DP8390_TRACE_BUFFER_SIZE; i++) {
    dp8390_trace_buffer[i][0] = '\0';
  }
  dp8390_trace_buffer_n = 0;
}



static void dp8390_ring_rx(dp8390_t *dp8390, uint8_t byte)
{
  if (dp8390->clda == dp8390->bnry) {
    return;
  }
  dp8390->ring[dp8390->clda] = byte;
  dp8390->clda++;
  if (dp8390->clda == (dp8390->pstop << 8)) {
    dp8390->clda = dp8390->pstart << 8;
  }
}



void dp8390_execute(dp8390_t *dp8390)
{
  uint16_t i;
  uint16_t byte_count;
  uint8_t next_packet;

  if (dp8390->net->rx_ready) {
    dp8390->clda = dp8390->curr << 8;
    byte_count = dp8390->net->rx_len + 4;
    next_packet = dp8390->curr + (byte_count / DP8390_PAGE_SIZE) + 1;
    if (next_packet >= dp8390->pstop) {
      next_packet = dp8390->pstart + (next_packet - dp8390->pstop);
    }
    dp8390->curr = next_packet;

    dp8390_ring_rx(dp8390, 0x01);              /* Receive Status = OK */
    dp8390_ring_rx(dp8390, next_packet);       /* Next Packet Pointer */
    dp8390_ring_rx(dp8390, byte_count & 0xFF); /* Receive Byte Count 0 */
    dp8390_ring_rx(dp8390, byte_count >> 8);   /* Receive Byte Count 1 */
    for (i = 0; i < dp8390->net->rx_len; i++) {
      dp8390_ring_rx(dp8390, dp8390->net->rx_frame[i]);
    }

    /* Issue packet received interrupt. */
    dp8390->isr |= 0x01;
    if (dp8390->imr & 1) {
      fe2010_irq(dp8390->fe2010, FE2010_IRQ_COM2);
    }

    dp8390->net->rx_ready = false;
  }
}



void dp8390_trace_dump(FILE *fh)
{
  int i;

  for (i = dp8390_trace_buffer_n; i < DP8390_TRACE_BUFFER_SIZE; i++) {
    if (dp8390_trace_buffer[i][0] != '\0') {
      fprintf(fh, dp8390_trace_buffer[i]);
    }
  }
  for (i = 0; i < dp8390_trace_buffer_n; i++) {
    if (dp8390_trace_buffer[i][0] != '\0') {
      fprintf(fh, dp8390_trace_buffer[i]);
    }
  }
}



