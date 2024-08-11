#include "xthdc.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "io.h"
#include "fe2010.h"
#include "console.h"
#include "panic.h"

#define XTHDC_TRACE_BUFFER_SIZE 256
#define XTHDC_TRACE_MAX 80

#define XTHDC_DATA      0x320
#define XTHDC_HW_RESET  0x321
#define XTHDC_HW_STATUS 0x321
#define XTHDC_DRIVE_SEL 0x322
#define XTHDC_DRIVE_CFG 0x322
#define XTHDC_MASK      0x323

#define XTHDC_STATUS_REQ  0
#define XTHDC_STATUS_IO   1
#define XTHDC_STATUS_CD   2
#define XTHDC_STATUS_XBSY 3
#define XTHDC_STATUS_DRQ  4
#define XTHDC_STATUS_IRQ  5

#define XTHDC_MASK_DRQEN  0
#define XTHDC_MASK_IRQEN  1

#define XTHDC_CMD_TEST_DRIVE        0x00
#define XTHDC_CMD_RECALIBRATE       0x01
#define XTHDC_CMD_REQUEST_SENSE     0x03
#define XTHDC_CMD_FORMAT_DRIVE      0x04
#define XTHDC_CMD_READY_VERIFY      0x05
#define XTHDC_CMD_FORMAT_TRACK      0x06
#define XTHDC_CMD_FORMAT_BAD_TRACK  0x07
#define XTHDC_CMD_READ              0x08
#define XTHDC_CMD_WRITE             0x0A
#define XTHDC_CMD_SEEK              0x0B
#define XTHDC_CMD_INITIALIZE_DRIVE  0x0C
#define XTHDC_CMD_READ_ECC_BURST    0x0D
#define XTHDC_CMD_READ_DATA_FROM_SB 0x0E
#define XTHDC_CMD_WRITE_DATA_TO_SB  0x0F
#define XTHDC_CMD_RAM_DIAGNOSTIC    0xE0
#define XTHDC_CMD_DRIVE_DIAGNOSTIC  0xE3
#define XTHDC_CMD_CTRL_DIAGNOSTIC   0xE4
#define XTHDC_CMD_READ_LONG_TRACK   0xE5
#define XTHDC_CMD_WRITE_LONG_TRACK  0xE6

#define xthdc_status_set(x, bit)   (((xthdc_t *)x)->status |=  (1 << bit));
#define xthdc_status_clear(x, bit) (((xthdc_t *)x)->status &= ~(1 << bit));

static char xthdc_trace_buffer[XTHDC_TRACE_BUFFER_SIZE][XTHDC_TRACE_MAX];
static int xthdc_trace_buffer_n = 0;



static void xthdc_trace(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(xthdc_trace_buffer[xthdc_trace_buffer_n],
    XTHDC_TRACE_MAX, format, args);
  va_end(args);

  xthdc_trace_buffer_n++;
  if (xthdc_trace_buffer_n >= XTHDC_TRACE_BUFFER_SIZE) {
    xthdc_trace_buffer_n = 0;
  }
}



static void xthdc_update_chs(xthdc_t *xthdc)
{
  xthdc->drive     = (xthdc->command[1] >> 5) & 1;
  xthdc->head      =  xthdc->command[1]       & 0x1F;
  xthdc->sector    =  xthdc->command[2]       & 0x3F;
  xthdc->cylinder  = (xthdc->command[2] >> 6) * 0x100;
  xthdc->cylinder +=  xthdc->command[3];
}



static uint8_t xthdc_read_sector_byte(xthdc_t *xthdc)
{
  uint32_t lba;

  lba = ((xthdc->cylinder * DISK_HEADS + xthdc->head) *
    DISK_SECTORS) + xthdc->sector;

  if (xthdc->byte_no == 0) {
    xthdc_trace("READ D=%d C=%d H=%d S=%d LBA=%d\n",
      xthdc->drive, xthdc->cylinder, xthdc->head, xthdc->sector + 1, lba);
  }

  return xthdc->data[(lba * DISK_SECTOR_SIZE) + xthdc->byte_no];
}



static uint8_t xthdc_image_read_callback(void *callback_data)
{
  uint8_t byte;
  byte = xthdc_read_sector_byte((xthdc_t *)callback_data);
  ((xthdc_t *)callback_data)->byte_no++;

  if (((xthdc_t *)callback_data)->byte_no >= DISK_SECTOR_SIZE) {
    ((xthdc_t *)callback_data)->byte_no = 0;
    ((xthdc_t *)callback_data)->sector++;
    if (((xthdc_t *)callback_data)->sector >= DISK_SECTORS) {
      ((xthdc_t *)callback_data)->sector = 0;
      ((xthdc_t *)callback_data)->head++;
      if (((xthdc_t *)callback_data)->head >= DISK_HEADS) {
        ((xthdc_t *)callback_data)->head = 0;
        ((xthdc_t *)callback_data)->cylinder++;
        if (((xthdc_t *)callback_data)->cylinder >= DISK_CYLINDERS) {
          ((xthdc_t *)callback_data)->cylinder = 0;
          panic("Overrun during XT HDC read callback!\n");
        }
      }
    }
  }

  return byte;
}



static void xthdc_write_sector_byte(xthdc_t *xthdc, uint8_t byte)
{
  uint32_t lba;

  lba = ((xthdc->cylinder * DISK_HEADS + xthdc->head) *
    DISK_SECTORS) + xthdc->sector;

  if (xthdc->byte_no == 0) {
    xthdc_trace("WRITE D=%d C=%d H=%d S=%d LBA=%d\n",
      xthdc->drive, xthdc->cylinder, xthdc->head, xthdc->sector + 1, lba);
  }

  xthdc->data[(lba * DISK_SECTOR_SIZE) + xthdc->byte_no] = byte;
}



static void xthdc_image_write_callback(void *callback_data, uint8_t byte)
{
  xthdc_write_sector_byte((xthdc_t *)callback_data, byte);
  ((xthdc_t *)callback_data)->byte_no++;

  if (((xthdc_t *)callback_data)->byte_no >= DISK_SECTOR_SIZE) {
    ((xthdc_t *)callback_data)->byte_no = 0;
    ((xthdc_t *)callback_data)->sector++;
    if (((xthdc_t *)callback_data)->sector >= DISK_SECTORS) {
      ((xthdc_t *)callback_data)->sector = 0;
      ((xthdc_t *)callback_data)->head++;
      if (((xthdc_t *)callback_data)->head >= DISK_HEADS) {
        ((xthdc_t *)callback_data)->head = 0;
        ((xthdc_t *)callback_data)->cylinder++;
        if (((xthdc_t *)callback_data)->cylinder >= DISK_CYLINDERS) {
          ((xthdc_t *)callback_data)->cylinder = 0;
          panic("Overrun during XT HDC write callback!\n");
        }
      }
    }
  }
}



static void xthdc_data_write(void *xthdc, uint16_t port, uint8_t value)
{
  (void)port;
  xthdc_trace("DATA write: 0x%02x\n", value);

  switch (((xthdc_t *)xthdc)->state) {
  case XTHDC_STATE_COMMAND:
    ((xthdc_t *)xthdc)->command[0] = value;
    ((xthdc_t *)xthdc)->state = XTHDC_STATE_COMMAND_PARAM_1;
    break;

  case XTHDC_STATE_COMMAND_PARAM_1:
    ((xthdc_t *)xthdc)->command[1] = value;
    ((xthdc_t *)xthdc)->state = XTHDC_STATE_COMMAND_PARAM_2;
    break;

  case XTHDC_STATE_COMMAND_PARAM_2:
    ((xthdc_t *)xthdc)->command[2] = value;
    ((xthdc_t *)xthdc)->state = XTHDC_STATE_COMMAND_PARAM_3;
    break;

  case XTHDC_STATE_COMMAND_PARAM_3:
    ((xthdc_t *)xthdc)->command[3] = value;
    ((xthdc_t *)xthdc)->state = XTHDC_STATE_COMMAND_PARAM_4;
    break;

  case XTHDC_STATE_COMMAND_PARAM_4:
    ((xthdc_t *)xthdc)->command[4] = value;
    ((xthdc_t *)xthdc)->state = XTHDC_STATE_COMMAND_PARAM_5;
    break;

  case XTHDC_STATE_COMMAND_PARAM_5:
    ((xthdc_t *)xthdc)->command[5] = value;

    switch (((xthdc_t *)xthdc)->command[0]) {
    case XTHDC_CMD_READ:
      xthdc_update_chs(xthdc);
      ((xthdc_t *)xthdc)->byte_no = 0;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_DRQEN) & 1) {
        /* DMA Transfer */
        fe2010_dma_write(((xthdc_t *)xthdc)->fe2010, FE2010_DMA_HARD_DISK,
          xthdc_image_read_callback, xthdc);
        if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
          fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
          xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
        }
        xthdc_status_set(xthdc, XTHDC_STATUS_IO);
        ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      } else {
        /* PIO Transfer */
        xthdc_status_clear(xthdc, XTHDC_STATUS_CD);
        xthdc_status_set(xthdc, XTHDC_STATUS_IO);
        ((xthdc_t *)xthdc)->state = XTHDC_STATE_READ_SECTOR;
      }
      break;

    case XTHDC_CMD_WRITE:
      xthdc_update_chs(xthdc);
      ((xthdc_t *)xthdc)->byte_no = 0;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_DRQEN) & 1) {
        /* DMA Transfer */
        fe2010_dma_read(((xthdc_t *)xthdc)->fe2010, FE2010_DMA_HARD_DISK,
          xthdc_image_write_callback, xthdc);
        if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
          fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
          xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
        }
        xthdc_status_set(xthdc, XTHDC_STATUS_IO);
        ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      } else {
        /* PIO Transfer */
        panic("XT HDC PIO write not implemented!\n");
        xthdc_status_set(xthdc, XTHDC_STATUS_IO);
        ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      }
      break;

    case XTHDC_CMD_READY_VERIFY:
      ((xthdc_t *)xthdc)->command_status = 0x20;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
        fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
        xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
      }
      xthdc_status_set(xthdc, XTHDC_STATUS_IO);
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      break;

    case XTHDC_CMD_INITIALIZE_DRIVE:
      ((xthdc_t *)xthdc)->byte_no = 0;
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_INITIALIZE_DRIVE;
      break;

    case XTHDC_CMD_RECALIBRATE:
      if ((((xthdc_t *)xthdc)->command[1] >> 5) & 1) { /* Drive number. */
        ((xthdc_t *)xthdc)->command_status = 0x22; /* Drive 2 not present. */
      } else {
        ((xthdc_t *)xthdc)->command_status = 0; /* Drive 1 present. */
      }
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
        fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
        xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
      }
      xthdc_status_set(xthdc, XTHDC_STATUS_IO);
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      break;

    case XTHDC_CMD_REQUEST_SENSE:
      ((xthdc_t *)xthdc)->command_status = 0x20;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
        fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
        xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
      }
      xthdc_status_set(xthdc, XTHDC_STATUS_IO);
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      break;

    default: /* All other commands. */
      panic("Unhandled XT HDC command: 0x%02x\n",
        ((xthdc_t *)xthdc)->command[0]);
      /* Fall through */
    case XTHDC_CMD_TEST_DRIVE:
    case XTHDC_CMD_CTRL_DIAGNOSTIC:
      ((xthdc_t *)xthdc)->command_status = 0;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
        fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
        xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
      }
      xthdc_status_set(xthdc, XTHDC_STATUS_IO);
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
      break;
    }
    break;

  case XTHDC_STATE_INITIALIZE_DRIVE:
    ((xthdc_t *)xthdc)->byte_no++;
    if (((xthdc_t *)xthdc)->byte_no >= 8) {
      ((xthdc_t *)xthdc)->command_status = 0;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
        fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
        xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
      }
      xthdc_status_set(xthdc, XTHDC_STATUS_IO);
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
    }
    break;

  case XTHDC_STATE_STATUS:
  case XTHDC_STATE_IDLE:
  default:
    panic("Unexpected XT HDC data write! (0x%02x)\n", value);
    break;
  }
}



static uint8_t xthdc_data_read(void *xthdc, uint16_t port)
{
  (void)port;
  uint8_t byte;

  switch (((xthdc_t *)xthdc)->state) {
  case XTHDC_STATE_STATUS:
    xthdc_status_clear(xthdc, XTHDC_STATUS_REQ);
    xthdc_status_clear(xthdc, XTHDC_STATUS_XBSY);
    xthdc_status_clear(xthdc, XTHDC_STATUS_CD);
    xthdc_status_clear(xthdc, XTHDC_STATUS_IO);
    ((xthdc_t *)xthdc)->state = XTHDC_STATE_IDLE;
    xthdc_trace("DATA read:  0x%02x\n", ((xthdc_t *)xthdc)->command_status);
    return ((xthdc_t *)xthdc)->command_status;

  case XTHDC_STATE_READ_SECTOR:
    byte = xthdc_read_sector_byte(xthdc);
    ((xthdc_t *)xthdc)->byte_no++;
    if (((xthdc_t *)xthdc)->byte_no >= DISK_SECTOR_SIZE) {
      ((xthdc_t *)xthdc)->command_status = 0;
      if ((((xthdc_t *)xthdc)->mask >> XTHDC_MASK_IRQEN) & 1) {
        fe2010_irq(((xthdc_t *)xthdc)->fe2010, FE2010_IRQ_HARD_DISK);
        xthdc_status_set(xthdc, XTHDC_STATUS_IRQ);
      }
      ((xthdc_t *)xthdc)->state = XTHDC_STATE_STATUS;
    }
    xthdc_trace("DATA read:  0x%02x\n", byte);
    return byte;

  case XTHDC_STATE_COMMAND:
  case XTHDC_STATE_IDLE:
  default:
    panic("Unexpected XT HDC data read!\n");
    break;
  }

  return 0;
}



static void xthdc_reset_write(void *xthdc, uint16_t port, uint8_t value)
{
  (void)port;
  xthdc_trace("RST  write: 0x%02x\n", value);
  ((xthdc_t *)xthdc)->state = XTHDC_STATE_IDLE;
}



static uint8_t xthdc_status_read(void *xthdc, uint16_t port)
{
  (void)port;
  uint8_t status = ((xthdc_t *)xthdc)->status;
  xthdc_status_clear(xthdc, XTHDC_STATUS_IRQ);
  xthdc_trace("STAT read:  0x%02x\n", status);
  return status;
}



static void xthdc_drive_sel_write(void *xthdc, uint16_t port, uint8_t value)
{
  (void)port;
  xthdc_trace("SEL  write: 0x%02x\n", value);
  xthdc_status_set(xthdc, XTHDC_STATUS_REQ);
  xthdc_status_set(xthdc, XTHDC_STATUS_XBSY);
  xthdc_status_set(xthdc, XTHDC_STATUS_CD);
  xthdc_status_clear(xthdc, XTHDC_STATUS_IO);
  ((xthdc_t *)xthdc)->state = XTHDC_STATE_COMMAND;
}



static uint8_t xthdc_drive_cfg_read(void *xthdc, uint16_t port)
{
  (void)port;
  xthdc_trace("CFG  read:  0x%02x\n", ((xthdc_t *)xthdc)->config);
  return ((xthdc_t *)xthdc)->config;
}



static void xthdc_mask_write(void *xthdc, uint16_t port, uint8_t value)
{
  (void)port;
  xthdc_trace("MASK write: 0x%02x\n", value);
  ((xthdc_t *)xthdc)->mask = value;
}



void xthdc_init(xthdc_t *xthdc, io_t *io, fe2010_t *fe2010)
{
  int i;

  memset(xthdc, 0, sizeof(xthdc_t));
  xthdc->fe2010 = fe2010;
  xthdc->state  = XTHDC_STATE_IDLE;
  xthdc->config = 0xFF; /* Needed for BIOS to report correct C/H/S values. */

  io->write[XTHDC_DATA].func = xthdc_data_write;
  io->write[XTHDC_DATA].cookie = xthdc;
  io->read[XTHDC_DATA].func = xthdc_data_read;
  io->read[XTHDC_DATA].cookie = xthdc;

  io->write[XTHDC_HW_RESET].func = xthdc_reset_write;
  io->write[XTHDC_HW_RESET].cookie = xthdc;
  io->read[XTHDC_HW_STATUS].func = xthdc_status_read;
  io->read[XTHDC_HW_STATUS].cookie = xthdc;

  io->write[XTHDC_DRIVE_SEL].func = xthdc_drive_sel_write;
  io->write[XTHDC_DRIVE_SEL].cookie = xthdc;
  io->read[XTHDC_DRIVE_CFG].func = xthdc_drive_cfg_read;
  io->read[XTHDC_DRIVE_CFG].cookie = xthdc;

  io->write[XTHDC_MASK].func = xthdc_mask_write;
  io->write[XTHDC_MASK].cookie = xthdc;

  for (i = 0; i < XTHDC_TRACE_BUFFER_SIZE; i++) {
    xthdc_trace_buffer[i][0] = '\0';
  }
  xthdc_trace_buffer_n = 0;
}



void xthdc_trace_dump(FILE *fh)
{
  int i;

  for (i = xthdc_trace_buffer_n; i < XTHDC_TRACE_BUFFER_SIZE; i++) {
    if (xthdc_trace_buffer[i][0] != '\0') {
      fprintf(fh, xthdc_trace_buffer[i]);
    }
  }
  for (i = 0; i < xthdc_trace_buffer_n; i++) {
    if (xthdc_trace_buffer[i][0] != '\0') {
      fprintf(fh, xthdc_trace_buffer[i]);
    }
  }
}



int xthdc_image_load(xthdc_t *xthdc, const char *filename)
{
  FILE *fh;
  size_t n;
  int c;

  xthdc->loaded = false;

  fh = fopen(filename, "rb");
  if (fh == NULL) {
    console_exit();
    fprintf(stderr, "fopen() for '%s' failed with errno: %d\n",
      filename, errno);
    return -1;
  }

  n = 0;
  while ((c = fgetc(fh)) != EOF) {
    if (n >= DISK_SIZE) {
      console_exit();
      fprintf(stderr, "Too large disk image: '%s'\n", filename);
      fclose(fh);
      return -1;
    }
    xthdc->data[n] = c;
    n++;
  }
  fclose(fh);

  /* Fill remaining space on disk with zeroes. */
  while (n < DISK_SIZE) {
    xthdc->data[n] = 0;
    n++;
  }

  strncpy(xthdc->loaded_filename, filename, PATH_MAX);
  xthdc->loaded = true;
  return 0;
}



int xthdc_image_save(xthdc_t *xthdc, const char *filename)
{
  FILE *fh;
  size_t n;

  if (xthdc->loaded == false) {
    console_exit();
    fprintf(stderr, "No image loaded!\n");
    return -2;
  }

  if (filename == NULL) {
    fh = fopen(xthdc->loaded_filename, "wb");
  } else {
    fh = fopen(filename, "wb");
  }
  if (fh == NULL) {
    console_exit();
    fprintf(stderr, "fopen() for '%s' failed with errno: %d\n",
      filename, errno);
    return -1;
  }

  for (n = 0; n < DISK_SIZE; n++) {
    fputc(xthdc->data[n], fh);
  }
  fclose(fh);

  if (filename != NULL) {
    strncpy(xthdc->loaded_filename, filename, PATH_MAX);
  }
  return 0;
}



