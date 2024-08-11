#ifndef _EDFS_H
#define _EDFS_H

#include <stdint.h>
#include "net.h"

void edfs_init(const char *root);
void edfs_handle_packet(net_t *net, uint8_t tx_frame[], uint16_t tx_len);
void edfs_trace_dump(FILE *fh);

#endif /* _EDFS_H */
