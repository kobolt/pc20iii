#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>

#include "console.h"
#include "panic.h"



void mem_init(mem_t *mem)
{
  int i;

  for (i = 0; i < MEM_SIZE_MAX; i++) {
    mem->m[i] = 0x00;
  }
  for (i = 0; i < MEM_SIZE_MAX / MEM_SECTION; i++) {
    mem->readonly[i] = false;
  }
}



uint8_t mem_read(mem_t *mem, uint32_t address)
{
  if (address >= MEM_SIZE_MAX) {
    panic("Memory read above 1MB: 0x%08x\n", address);
    return 0xFF;
  } else {
    return mem->m[address];
  }
}



uint8_t mem_read_by_segment(mem_t *mem, uint16_t segment, uint16_t offset)
{
  return mem_read(mem, ((segment << 4) + offset) & 0xFFFFF);
}



void mem_write(mem_t *mem, uint32_t address, uint8_t value)
{
  if (address >= MEM_SIZE_MAX) {
    panic("Memory write above 1MB: 0x%08x\n", address);
  } else {
    if (mem->readonly[address / MEM_SECTION] == false) {
      mem->m[address] = value;
    }
  }
}



void mem_write_by_segment(mem_t *mem, uint16_t segment, uint16_t offset,
  uint8_t value)
{
  mem_write(mem, ((segment << 4) + offset) & 0xFFFFF, value);
}



int mem_load_rom(mem_t *mem, const char *filename, uint32_t address)
{
  FILE *fh;
  int c;

  if (address >= MEM_SIZE_MAX) {
    return -2;
  }

  fh = fopen(filename, "rb");
  if (fh == NULL) {
    console_exit();
    fprintf(stderr, "fopen() for '%s' failed with errno: %d\n",
      filename, errno);
    return -1;
  }

  while ((c = fgetc(fh)) != EOF) {
    mem->m[address] = c;
    mem->readonly[address / MEM_SECTION] = true;
    address++;
    if (address >= MEM_SIZE_MAX) {
      fclose(fh);
      return 0;
    }
  }

  fclose(fh);
  return 0;
}



static void mem_dump_16(FILE *fh, mem_t *mem, uint32_t start, uint32_t end)
{
  int i;
  uint32_t address;
  uint8_t value;

  fprintf(fh, "%05x   ", start & 0xFFFF0);

  /* Hex */
  for (i = 0; i < 16; i++) {
    address = (start & 0xFFFF0) + i;
    value = mem_read(mem, address);
    if ((address >= start) && (address <= end)) {
      fprintf(fh, "%02x ", value);
    } else {
      fprintf(fh, "   ");
    }
    if (i % 4 == 3) {
      fprintf(fh, " ");
    }
  }

  /* Character */
  for (i = 0; i < 16; i++) {
    address = (start & 0xFFFF0) + i;
    value = mem_read(mem, address);
    if ((address >= start) && (address <= end)) {
      if (isprint(value)) {
        fprintf(fh, "%c", value);
      } else {
        fprintf(fh, ".");
      }
    } else {
      fprintf(fh, " ");
    }
  }

  fprintf(fh, "\n");
}



void mem_dump(FILE *fh, mem_t *mem, uint32_t start, uint32_t end)
{
  uint32_t i;
  mem_dump_16(fh, mem, start, end);
  for (i = (start & 0xFFFF0) + 16; i <= end; i += 16) {
    mem_dump_16(fh, mem, i, end);
  }
}



