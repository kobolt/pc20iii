#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#ifdef CPU_RELAX
#include <poll.h>
#endif /* CPU_RELAX */

#include "i8088.h"
#include "i8088_trace.h"
#include "mem.h"
#include "io.h"
#include "fe2010.h"
#include "mos5720.h"
#include "fdc9268.h"
#include "m6242.h"
#include "xthdc.h"
#include "i8250.h"
#include "dp8390.h"
#include "net.h"
#include "console.h"
#include "debugger.h"
#include "panic.h"

#define BIOS_ROM_FILENAME "rom/cbm-pc10sd-bios-v4.38-318085-05-C72A.bin"
#define BIOS_ROM_ADDRESS 0xF8000

static i8088_t cpu;
static mem_t mem;
static io_t io;
static fe2010_t fe2010;
static mos5720_t mos5720;
static fdc9268_t fdc9268;
static m6242_t m6242;
static xthdc_t xthdc;
static i8250_t i8250;
static dp8390_t dp8390;
static net_t net;

static bool debugger_break = false;
static char panic_msg[80];



void panic(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(panic_msg, sizeof(panic_msg), format, args);
  va_end(args);

  debugger_break = true;
}



static void sig_handler(int sig)
{
  switch (sig) {
  case SIGINT:
    debugger_break = true;
    return;
  }
}



static void display_help(const char *progname)
{
  fprintf(stdout, "Usage: %s <options>\n", progname);
  fprintf(stdout, "Options:\n"
    "  -h        Display this help.\n"
    "  -a FILE   Load floppy image FILE into floppy drive A:\n"
    "  -b FILE   Load floppy image FILE into floppy drive B:\n"
    "  -w FILE   Load hard disk image FILE for C:\n"
    "  -s SPT    Override SPT sectors-per-track for floppy images.\n"
    "  -r FILE   Use FILE for BIOS ROM instead of the default.\n"
    "  -x ADDR   Load BIOS ROM at (hex) ADDR instead of the default.\n"
    "  -t TTY    Passthrough COM1 to TTY device.\n"
    "\n");
  fprintf(stdout,
    "Default BIOS ROM '%s' @ 0x%05x\n", BIOS_ROM_FILENAME, BIOS_ROM_ADDRESS);
  fprintf(stdout,
    "Using Ctrl+C will break into debugger, use 'q' from there to quit.\n\n");
}



int main(int argc, char *argv[])
{
  int c;
  uint32_t cycle;
  char *bios_rom_filename = BIOS_ROM_FILENAME;
  uint32_t bios_rom_address = BIOS_ROM_ADDRESS;
  char *floppy_a_image = NULL;
  char *floppy_b_image = NULL;
  char *hard_disk_image = NULL;
  char *tty_device = NULL;
  int floppy_image_spt = 0;

  panic_msg[0] = '\0';
  signal(SIGINT, sig_handler);

  while ((c = getopt(argc, argv, "ha:b:w:s:r:x:t:")) != -1) {
    switch (c) {
    case 'h':
      display_help(argv[0]);
      return EXIT_SUCCESS;

    case 'a':
      floppy_a_image = optarg;
      break;

    case 'b':
      floppy_b_image = optarg;
      break;

    case 'w':
      hard_disk_image = optarg;
      break;

    case 's':
      floppy_image_spt = atoi(optarg);
      break;

    case 'r':
      bios_rom_filename = optarg;
      break;

    case 'x':
      sscanf(optarg, "%x", &bios_rom_address);
      break;

    case 't':
      tty_device = optarg;
      break;

    case '?':
    default:
      display_help(argv[0]);
      return EXIT_FAILURE;
    }
  }

  i8088_trace_init();
  i8088_init(&cpu, &io);
  mem_init(&mem);
  io_init(&io);

  fe2010_init(&fe2010, &io, &cpu, &mem);
  mos5720_init(&mos5720, &io, &fe2010);
  fdc9268_init(&fdc9268, &io, &fe2010);
  m6242_init(&m6242, &io);
  net_init(&net);
  dp8390_init(&dp8390, &io, &fe2010, &net);

  if (tty_device) {
    if (i8250_init(&i8250, &io, &fe2010, &mos5720, tty_device) != 0) {
      return EXIT_FAILURE;
    }
  }

  console_init(&io);

  if (mem_load_rom(&mem, bios_rom_filename, bios_rom_address) != 0) {
    return EXIT_FAILURE;
  }

  if (floppy_a_image) {
    if (fdc9268_image_load(&fdc9268, 0, floppy_a_image,
      floppy_image_spt) != 0) {
      return EXIT_FAILURE;
    }
  }

  if (floppy_b_image) {
    if (fdc9268_image_load(&fdc9268, 1, floppy_b_image,
      floppy_image_spt) != 0) {
      return EXIT_FAILURE;
    }
  }

  if (hard_disk_image) {
    xthdc_init(&xthdc, &io, &fe2010);
    if (xthdc_image_load(&xthdc, hard_disk_image) != 0) {
      return EXIT_FAILURE;
    }
  }

  cycle = 0;
  i8088_reset(&cpu);
  while (1) {
    i8088_execute(&cpu, &mem);
    fe2010_execute(&fe2010);

    if ((cycle % 10000) == 0) {
      console_execute_keyboard(&fe2010, &mos5720);
      console_execute_screen(&mem);
      net_execute(&net);
      dp8390_execute(&dp8390);
    }

    if (tty_device) {
      if ((cycle % 100) == 0) {
        i8250_execute(&i8250);
      }
    }

#ifdef CPU_RELAX
    /* Check if BIOS int16h gets called for keyboard services. */
    if (cpu.cs == (mem.m[0x5A] + (mem.m[0x5B] * 0x100)) &&
        cpu.ip == (mem.m[0x58] + (mem.m[0x59] * 0x100))) {
      console_execute_screen(&mem);
      struct pollfd fds[1];
      fds[0].fd = STDIN_FILENO;
      fds[0].events = POLLIN;
#if CPU_RELAX == CPM
      /* CP/M-86 calls with AH=0 and waits indefinitely. */
      if (cpu.ah == 0) {
        while (poll(fds, 1, 10) == 0);
      }
#else /* CPU_RELAX == DOS */
      /* DOS typically calls with AH=1 to poll once in while. */
      poll(fds, 1, 1);
#endif
    }
#endif /* CPU_RELAX */

#ifdef BREAKPOINT
    if (cpu.ip == debugger_breakpoint_ip) {
      if (cpu.cs == debugger_breakpoint_cs || debugger_breakpoint_cs == -1) {
        debugger_break = true;
      }
    }
#endif /* BREAKPOINT */

    if (debugger_break) {
      console_pause();
      if (panic_msg[0] != '\0') {
        fprintf(stdout, "%s", panic_msg);
        panic_msg[0] = '\0';
      }
      debugger_break = debugger(&cpu, &mem, &fe2010, &fdc9268, &xthdc);
      if (! debugger_break) {
        console_resume();
      }
    }

    cycle++;
  }

  return EXIT_SUCCESS;
}



