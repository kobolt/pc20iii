#include "fdc9268.h"
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

#define FLOPPY_SECTOR_SIZE 512
#define FLOPPY_HPC 2 /* Heads Per Cylinder */

#define FDC_TRACE_BUFFER_SIZE 256
#define FDC_TRACE_MAX 80

#define FDC_DOR  0x3F2 /* Digital Output Register */
#define FDC_MSR  0x3F4 /* Main Status Register */
#define FDC_FIFO 0x3F5

#define FDC_CMD_SPECIFY 0x03 /* Specify */
#define FDC_CMD_SDS     0x04 /* Sense Drive Status */
#define FDC_CMD_WRITE   0x05 /* Write */
#define FDC_CMD_READ    0x06 /* Read */
#define FDC_CMD_RECALIB 0x07 /* Recalibrate */
#define FDC_CMD_SIS     0x08 /* Sense Interrupt Status */
#define FDC_CMD_SEEK    0x0F /* Seek */

#define FDC_DOR_DRIVE_SEL0 0
#define FDC_DOR_DRIVE_SEL1 1
#define FDC_DOR_RESET      2
#define FDC_DOR_DMAEN      3
#define FDC_DOR_MOT_EN0    4
#define FDC_DOR_MOT_EN1    5
#define FDC_DOR_MOT_EN2    6
#define FDC_DOR_MOT_EN3    7

#define FDC_MSR_DRV0_BUSY 0
#define FDC_MSR_DRV1_BUSY 1
#define FDC_MSR_DRV2_BUSY 2
#define FDC_MSR_DRV3_BUSY 3
#define FDC_MSR_CMD_BUSY  4
#define FDC_MSR_NON_DMA   5
#define FDC_MSR_DIO       6
#define FDC_MSR_RQM       7

#define FDC_ST0_DRIVE_SEL0      0
#define FDC_ST0_DRIVE_SEL1      1
#define FDC_ST0_HEAD_NO_AT_INT  2
#define FDC_ST0_NOT_READY       3
#define FDC_ST0_EQUIPMENT_ERROR 4
#define FDC_ST0_SEEK_COMPLETE   5
#define FDC_ST0_CMD_STATUS0     6
#define FDC_ST0_CMD_STATUS1     7

#define FDC_ST1_MARK_NOT_FOUND   0
#define FDC_ST1_WRITE_PROTECT    1
#define FDC_ST1_SECTOR_NOT_FOUND 2
#define FDC_ST1_OVERRUN_ERROR    4
#define FDC_ST1_CRC_ERROR        5
#define FDC_ST1_END_OF_CYLINDER  7

#define fdc_msr_set(x, bit)   (((fdc9268_t *)x)->msr |=  (1 << bit));
#define fdc_msr_clear(x, bit) (((fdc9268_t *)x)->msr &= ~(1 << bit));
#define fdc_st0_set(x, bit)   (((fdc9268_t *)x)->st0 |=  (1 << bit));
#define fdc_st0_clear(x, bit) (((fdc9268_t *)x)->st0 &= ~(1 << bit));
#define fdc_state_set(x, st)  (((fdc9268_t *)x)->state = st);

static char fdc_trace_buffer[FDC_TRACE_BUFFER_SIZE][FDC_TRACE_MAX];
static int fdc_trace_buffer_n = 0;



static void fdc_trace(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(fdc_trace_buffer[fdc_trace_buffer_n],
    FDC_TRACE_MAX, format, args);
  va_end(args);

  fdc_trace_buffer_n++;
  if (fdc_trace_buffer_n >= FDC_TRACE_BUFFER_SIZE) {
    fdc_trace_buffer_n = 0;
  }
}



static void fdc_reset(fdc9268_t *fdc)
{
  fdc->state = FDC_STATE_IDLE;
  fdc->msr   = (1 << FDC_MSR_RQM);
  fdc->st0   = (1 << FDC_ST0_CMD_STATUS0) | (1 << FDC_ST0_CMD_STATUS1);
  fdc->st1   = 0;
  fdc->st2   = 0;
  fdc->pcn   = 0;

  fdc->pending_irq = false;
  fdc->dor_reset   = true;
}



static void fdc_st0_drive_sel_update(fdc9268_t *fdc, uint8_t value)
{
  switch (value & 3) {
  case 0:
    fdc_st0_clear(fdc, FDC_ST0_DRIVE_SEL0);
    fdc_st0_clear(fdc, FDC_ST0_DRIVE_SEL1);
    break;
  case 1:
    fdc_st0_set(fdc, FDC_ST0_DRIVE_SEL0);
    fdc_st0_clear(fdc, FDC_ST0_DRIVE_SEL1);
    break;
  case 2:
    fdc_st0_clear(fdc, FDC_ST0_DRIVE_SEL0);
    fdc_st0_set(fdc, FDC_ST0_DRIVE_SEL1);
    break;
  case 3:
    fdc_st0_set(fdc, FDC_ST0_DRIVE_SEL0);
    fdc_st0_set(fdc, FDC_ST0_DRIVE_SEL1);
    break;
  default:
    break;
  }

  if ((value >> 2) & 1) {
    fdc_st0_set(fdc, FDC_ST0_HEAD_NO_AT_INT);
  } else {
    fdc_st0_clear(fdc, FDC_ST0_HEAD_NO_AT_INT);
  }
}



static void fdc_image_write_callback(void *callback_data, uint8_t byte)
{
  ((floppy_t *)callback_data)->data[
    ((floppy_t *)callback_data)->pos] = byte;
  ((floppy_t *)callback_data)->pos++;

  if (((floppy_t *)callback_data)->pos > ((floppy_t *)callback_data)->size) {
    ((floppy_t *)callback_data)->pos = 0;
    panic("Overrun during FDC write callback!\n");
  }
}



static uint8_t fdc_image_read_callback(void *callback_data)
{
  uint8_t byte;

  byte = ((floppy_t *)callback_data)->data[
    ((floppy_t *)callback_data)->pos];
  ((floppy_t *)callback_data)->pos++;

  if (((floppy_t *)callback_data)->pos > ((floppy_t *)callback_data)->size) {
    ((floppy_t *)callback_data)->pos = 0;
    panic("Overrun during FDC read callback!\n");
  }

  return byte;
}



static bool fdc_image_dma(fdc9268_t *fdc, bool read_operation)
{
  floppy_t *floppy;
  uint32_t lba;
  uint16_t spt;
  int ds;

  ds = fdc->st0 & 0x3; /* Drive Selected */

  if (fdc->floppy[ds].loaded == false) {
    return false;
  }

  floppy = &fdc->floppy[ds];
  spt = fdc->floppy[ds].spt;

  /* LBA = ( ( cylinder * HPC + head ) * SPT ) + sector - 1 */
  lba = ((fdc->cmd_cylinder * FLOPPY_HPC + fdc->cmd_head) *
    spt) + fdc->cmd_sector - 1;

  fdc->floppy[ds].pos = (lba * FLOPPY_SECTOR_SIZE);

  if (read_operation) {
    fe2010_dma_write(fdc->fe2010, FE2010_DMA_FLOPPY_DISK,
      fdc_image_read_callback, floppy);
  } else {
    fe2010_dma_read(fdc->fe2010, FE2010_DMA_FLOPPY_DISK,
      fdc_image_write_callback, floppy);
  }

  return true;
}



static void fdc_dor_write(void *fdc, uint16_t port, uint8_t value)
{
  (void)port;
  fdc_trace("DOR write: 0x%02x\n", value);

  if (((value >> FDC_DOR_RESET) & 1) == 0) {
    fdc_reset(fdc);
    return;
  }

  /* Generate IRQ when DMA is enabled after it has been reset. */
  if ((((value >> FDC_DOR_DMAEN) & 1) == 1) && ((fdc9268_t *)fdc)->dor_reset) {
    ((fdc9268_t *)fdc)->dor_reset = false;
    fe2010_irq(((fdc9268_t *)fdc)->fe2010, FE2010_IRQ_FLOPPY_DISK);
    ((fdc9268_t *)fdc)->pending_irq = true;
  }
}



static uint8_t fdc_msr_read(void *fdc, uint16_t port)
{
  (void)port;
  fdc_trace("MSR read: 0x%02x\n", ((fdc9268_t *)fdc)->msr);
  return ((fdc9268_t *)fdc)->msr;
}



static uint8_t fdc_fifo_read(void *fdc, uint16_t port)
{
  (void)port;
  switch (((fdc9268_t *)fdc)->state) {
  case FDC_STATE_CMD_SIS_ST0:
    if (((fdc9268_t *)fdc)->pending_irq == false) {
      fdc_state_set(fdc, FDC_STATE_CMD_SIS_PCN);
      fdc_trace("FIFO read: SIS/ST0: 0x80\n");
      return 0x80; /* No IRQ pending. */
    }
    ((fdc9268_t *)fdc)->pending_irq = false;
    fdc_state_set(fdc, FDC_STATE_CMD_SIS_PCN);
    fdc_trace("FIFO read: SIS/ST0: 0x%02x\n", ((fdc9268_t *)fdc)->st0);
    return ((fdc9268_t *)fdc)->st0;

  case FDC_STATE_CMD_SIS_PCN:
    fdc_msr_clear(fdc, FDC_MSR_DIO);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fdc_state_set(fdc, FDC_STATE_IDLE);
    fdc_trace("FIFO read: SIS/PCN: 0x%02x\n", ((fdc9268_t *)fdc)->pcn);
    return ((fdc9268_t *)fdc)->pcn;

  case FDC_STATE_CMD_SDS_ST3:
    fdc_msr_clear(fdc, FDC_MSR_DIO);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fdc_state_set(fdc, FDC_STATE_IDLE);
    fdc_trace("FIFO read: SDS/ST3: 0x%02x\n", ((fdc9268_t *)fdc)->st3);
    return ((fdc9268_t *)fdc)->st3;

  case FDC_STATE_CMD_WRITE_ST0:
    ((fdc9268_t *)fdc)->pending_irq = false;
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_ST1);
    fdc_trace("FIFO read: Write/ST0: 0x%02x\n", ((fdc9268_t *)fdc)->st0);
    return ((fdc9268_t *)fdc)->st0;

  case FDC_STATE_CMD_WRITE_ST1:
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_ST2);
    fdc_trace("FIFO read: Write/ST1: 0x%02x\n", ((fdc9268_t *)fdc)->st1);
    return ((fdc9268_t *)fdc)->st1;

  case FDC_STATE_CMD_WRITE_ST2:
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_C_AFTER);
    fdc_trace("FIFO read: Write/ST2: 0x%02x\n", ((fdc9268_t *)fdc)->st2);
    return ((fdc9268_t *)fdc)->st2;

  case FDC_STATE_CMD_WRITE_C_AFTER:
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_H_AFTER);
    fdc_trace("FIFO read: Write/C (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_cylinder);
    return ((fdc9268_t *)fdc)->cmd_cylinder;

  case FDC_STATE_CMD_WRITE_H_AFTER:
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_R_AFTER);
    fdc_trace("FIFO read: Write/H (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_head);
    return ((fdc9268_t *)fdc)->cmd_head;

  case FDC_STATE_CMD_WRITE_R_AFTER:
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_N_AFTER);
    fdc_trace("FIFO read: Write/R (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_sector);
    return ((fdc9268_t *)fdc)->cmd_sector;

  case FDC_STATE_CMD_WRITE_N_AFTER:
    fdc_msr_clear(fdc, FDC_MSR_DIO);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fdc_state_set(fdc, FDC_STATE_IDLE);
    fdc_trace("FIFO read: Write/N (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_number);
    return ((fdc9268_t *)fdc)->cmd_number;

  case FDC_STATE_CMD_READ_ST0:
    ((fdc9268_t *)fdc)->pending_irq = false;
    fdc_state_set(fdc, FDC_STATE_CMD_READ_ST1);
    fdc_trace("FIFO read: Read/ST0: 0x%02x\n", ((fdc9268_t *)fdc)->st0);
    return ((fdc9268_t *)fdc)->st0;

  case FDC_STATE_CMD_READ_ST1:
    fdc_state_set(fdc, FDC_STATE_CMD_READ_ST2);
    fdc_trace("FIFO read: Read/ST1: 0x%02x\n", ((fdc9268_t *)fdc)->st1);
    return ((fdc9268_t *)fdc)->st1;

  case FDC_STATE_CMD_READ_ST2:
    fdc_state_set(fdc, FDC_STATE_CMD_READ_C_AFTER);
    fdc_trace("FIFO read: Read/ST2: 0x%02x\n", ((fdc9268_t *)fdc)->st2);
    return ((fdc9268_t *)fdc)->st2;

  case FDC_STATE_CMD_READ_C_AFTER:
    fdc_state_set(fdc, FDC_STATE_CMD_READ_H_AFTER);
    fdc_trace("FIFO read: Read/C (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_cylinder);
    return ((fdc9268_t *)fdc)->cmd_cylinder;

  case FDC_STATE_CMD_READ_H_AFTER:
    fdc_state_set(fdc, FDC_STATE_CMD_READ_R_AFTER);
    fdc_trace("FIFO read: Read/H (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_head);
    return ((fdc9268_t *)fdc)->cmd_head;

  case FDC_STATE_CMD_READ_R_AFTER:
    fdc_state_set(fdc, FDC_STATE_CMD_READ_N_AFTER);
    fdc_trace("FIFO read: Read/R (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_sector);
    return ((fdc9268_t *)fdc)->cmd_sector;

  case FDC_STATE_CMD_READ_N_AFTER:
    fdc_msr_clear(fdc, FDC_MSR_DIO);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fdc_state_set(fdc, FDC_STATE_IDLE);
    fdc_trace("FIFO read: Read/N (After): 0x%02x\n",
      ((fdc9268_t *)fdc)->cmd_number);
    return ((fdc9268_t *)fdc)->cmd_number;

  case FDC_STATE_IDLE:
  default:
    panic("Unexpected FDC FIFO read!\n");
    break;
  }
  return 0;
}



static void fdc_fifo_write(void *fdc, uint16_t port, uint8_t value)
{
  (void)port;
  switch (((fdc9268_t *)fdc)->state) {
  case FDC_STATE_IDLE:
    if (value == FDC_CMD_SIS) {
      fdc_trace("FIFO write: SIS\n");
      fdc_msr_set(fdc, FDC_MSR_DIO);
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_SIS_ST0);

    } else if (value == FDC_CMD_SDS) {
      fdc_trace("FIFO write: SDS\n");
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_SDS_DS);

    } else if (value == FDC_CMD_SPECIFY) {
      fdc_trace("FIFO write: Specify\n");
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_SPECIFY_SRT_HUT);

    } else if (value == FDC_CMD_RECALIB) {
      fdc_trace("FIFO write: Recalib\n");
      fdc_st0_clear(fdc, FDC_ST0_SEEK_COMPLETE);
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_RECALIB_DS);

    } else if (value == FDC_CMD_SEEK) {
      fdc_trace("FIFO write: Seek\n");
      fdc_st0_clear(fdc, FDC_ST0_SEEK_COMPLETE);
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_SEEK_DS);

    } else if ((value & 0x1F) == FDC_CMD_READ) {
      fdc_trace("FIFO write: Read\n");
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_READ_DS);

    } else if ((value & 0x1F) == FDC_CMD_WRITE) {
      fdc_trace("FIFO write: Write\n");
      fdc_msr_set(fdc, FDC_MSR_CMD_BUSY);
      fdc_state_set(fdc, FDC_STATE_CMD_WRITE_DS);

    } else {
      panic("Unhandled FDC command: 0x%02x\n", value);
    }
    break;

  case FDC_STATE_CMD_SDS_DS:
    fdc_trace("FIFO write: SDS/DS: 0x%02x\n", value);
    fdc_st0_drive_sel_update(fdc, value);
    fdc_msr_set(fdc, FDC_MSR_DIO);
    fdc_state_set(fdc, FDC_STATE_CMD_SDS_ST3);
    break;

  case FDC_STATE_CMD_SPECIFY_SRT_HUT:
    fdc_trace("FIFO write: Specify/SRT+HUT: 0x%02x\n", value);
    fdc_state_set(fdc, FDC_STATE_CMD_SPECIFY_HLT_ND);
    break;

  case FDC_STATE_CMD_SPECIFY_HLT_ND:
    fdc_trace("FIFO write: Specify/HLT+DMA: 0x%02x\n", value);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fdc_state_set(fdc, FDC_STATE_IDLE);
    break;

  case FDC_STATE_CMD_RECALIB_DS:
    fdc_trace("FIFO write: Recalib/DS: 0x%02x\n", value);
    ((fdc9268_t *)fdc)->pcn = 0;
    fdc_st0_set(fdc, FDC_ST0_SEEK_COMPLETE);
    fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS0);
    fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS1);
    fdc_st0_drive_sel_update(fdc, value);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fe2010_irq(((fdc9268_t *)fdc)->fe2010, FE2010_IRQ_FLOPPY_DISK);
    ((fdc9268_t *)fdc)->pending_irq = true;
    fdc_state_set(fdc, FDC_STATE_IDLE);
    break;

  case FDC_STATE_CMD_SEEK_DS:
    fdc_trace("FIFO write: Seek/DS: 0x%02x\n", value);
    fdc_st0_drive_sel_update(fdc, value);
    fdc_state_set(fdc, FDC_STATE_CMD_SEEK_NCN);
    break;

  case FDC_STATE_CMD_SEEK_NCN:
    fdc_trace("FIFO write: Seek/NCN: 0x%02x\n", value);
    ((fdc9268_t *)fdc)->pcn = value;
    fdc_st0_set(fdc, FDC_ST0_SEEK_COMPLETE);
    fdc_msr_clear(fdc, FDC_MSR_CMD_BUSY);
    fe2010_irq(((fdc9268_t *)fdc)->fe2010, FE2010_IRQ_FLOPPY_DISK);
    ((fdc9268_t *)fdc)->pending_irq = true;
    fdc_state_set(fdc, FDC_STATE_IDLE);
    break;

  case FDC_STATE_CMD_WRITE_DS:
    fdc_trace("FIFO write: Write/DS: 0x%02x\n", value);
    fdc_st0_drive_sel_update(fdc, value);
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_C_PRIOR);
    break;

  case FDC_STATE_CMD_WRITE_C_PRIOR:
    fdc_trace("FIFO write: Write/C (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_cylinder = value;
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_H_PRIOR);
    break;

  case FDC_STATE_CMD_WRITE_H_PRIOR:
    fdc_trace("FIFO write: Write/H (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_head = value;
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_R_PRIOR);
    break;

  case FDC_STATE_CMD_WRITE_R_PRIOR:
    fdc_trace("FIFO write: Write/R (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_sector = value;
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_N_PRIOR);
    break;

  case FDC_STATE_CMD_WRITE_N_PRIOR:
    fdc_trace("FIFO write: Write/N (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_number = value;
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_EOT);
    break;

  case FDC_STATE_CMD_WRITE_EOT:
    fdc_trace("FIFO write: Write/EOT: 0x%02x\n", value);
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_GPL);
    break;

  case FDC_STATE_CMD_WRITE_GPL:
    fdc_trace("FIFO write: Write/GPL: 0x%02x\n", value);
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_DTL);
    break;

  case FDC_STATE_CMD_WRITE_DTL:
    fdc_trace("FIFO write: Write/DTL: 0x%02x\n", value);
    if (fdc_image_dma(fdc, false)) {
      /* Clear error bits if data was written. */
      fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS0);
      fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS1);
      ((fdc9268_t *)fdc)->st1 = 0;
    } else {
      /* Report error and sector not found if writing failed. */
      fdc_st0_set(fdc, FDC_ST0_CMD_STATUS0);
      fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS1);
      ((fdc9268_t *)fdc)->st1 = (1 << FDC_ST1_SECTOR_NOT_FOUND);
    }
    fe2010_irq(((fdc9268_t *)fdc)->fe2010, FE2010_IRQ_FLOPPY_DISK);
    ((fdc9268_t *)fdc)->pending_irq = true;
    fdc_msr_set(fdc, FDC_MSR_DIO);
    fdc_state_set(fdc, FDC_STATE_CMD_WRITE_ST0);
    break;

  case FDC_STATE_CMD_READ_DS:
    fdc_trace("FIFO write: Read/DS: 0x%02x\n", value);
    fdc_st0_drive_sel_update(fdc, value);
    fdc_state_set(fdc, FDC_STATE_CMD_READ_C_PRIOR);
    break;

  case FDC_STATE_CMD_READ_C_PRIOR:
    fdc_trace("FIFO write: Read/C (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_cylinder = value;
    fdc_state_set(fdc, FDC_STATE_CMD_READ_H_PRIOR);
    break;

  case FDC_STATE_CMD_READ_H_PRIOR:
    fdc_trace("FIFO write: Read/H (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_head = value;
    fdc_state_set(fdc, FDC_STATE_CMD_READ_R_PRIOR);
    break;

  case FDC_STATE_CMD_READ_R_PRIOR:
    fdc_trace("FIFO write: Read/R (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_sector = value;
    fdc_state_set(fdc, FDC_STATE_CMD_READ_N_PRIOR);
    break;

  case FDC_STATE_CMD_READ_N_PRIOR:
    fdc_trace("FIFO write: Read/N (Prior): 0x%02x\n", value);
    ((fdc9268_t *)fdc)->cmd_number = value;
    fdc_state_set(fdc, FDC_STATE_CMD_READ_EOT);
    break;

  case FDC_STATE_CMD_READ_EOT:
    fdc_trace("FIFO write: Read/EOT: 0x%02x\n", value);
    fdc_state_set(fdc, FDC_STATE_CMD_READ_GPL);
    break;

  case FDC_STATE_CMD_READ_GPL:
    fdc_trace("FIFO write: Read/GPL: 0x%02x\n", value);
    fdc_state_set(fdc, FDC_STATE_CMD_READ_DTL);
    break;

  case FDC_STATE_CMD_READ_DTL:
    fdc_trace("FIFO write: Read/DTL: 0x%02x\n", value);
    if (fdc_image_dma(fdc, true)) {
      /* Clear error bits if data was read. */
      fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS0);
      fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS1);
      ((fdc9268_t *)fdc)->st1 = 0;
    } else {
      /* Report error and sector not found if reading failed. */
      fdc_st0_set(fdc, FDC_ST0_CMD_STATUS0);
      fdc_st0_clear(fdc, FDC_ST0_CMD_STATUS1);
      ((fdc9268_t *)fdc)->st1 = (1 << FDC_ST1_SECTOR_NOT_FOUND);
    }
    fe2010_irq(((fdc9268_t *)fdc)->fe2010, FE2010_IRQ_FLOPPY_DISK);
    ((fdc9268_t *)fdc)->pending_irq = true;
    fdc_msr_set(fdc, FDC_MSR_DIO);
    fdc_state_set(fdc, FDC_STATE_CMD_READ_ST0);
    break;

  default:
    panic("Unexpected FDC FIFO write! (0x%02x)\n", value);
    break;
  }
}



void fdc9268_init(fdc9268_t *fdc, io_t *io, fe2010_t *fe2010)
{
  int i;

  memset(fdc, 0, sizeof(fdc9268_t));
  fdc->fe2010 = fe2010;
  fdc_reset(fdc);

  io->write[FDC_DOR].func = fdc_dor_write;
  io->write[FDC_DOR].cookie = fdc;
  io->read[FDC_MSR].func = fdc_msr_read;
  io->read[FDC_MSR].cookie = fdc;
  io->read[FDC_FIFO].func = fdc_fifo_read;
  io->read[FDC_FIFO].cookie = fdc;
  io->write[FDC_FIFO].func = fdc_fifo_write;
  io->write[FDC_FIFO].cookie = fdc;

  for (i = 0; i < FDC_TRACE_BUFFER_SIZE; i++) {
    fdc_trace_buffer[i][0] = '\0';
  }
  fdc_trace_buffer_n = 0;
}



void fdc9268_trace_dump(FILE *fh)
{
  int i;

  for (i = fdc_trace_buffer_n; i < FDC_TRACE_BUFFER_SIZE; i++) {
    if (fdc_trace_buffer[i][0] != '\0') {
      fprintf(fh, fdc_trace_buffer[i]);
    }
  }
  for (i = 0; i < fdc_trace_buffer_n; i++) {
    if (fdc_trace_buffer[i][0] != '\0') {
      fprintf(fh, fdc_trace_buffer[i]);
    }
  }
}



int fdc9268_image_load(fdc9268_t *fdc, int ds, const char *filename,
  int spt_override)
{
  FILE *fh;
  size_t n;
  int c;

  fdc->floppy[ds].loaded = false;

  fh = fopen(filename, "rb");
  if (fh == NULL) {
    console_exit();
    fprintf(stderr, "fopen() for '%s' failed with errno: %d\n",
      filename, errno);
    return -1;
  }

  n = 0;
  while ((c = fgetc(fh)) != EOF) {
    if (n >= FLOPPY_SIZE_MAX) {
      console_exit();
      fprintf(stderr, "Too large floppy image: '%s'\n", filename);
      fclose(fh);
      return -1;
    }
    fdc->floppy[ds].data[n] = c;
    n++;
  }
  fclose(fh);
  fdc->floppy[ds].size = n;

  if (spt_override > 0) {
    fdc->floppy[ds].spt = spt_override;
  } else {
    /* Try to autodetect based on offset in Volume Boot Record. */
    fdc->floppy[ds].spt = fdc->floppy[ds].data[0x18];

    /* 9 = 720K, 18 = 1.44M, 36 = 2.88M. */
    if (fdc->floppy[ds].spt != 9 &&
        fdc->floppy[ds].spt != 18 &&
        fdc->floppy[ds].spt != 36) {
      console_exit();
      fprintf(stderr, "Unknown sectors-per-track for floppy image: '%s'\n",
        filename);
      return -1;
    }
  }

  strncpy(fdc->floppy[ds].loaded_filename, filename, PATH_MAX);
  fdc->floppy[ds].loaded = true;
  return 0;
}



int fdc9268_image_save(fdc9268_t *fdc, int ds, const char *filename)
{
  FILE *fh;
  size_t n;

  if (fdc->floppy[ds].loaded == false) {
    console_exit();
    fprintf(stderr, "No image loaded!\n");
    return -2;
  }

  if (filename == NULL) {
    fh = fopen(fdc->floppy[ds].loaded_filename, "wb");
  } else {
    fh = fopen(filename, "wb");
  }
  if (fh == NULL) {
    console_exit();
    fprintf(stderr, "fopen() for '%s' failed with errno: %d\n",
      filename, errno);
    return -1;
  }

  for (n = 0; n < fdc->floppy[ds].size; n++) {
    fputc(fdc->floppy[ds].data[n], fh);
  }
  fclose(fh);

  if (filename != NULL) {
    strncpy(fdc->floppy[ds].loaded_filename, filename, PATH_MAX);
  }
  return 0;
}



void fdc9268_image_eject(fdc9268_t *fdc, int ds)
{
  fdc->floppy[ds].loaded = false;
  fdc->floppy[ds].loaded_filename[0] = '\0';
}



