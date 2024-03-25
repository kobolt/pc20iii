#ifndef _MEM_H
#define _MEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define MEM_SIZE_MAX 0x100000
#define MEM_SECTION 0x2000 /* 8192 bytes */

typedef struct mem_s {
  uint8_t m[MEM_SIZE_MAX];
  bool readonly[MEM_SIZE_MAX / MEM_SECTION];
} mem_t;

void mem_init(mem_t *mem);
uint8_t mem_read(mem_t *mem, uint32_t address);
uint8_t mem_read_by_segment(mem_t *mem, uint16_t segment, uint16_t offset);
void mem_write(mem_t *mem, uint32_t address, uint8_t value);
void mem_write_by_segment(mem_t *mem, uint16_t segment, uint16_t offset,
  uint8_t value);
int mem_load_rom(mem_t *mem, const char *filename, uint32_t address);
void mem_dump(FILE *fh, mem_t *mem, uint32_t start, uint32_t end);

#endif /* _MEM_H */
