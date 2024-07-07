#ifndef _DP8390_H
#define _DP8390_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "fe2010.h"
#include "net.h"
#include "io.h"

typedef struct dp8390_s {
  uint8_t bnry;
  uint8_t cr;
  uint8_t curr;
  uint8_t imr;
  uint8_t isr;
  uint8_t pstart;
  uint8_t pstop;
  uint8_t tcr;
  uint8_t tsr;
  uint16_t clda;
  uint16_t crda;
  uint16_t rbcr;
  uint16_t rsar;
  uint16_t tbcr;
  uint16_t tpsr;

  uint8_t ring[UINT16_MAX + 1];

  net_t *net;
  fe2010_t* fe2010;
} dp8390_t;

void dp8390_init(dp8390_t *dp8390, io_t *io, fe2010_t *fe2010, net_t *net);
void dp8390_trace_dump(FILE *fh);
void dp8390_execute(dp8390_t *dp8390);

#endif /* _DP8390_H */
