#ifndef _XTHDC_H
#define _XTHDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "io.h"
#include "fe2010.h"

/* Hard disk C/H/S always set to 20MB Western Digital 93024-X */
#define DISK_CYLINDERS   615
#define DISK_HEADS       4
#define DISK_SECTORS     17
#define DISK_SECTOR_SIZE 512

#define DISK_SIZE (DISK_CYLINDERS * \
                   DISK_HEADS * \
                   DISK_SECTORS * \
                   DISK_SECTOR_SIZE)

typedef enum {
  XTHDC_STATE_IDLE,
  XTHDC_STATE_COMMAND,
  XTHDC_STATE_COMMAND_PARAM_1,
  XTHDC_STATE_COMMAND_PARAM_2,
  XTHDC_STATE_COMMAND_PARAM_3,
  XTHDC_STATE_COMMAND_PARAM_4,
  XTHDC_STATE_COMMAND_PARAM_5,
  XTHDC_STATE_INITIALIZE_DRIVE,
  XTHDC_STATE_READ_SECTOR,
  XTHDC_STATE_STATUS,
} xthdc_state_t;

typedef struct xthdc_s {
  xthdc_state_t state;
  uint8_t status;
  uint8_t mask;
  uint8_t config;
  uint8_t command[6];
  uint8_t command_status;

  uint8_t drive;
  uint16_t cylinder;
  uint8_t head;
  uint8_t sector;
  uint16_t byte_no;

  bool loaded;
  uint8_t data[DISK_SIZE];

  fe2010_t* fe2010;
} xthdc_t;

void xthdc_init(xthdc_t *xthdc, io_t *io, fe2010_t *fe2010);
void xthdc_trace_dump(FILE *fh);
int xthdc_image_load(xthdc_t *xthdc, const char *filename);
int xthdc_image_save(xthdc_t *xthdc, const char *filename);

#endif /* _XTHDC_H */
