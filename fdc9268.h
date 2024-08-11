#ifndef _FDC9268_H
#define _FDC9268_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include "io.h"
#include "fe2010.h"

#define FLOPPY_SIZE_MAX 2949120 /* 2.88M Format */

typedef enum {
  FDC_STATE_IDLE,
  FDC_STATE_CMD_SIS_ST0,
  FDC_STATE_CMD_SIS_PCN,
  FDC_STATE_CMD_SPECIFY_SRT_HUT,
  FDC_STATE_CMD_SPECIFY_HLT_ND,
  FDC_STATE_CMD_RECALIB_DS,
  FDC_STATE_CMD_SEEK_DS,
  FDC_STATE_CMD_SEEK_NCN,
  FDC_STATE_CMD_SDS_DS,
  FDC_STATE_CMD_SDS_ST3,

  FDC_STATE_CMD_WRITE_DS,
  FDC_STATE_CMD_WRITE_C_PRIOR,
  FDC_STATE_CMD_WRITE_H_PRIOR,
  FDC_STATE_CMD_WRITE_R_PRIOR,
  FDC_STATE_CMD_WRITE_N_PRIOR,
  FDC_STATE_CMD_WRITE_EOT,
  FDC_STATE_CMD_WRITE_GPL,
  FDC_STATE_CMD_WRITE_DTL,
  FDC_STATE_CMD_WRITE_ST0,
  FDC_STATE_CMD_WRITE_ST1,
  FDC_STATE_CMD_WRITE_ST2,
  FDC_STATE_CMD_WRITE_C_AFTER,
  FDC_STATE_CMD_WRITE_H_AFTER,
  FDC_STATE_CMD_WRITE_R_AFTER,
  FDC_STATE_CMD_WRITE_N_AFTER,

  FDC_STATE_CMD_READ_DS,
  FDC_STATE_CMD_READ_C_PRIOR,
  FDC_STATE_CMD_READ_H_PRIOR,
  FDC_STATE_CMD_READ_R_PRIOR,
  FDC_STATE_CMD_READ_N_PRIOR,
  FDC_STATE_CMD_READ_EOT,
  FDC_STATE_CMD_READ_GPL,
  FDC_STATE_CMD_READ_DTL,
  FDC_STATE_CMD_READ_ST0,
  FDC_STATE_CMD_READ_ST1,
  FDC_STATE_CMD_READ_ST2,
  FDC_STATE_CMD_READ_C_AFTER,
  FDC_STATE_CMD_READ_H_AFTER,
  FDC_STATE_CMD_READ_R_AFTER,
  FDC_STATE_CMD_READ_N_AFTER,
} fdc9268_state_t;

typedef struct floppy_s {
  bool loaded;
  char loaded_filename[PATH_MAX];
  uint8_t data[FLOPPY_SIZE_MAX];
  uint8_t spt; /* Sectors Per Track */
  size_t size; /* Actual size used. */
  size_t pos; /* Current position during DMA transfer. */
} floppy_t;

typedef struct fdc9268_s {
  fdc9268_state_t state;
  uint8_t msr; /* Main Status Register */
  uint8_t st0; /* Status 0 */
  uint8_t st1; /* Status 1 */
  uint8_t st2; /* Status 2 */
  uint8_t st3; /* Status 3 */
  uint8_t pcn; /* Present Cylinder Number */
  bool pending_irq;
  bool dor_reset;

  uint8_t cmd_cylinder;
  uint8_t cmd_head;
  uint8_t cmd_sector;
  uint8_t cmd_number;

  floppy_t floppy[4];

  fe2010_t* fe2010;
} fdc9268_t;

void fdc9268_init(fdc9268_t *fdc, io_t *io, fe2010_t *fe2010);
void fdc9268_trace_dump(FILE *fh);
int fdc9268_image_load(fdc9268_t *fdc, int ds, const char *filename,
  int spt_override);
int fdc9268_image_save(fdc9268_t *fdc, int ds, const char *filename);

#endif /* _FDC9268_H */
