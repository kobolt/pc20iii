#include "fe2010.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "io.h"
#include "panic.h"

#define FE2010_KEYBOARD_DATA_REGISTER 0x60
#define FE2010_CONTROL_REGISTER       0x61
#define FE2010_SWITCH_REGISTER        0x62
#define FE2010_CONFIGURATION_REGISTER 0x63

#define I8237_DMA_CH0_ADDRESS    0x00
#define I8237_DMA_CH0_WORD_COUNT 0x01
#define I8237_DMA_CH1_ADDRESS    0x02
#define I8237_DMA_CH1_WORD_COUNT 0x03
#define I8237_DMA_CH2_ADDRESS    0x04
#define I8237_DMA_CH2_WORD_COUNT 0x05
#define I8237_DMA_CH3_ADDRESS    0x06
#define I8237_DMA_CH3_WORD_COUNT 0x07

#define I8237_DMA_MODE_REGISTER 0x0B

#define I8237_DMA_CH0_PAGE 0x87
#define I8237_DMA_CH1_PAGE 0x83
#define I8237_DMA_CH2_PAGE 0x81
#define I8237_DMA_CH3_PAGE 0x82

#define I8259_IRQ_MASK_REGISTER 0x21
#define I8259_NMI_MASK_REGISTER 0xA0

#define I8253_PIT_COUNTER_0 0x40
#define I8253_PIT_COUNTER_1 0x41
#define I8253_PIT_COUNTER_2 0x42
#define I8253_PIT_CONTROL   0x43

#define PIT_MODE_INT  0 /* Interrupt on Terminal Count. */
#define PIT_MODE_SWRG 3 /* Square Wave Rate Generator. */

#define DMA_MODE_WRITE 1
#define DMA_MODE_READ  2



static uint8_t fe2010_scancode_read(void *fe2010, uint16_t port)
{
  (void)port;
  return ((fe2010_t *)fe2010)->scancode;
}



static uint8_t fe2010_ctrl_read(void *fe2010, uint16_t port)
{
  (void)port;
  return ((fe2010_t *)fe2010)->ctrl;
}



static void fe2010_ctrl_write(void *fe2010, uint16_t port, uint8_t value)
{
  (void)port;

  /* Clear keyboard data register. */
  if ((value >> 7) & 1) {
    ((fe2010_t *)fe2010)->scancode = 0;
  }

  /* Enable keyboard clock, check if flank from 0 to 1. */
  if ((((((fe2010_t *)fe2010)->ctrl >> 6) & 1) == 0) && (value >> 6) & 1) {
    /* Trigger an IRQ with the 10101010 scancode for the BIOS check. */
    ((fe2010_t *)fe2010)->scancode = 0xAA;
    fe2010_irq(fe2010, FE2010_IRQ_KEYBOARD);
  }

  ((fe2010_t *)fe2010)->ctrl = value;
}



static uint8_t fe2010_switch_read(void *fe2010, uint16_t port)
{
  (void)port;
  uint8_t value;
  if ((((fe2010_t *)fe2010)->ctrl >> 2) & 1) { /* Bits 0-3 */
    value = ((fe2010_t *)fe2010)->switches & 0xF;
  } else { /* Bits 4-7 */
    value = ((fe2010_t *)fe2010)->switches >> 4;
  }
  value |= ((fe2010_t *)fe2010)->timer_2_output << 4;
  value |= ((fe2010_t *)fe2010)->timer_2_output << 5;
  return value;
}



static uint8_t fe2010_conf_read(void *fe2010, uint16_t port)
{
  (void)port;
  return ((fe2010_t *)fe2010)->conf;
}



static void fe2010_conf_write(void *fe2010, uint16_t port, uint8_t value)
{
  (void)port;
  ((fe2010_t *)fe2010)->conf = value;
}



static uint8_t i8237_dma_reg_read(void *fe2010, uint16_t port)
{
  if (((fe2010_t *)fe2010)->dma_flip_flop) {
    ((fe2010_t *)fe2010)->dma_flip_flop = false;
    return (((fe2010_t *)fe2010)->dma_reg[port & 7] >> 8) & 0xFF;
  } else {
    ((fe2010_t *)fe2010)->dma_flip_flop = true;
    return (((fe2010_t *)fe2010)->dma_reg[port & 7]) & 0xFF;
  }
}



static void i8237_dma_reg_write(void *fe2010, uint16_t port, uint8_t value)
{
  if (((fe2010_t *)fe2010)->dma_flip_flop) {
    ((fe2010_t *)fe2010)->dma_flip_flop = false;
    ((fe2010_t *)fe2010)->dma_reg[port & 7] += (value * 0x100);
  } else {
    ((fe2010_t *)fe2010)->dma_flip_flop = true;
    ((fe2010_t *)fe2010)->dma_reg[port & 7] = value;
  }
}



static void i8237_dma_mode_write(void *fe2010, uint16_t port, uint8_t value)
{
  (void)port;
  ((fe2010_t *)fe2010)->dma_mode[value & 3] = value & 0xFC;
}



static void i8237_dma_page_write(void *fe2010, uint16_t port, uint8_t value)
{
  switch (port) {
  case I8237_DMA_CH0_PAGE:
    ((fe2010_t *)fe2010)->dma_page[0] = value;
    break;
  case I8237_DMA_CH1_PAGE:
    ((fe2010_t *)fe2010)->dma_page[1] = value;
    break;
  case I8237_DMA_CH2_PAGE:
    ((fe2010_t *)fe2010)->dma_page[2] = value;
    break;
  case I8237_DMA_CH3_PAGE:
    ((fe2010_t *)fe2010)->dma_page[3] = value;
    break;
  }
}



static uint8_t i8259_irq_mask_read(void *fe2010, uint16_t port)
{
  (void)port;
  return ((fe2010_t *)fe2010)->irq_mask;
}



static void i8259_irq_mask_write(void *fe2010, uint16_t port, uint8_t value)
{
  (void)port;
  ((fe2010_t *)fe2010)->irq_mask = value;
}



static void i8259_nmi_mask_write(void *fe2010, uint16_t port, uint8_t value)
{
  (void)port;
  ((fe2010_t *)fe2010)->nmi_mask = value;
}



static uint8_t i8253_pit_counter_read(void *fe2010, uint16_t port)
{
  uint8_t pit_select;

  switch (port) {
  case I8253_PIT_COUNTER_0:
    pit_select = 0;
    break;

  case I8253_PIT_COUNTER_1:
    pit_select = 1;
    break;

  case I8253_PIT_COUNTER_2:
    pit_select = 2;
    break;

  default:
    return 0;
  }

  switch (((fe2010_t *)fe2010)->pit[pit_select].rl) {
  case 0b00: /* Counter latching operation. */
    if (((fe2010_t *)fe2010)->pit[pit_select].flip_flop) {
      ((fe2010_t *)fe2010)->pit[pit_select].flip_flop = false;
      return ((fe2010_t *)fe2010)->pit[pit_select].latch_msb;
    } else {
      ((fe2010_t *)fe2010)->pit[pit_select].flip_flop = true;
      return ((fe2010_t *)fe2010)->pit[pit_select].latch_lsb;
    }

  case 0b01: /* Read LSB only. */
    return ((fe2010_t *)fe2010)->pit[pit_select].counter_lsb;

  case 0b10: /* Read MSB only. */
    return ((fe2010_t *)fe2010)->pit[pit_select].counter_msb;

  case 0b11: /* Read LSB, then MSB. */
    if (((fe2010_t *)fe2010)->pit[pit_select].flip_flop) {
      ((fe2010_t *)fe2010)->pit[pit_select].flip_flop = false;
      return ((fe2010_t *)fe2010)->pit[pit_select].counter_msb;
    } else {
      ((fe2010_t *)fe2010)->pit[pit_select].flip_flop = true;
      return ((fe2010_t *)fe2010)->pit[pit_select].counter_lsb;
    }

  default:
    break;
  }

  return 0;
}



static void i8253_pit_counter_write(void *fe2010, uint16_t port, uint8_t value)
{
  uint8_t pit_select;

  switch (port) {
  case I8253_PIT_COUNTER_0:
    pit_select = 0;
    break;

  case I8253_PIT_COUNTER_1:
    pit_select = 1;
    break;

  case I8253_PIT_COUNTER_2:
    pit_select = 2;
    break;

  default:
    return;
  }

  switch (((fe2010_t *)fe2010)->pit[pit_select].rl) {
  case 0b00:
    panic("PIT latched load mode not implemented!\n");
    break;

  case 0b01: /* Load LSB only. */
    ((fe2010_t *)fe2010)->pit[pit_select].counter_lsb = value;
    ((fe2010_t *)fe2010)->pit[pit_select].counter_msb = 0;
    break;

  case 0b10: /* Load MSB only. */
    ((fe2010_t *)fe2010)->pit[pit_select].counter_lsb = 0;
    ((fe2010_t *)fe2010)->pit[pit_select].counter_msb = value;
    break;

  case 0b11: /* Load LSB, then MSB. */
    if (((fe2010_t *)fe2010)->pit[pit_select].flip_flop) {
      ((fe2010_t *)fe2010)->pit[pit_select].flip_flop = false;
      ((fe2010_t *)fe2010)->pit[pit_select].counter_msb = value;
    } else {
      ((fe2010_t *)fe2010)->pit[pit_select].flip_flop = true;
      ((fe2010_t *)fe2010)->pit[pit_select].counter_lsb = value;
    }
    break;

  default:
    break;
  }
}



static void i8253_pit_control_write(void *fe2010, uint16_t port, uint8_t value)
{
  (void)port;
  uint8_t pit_select;

  pit_select = (value >> 6);
  if (pit_select > 2) {
    panic("Illegal PIT counter selected: %d\n", pit_select);
    return;
  }

  ((fe2010_t *)fe2010)->pit[pit_select].control = (value & 0x3F);

  if (((fe2010_t *)fe2010)->pit[pit_select].rl == 0) { /* Update latch. */
    ((fe2010_t *)fe2010)->pit[pit_select].latch =
      ((fe2010_t *)fe2010)->pit[pit_select].counter;
  }

  switch (pit_select) {
  case 0:
  case 1:
    break;

  case 2:
    if (((fe2010_t *)fe2010)->pit[pit_select].mode == PIT_MODE_INT) {
      ((fe2010_t *)fe2010)->timer_2_output = true;
    } else {
      ((fe2010_t *)fe2010)->timer_2_output = false;
    }
    break;

  default:
    break;
  }
}



void fe2010_init(fe2010_t *fe2010, io_t *io, i8088_t *cpu, mem_t *mem)
{
  int i;

  memset(fe2010, 0, sizeof(fe2010_t));
  fe2010->cpu = cpu;
  fe2010->mem = mem;

  /* Set initial DIP switches:
   * - No 8087 installed.
   * - 640K RAM.
   * - CGA 80 columns.
   * - 2 floppy drives.
   */
  fe2010->switches = 0b01011100;

  io->read[FE2010_KEYBOARD_DATA_REGISTER].func = fe2010_scancode_read;
  io->read[FE2010_KEYBOARD_DATA_REGISTER].cookie = fe2010;
  io->read[FE2010_CONTROL_REGISTER].func = fe2010_ctrl_read;
  io->read[FE2010_CONTROL_REGISTER].cookie = fe2010;
  io->read[FE2010_SWITCH_REGISTER].func = fe2010_switch_read;
  io->read[FE2010_SWITCH_REGISTER].cookie = fe2010;
  io->read[FE2010_CONFIGURATION_REGISTER].func = fe2010_conf_read;
  io->read[FE2010_CONFIGURATION_REGISTER].cookie = fe2010;

  io->write[FE2010_CONTROL_REGISTER].func = fe2010_ctrl_write;
  io->write[FE2010_CONTROL_REGISTER].cookie = fe2010;
  io->write[FE2010_CONFIGURATION_REGISTER].func = fe2010_conf_write;
  io->write[FE2010_CONFIGURATION_REGISTER].cookie = fe2010;

  for (i = I8237_DMA_CH0_ADDRESS; i <= I8237_DMA_CH3_WORD_COUNT; i++) {
    io->read[i].func = i8237_dma_reg_read;
    io->read[i].cookie = fe2010;
    io->write[i].func = i8237_dma_reg_write;
    io->write[i].cookie = fe2010;
  }

  io->write[I8237_DMA_MODE_REGISTER].func = i8237_dma_mode_write;
  io->write[I8237_DMA_MODE_REGISTER].cookie = fe2010;

  io->write[I8237_DMA_CH0_PAGE].func = i8237_dma_page_write;
  io->write[I8237_DMA_CH0_PAGE].cookie = fe2010;
  io->write[I8237_DMA_CH1_PAGE].func = i8237_dma_page_write;
  io->write[I8237_DMA_CH1_PAGE].cookie = fe2010;
  io->write[I8237_DMA_CH2_PAGE].func = i8237_dma_page_write;
  io->write[I8237_DMA_CH2_PAGE].cookie = fe2010;
  io->write[I8237_DMA_CH3_PAGE].func = i8237_dma_page_write;
  io->write[I8237_DMA_CH3_PAGE].cookie = fe2010;

  io->read[I8259_IRQ_MASK_REGISTER].func = i8259_irq_mask_read;
  io->read[I8259_IRQ_MASK_REGISTER].cookie = fe2010;
  io->write[I8259_IRQ_MASK_REGISTER].func = i8259_irq_mask_write;
  io->write[I8259_IRQ_MASK_REGISTER].cookie = fe2010;

  io->write[I8259_NMI_MASK_REGISTER].func = i8259_nmi_mask_write;
  io->write[I8259_NMI_MASK_REGISTER].cookie = fe2010;

  io->read[I8253_PIT_COUNTER_0].func = i8253_pit_counter_read;
  io->read[I8253_PIT_COUNTER_0].cookie = fe2010;
  io->write[I8253_PIT_COUNTER_0].func = i8253_pit_counter_write;
  io->write[I8253_PIT_COUNTER_0].cookie = fe2010;

  io->read[I8253_PIT_COUNTER_1].func = i8253_pit_counter_read;
  io->read[I8253_PIT_COUNTER_1].cookie = fe2010;
  io->write[I8253_PIT_COUNTER_1].func = i8253_pit_counter_write;
  io->write[I8253_PIT_COUNTER_1].cookie = fe2010;

  io->read[I8253_PIT_COUNTER_2].func = i8253_pit_counter_read;
  io->read[I8253_PIT_COUNTER_2].cookie = fe2010;
  io->write[I8253_PIT_COUNTER_2].func = i8253_pit_counter_write;
  io->write[I8253_PIT_COUNTER_2].cookie = fe2010;

  io->write[I8253_PIT_CONTROL].func = i8253_pit_control_write;
  io->write[I8253_PIT_CONTROL].cookie = fe2010;
}



void fe2010_execute(fe2010_t *fe2010)
{
  static int cycle = 0;
  int i;
  int j;

  /* NOTE: Counting is currently not synchronized at all against CPU! */
  /* This particular cycle counting makes the POST timer 2 check succeed. */
  cycle++;
  if (cycle > 6) {
    cycle = 0;

    /* Check for pending IRQs. */
    for (i = 0; i < 8; i++) {
      if (fe2010->irq_pending[i]) {
        fe2010_irq(fe2010, i);
      }
    }

    /* Operate PIT timers. */
    for (j = 0; j < 2; j++) {
      for (i = 0; i < 3; i++) {
        fe2010->pit[i].counter--;
        if (fe2010->pit[i].counter == 0) {
          if (i == 0) {
            fe2010_irq(fe2010, FE2010_IRQ_TIMER);
          } else if (i == 2) {
            ((fe2010_t *)fe2010)->timer_2_output = false;
          }
        }
      }
    }
  }
}



void fe2010_irq(fe2010_t *fe2010, int irq_no)
{
  if (fe2010->irq_mask >> irq_no) {
    fe2010->irq_pending[irq_no] = i8088_irq(fe2010->cpu, fe2010->mem, irq_no);
  }
}



void fe2010_dma_write(fe2010_t *fe2010, int channel_no,
  uint8_t (*callback_func)(void *), void *callback_data)
{
  uint32_t address;
  uint16_t count;
  size_t i;

  if (((fe2010->dma_mode[channel_no] >> 2) & 0x3) != DMA_MODE_WRITE) {
    /* Only write to memory if in write mode! */
    return;
  }

  address = fe2010->dma_reg[channel_no * 2] +
           (fe2010->dma_page[channel_no] << 16);
  count = fe2010->dma_reg[(channel_no * 2) + 1];

  for (i = 0; i <= count; i++) {
    mem_write(fe2010->mem, address + i, (callback_func)(callback_data));
  }
}



void fe2010_dma_read(fe2010_t *fe2010, int channel_no,
  void (*callback_func)(void *, uint8_t), void *callback_data)
{
  uint32_t address;
  uint16_t count;
  size_t i;

  if (((fe2010->dma_mode[channel_no] >> 2) & 0x3) != DMA_MODE_READ) {
    /* Only read from memory if in read mode! */
    return;
  }

  address = fe2010->dma_reg[channel_no * 2] +
           (fe2010->dma_page[channel_no] << 16);
  count = fe2010->dma_reg[(channel_no * 2) + 1];

  for (i = 0; i <= count; i++) {
    (callback_func)(callback_data, mem_read(fe2010->mem, address + i));
  }
}



void fe2010_keyboard_press(fe2010_t *fe2010, int scancode)
{
  if ((fe2010->ctrl >> 6) & 1) { /* Keyboard clock enabled. */
    fe2010->scancode = scancode;
    fe2010_irq(fe2010, FE2010_IRQ_KEYBOARD);
  }
}



static int fe2010_cpu_speed(fe2010_t *fe2010)
{
  if (fe2010->conf >> 7 & 1) {
    return 9540000; /* Double, 9.54MHz */
  } else if (fe2010->conf >> 6 & 1) {
    return 7155000; /* Turbo, 7.16MHz */
  } else {
    return 4770000; /* Standard, 4.77MHz */
  }
}



static int fe2010_system_memory_size(fe2010_t *fe2010)
{
  switch (fe2010->switches >> 2 & 0x3) {
  case 0:
    return 128;
  case 1:
    return 256;
  case 2:
    return 512;
  case 3:
    return 640;
  default:
    return 0;
  }
}



void fe2010_dump(FILE *fh, fe2010_t *fe2010)
{
  int i;

  fprintf(fh, "Keyboard Data Register: 0x%02x\n", fe2010->scancode);
  fprintf(fh, "Control Register      : 0x%02x\n", fe2010->ctrl);
  fprintf(fh, "Configuration Register: 0x%02x\n", fe2010->conf);
  fprintf(fh, "  CPU Speed: %.2fMHz\n",
    fe2010_cpu_speed(fe2010) / 1000000.0);
  fprintf(fh, "Switches: 0x%02x\n", fe2010->switches);
  fprintf(fh, "  8087 Installed: %s\n",
    ((fe2010->switches >> 1) & 1) ? "Yes" : "No");
  fprintf(fh, "  System Memory : %dKB\n", fe2010_system_memory_size(fe2010));
  fprintf(fh, "  Video Type    : %d\n", (fe2010->switches >> 4) & 0x3);
  fprintf(fh, "  Floppy Drives : %d\n", ((fe2010->switches >> 6) & 0x3) + 1);
  fprintf(fh, "Timer 2 Output: %d\n", fe2010->timer_2_output);

  fprintf(fh, "IRQ Mask: 0x%02x\n", fe2010->irq_mask);
  fprintf(fh, "NMI Mask: 0x%02x\n", fe2010->nmi_mask);

  for (i = 0; i < 4; i++) {
    fprintf(fh, "DMA Channel %d:\n", i);
    fprintf(fh, "  Address   : 0x%04x\n", fe2010->dma_reg[i * 2]);
    fprintf(fh, "  Word Count: 0x%04x\n", fe2010->dma_reg[(i * 2) + 1]);
    fprintf(fh, "  Page      : 0x%02x\n", fe2010->dma_page[i]);
    fprintf(fh, "  Mode      : 0x%02x\n", fe2010->dma_mode[i]);
  }

  for (i = 0; i < 3; i++) {
    fprintf(fh, "PIT Channel %d:\n", i);
    fprintf(fh, "  Control  : 0x%02x\n", fe2010->pit[i].control);
    fprintf(fh, "    BCD    : %d\n", fe2010->pit[i].bcd);
    fprintf(fh, "    Mode   : %d\n", fe2010->pit[i].mode);
    fprintf(fh, "    R/L    : %d\n", fe2010->pit[i].rl);
    fprintf(fh, "  Counter  : 0x%04x\n", fe2010->pit[i].counter);
    fprintf(fh, "  Latch    : 0x%04x\n", fe2010->pit[i].latch);
    fprintf(fh, "  Flip-Flop: %d\n", fe2010->pit[i].flip_flop);
  }
}



