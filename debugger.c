#include "debugger.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "i8088.h"
#include "i8088_trace.h"
#include "mem.h"
#include "fe2010.h"
#include "fdc9268.h"
#include "xthdc.h"
#include "i8250.h"
#include "dp8390.h"
#include "net.h"
#include "edfs.h"

#define DEBUGGER_ARGS 3

#ifdef BREAKPOINT
int32_t debugger_breakpoint_cs = -1;
int32_t debugger_breakpoint_ip = -1;
#endif



static void debugger_help(void)
{ 
  fprintf(stdout, "Debugger Commands:\n");
  fprintf(stdout, "  q              - Quit\n");
  fprintf(stdout, "  ? | h          - Help\n");
  fprintf(stdout, "  c              - Continue\n");
  fprintf(stdout, "  s              - Step\n");
#ifdef BREAKPOINT
  fprintf(stdout, "  k <addr>       - Breakpoint\n");
#endif /* BREAKPOINT */
  fprintf(stdout, "  t [extended]   - CPU Trace\n");
  fprintf(stdout, "  i              - Interrupt Trace\n");
  fprintf(stdout, "  d <addr> [end] - Dump Memory\n");
  fprintf(stdout, "  g              - FE2010 Status\n");
  fprintf(stdout, "  f              - FDC9268 Trace\n");
  fprintf(stdout, "  x              - XT HDC Trace\n");
  fprintf(stdout, "  e              - COM1/8250 Trace\n");
  fprintf(stdout, "  p              - DP8390 Trace\n");
  fprintf(stdout, "  n              - Network Trace\n");
  fprintf(stdout, "  y              - EtherDFS Trace\n");
  fprintf(stdout, "  a <filename>   - Load Floppy A:\n");
  fprintf(stdout, "  b <filename>   - Load Floppy B:\n");
  fprintf(stdout, "  A [filename]   - Save Floppy A:\n");
  fprintf(stdout, "  B [filename]   - Save Floppy B:\n");
  fprintf(stdout, "  W [filename]   - Save Hard Disk Image\n");
}



static bool debugger_overwrite(FILE *out, FILE *in, const char *filename)
{
  struct stat st;
  char answer[2];

  if (stat(filename, &st) == 0) {
    if (S_ISREG(st.st_mode)) {
      while (1) {
        fprintf(out, "\rOverwrite '%s' (y/n) ? ", filename);
        if (fgets(answer, sizeof(answer), in) == NULL) {
          if (feof(stdin)) {
            return false;
          }
        } else {
          if (answer[0] == 'y') {
            return true;
          } else if (answer[0] == 'n') {
            return false;
          }
        }
      }

    } else {
      fprintf(out, "Filename is not a file!\n");
      return false;
    }

  } else {
    if (errno == ENOENT) {
      return true; /* File not found, OK to write. */

    } else {
      fprintf(out, "stat() failed with errno: %d\n", errno);
      return false;
    }
  }
}



bool debugger(i8088_t *cpu, mem_t *mem, fe2010_t *fe2010,
  fdc9268_t *fdc9268, xthdc_t *xthdc)
{
  char input[512];
  char *argv[DEBUGGER_ARGS];
  int argc;
  int value1;
  int value2;

  fprintf(stdout, "\n");
  while (1) {
    fprintf(stdout, "\r%04X:%04X> ", cpu->cs, cpu->ip);

    if (fgets(input, sizeof(input), stdin) == NULL) {
      if (feof(stdin)) {
        exit(EXIT_SUCCESS);
      }
      continue;
    }

    if ((strlen(input) > 0) && (input[strlen(input) - 1] == '\n')) {
      input[strlen(input) - 1] = '\0'; /* Strip newline. */
    }

    argv[0] = strtok(input, " ");
    if (argv[0] == NULL) {
      continue;
    }

    for (argc = 1; argc < DEBUGGER_ARGS; argc++) {
      argv[argc] = strtok(NULL, " ");
      if (argv[argc] == NULL) {
        break;
      }
    }

    if (strncmp(argv[0], "q", 1) == 0) {
      exit(EXIT_SUCCESS);
 
    } else if (strncmp(argv[0], "?", 1) == 0) {
      debugger_help();

    } else if (strncmp(argv[0], "h", 1) == 0) {
      debugger_help();

    } else if (strncmp(argv[0], "c", 1) == 0) {
      return false;

    } else if (strncmp(argv[0], "s", 1) == 0) {
      return true;

#ifdef BREAKPOINT
    } else if (strncmp(argv[0], "k", 1) == 0) {
      if (argc >= 2) {
        if (sscanf(argv[1], "%4x:%4x", &value1, &value2) == 2) {
          debugger_breakpoint_cs = (value1 & 0xFFFF);
          debugger_breakpoint_ip = (value2 & 0xFFFF);
          fprintf(stdout, "Breakpoint at %04X:%04X set.\n",
            debugger_breakpoint_cs, debugger_breakpoint_ip);
        } else {
          debugger_breakpoint_ip = (value1 & 0xFFFF);
          fprintf(stdout, "Breakpoint at *:%04X set.\n",
            debugger_breakpoint_ip);
        }
      } else {
        if (debugger_breakpoint_ip < 0) {
          fprintf(stdout, "Missing argument!\n");
        } else {
          if (debugger_breakpoint_cs < 0) {
            fprintf(stdout, "Breakpoint at *:%04X removed.\n",
              debugger_breakpoint_ip);
          } else {
            fprintf(stdout, "Breakpoint at %04X:%04X removed.\n",
              debugger_breakpoint_cs, debugger_breakpoint_ip);
          }
        }
        debugger_breakpoint_ip = -1;
        debugger_breakpoint_cs = -1;
      }
#endif /* BREAKPOINT */

    } else if (strncmp(argv[0], "t", 1) == 0) {
      if (argc >= 2 && strlen(argv[1]) > 0) {
        i8088_trace_dump(stdout, true);
      } else {
        i8088_trace_dump(stdout, false);
      }

    } else if (strncmp(argv[0], "i", 1) == 0) {
      i8088_trace_int_dump(stdout);

    } else if (strncmp(argv[0], "d", 1) == 0) {
      if (argc >= 3) {
        sscanf(argv[1], "%5x", &value1);
        sscanf(argv[2], "%5x", &value2);
        mem_dump(stdout, mem, (uint32_t)value1, (uint32_t)value2);
      } else if (argc >= 2) {
        sscanf(argv[1], "%5x", &value1);
        value2 = value1 + 0xFF;
        if (value2 > 0xFFFFF) {
          value2 = 0xFFFFF; /* Truncate */
        }
        mem_dump(stdout, mem, (uint32_t)value1, (uint32_t)value2);
      } else {
        fprintf(stdout, "Missing argument!\n");
      }

    } else if (strncmp(argv[0], "g", 1) == 0) {
      fe2010_dump(stdout, fe2010);

    } else if (strncmp(argv[0], "f", 1) == 0) {
      fdc9268_trace_dump(stdout);

    } else if (strncmp(argv[0], "x", 1) == 0) {
      xthdc_trace_dump(stdout);

    } else if (strncmp(argv[0], "e", 1) == 0) {
      i8250_trace_dump(stdout);

    } else if (strncmp(argv[0], "p", 1) == 0) {
      dp8390_trace_dump(stdout);

    } else if (strncmp(argv[0], "n", 1) == 0) {
      net_trace_dump(stdout);

    } else if (strncmp(argv[0], "y", 1) == 0) {
      edfs_trace_dump(stdout);

    } else if (strncmp(argv[0], "a", 1) == 0) {
      if (argc >= 2) {
        fdc9268_image_load(fdc9268, 0, argv[1], 0);
      } else {
        fprintf(stdout, "Missing argument!\n");
      }

    } else if (strncmp(argv[0], "b", 1) == 0) {
      if (argc >= 2) {
        fdc9268_image_load(fdc9268, 1, argv[1], 0);
      } else {
        fprintf(stdout, "Missing argument!\n");
      }

    } else if (strncmp(argv[0], "A", 1) == 0) {
      if (argc >= 2) {
        if (debugger_overwrite(stdout, stdin, argv[1])) {
          fdc9268_image_save(fdc9268, 0, argv[1]);
        }
      } else {
        if (debugger_overwrite(stdout, stdin,
          fdc9268->floppy[0].loaded_filename)) {
          fdc9268_image_save(fdc9268, 0, NULL);
        }
      }

    } else if (strncmp(argv[0], "B", 1) == 0) {
      if (argc >= 2) {
        if (debugger_overwrite(stdout, stdin, argv[1])) {
          fdc9268_image_save(fdc9268, 1, argv[1]);
        }
      } else {
        if (debugger_overwrite(stdout, stdin,
          fdc9268->floppy[1].loaded_filename)) {
          fdc9268_image_save(fdc9268, 1, NULL);
        }
      }

    } else if (strncmp(argv[0], "W", 1) == 0) {
      if (argc >= 2) {
        if (debugger_overwrite(stdout, stdin, argv[1])) {
          xthdc_image_save(xthdc, argv[1]);
        }
      } else {
        if (debugger_overwrite(stdout, stdin, xthdc->loaded_filename)) {
          xthdc_image_save(xthdc, NULL);
        }
      }
    }
  }
} 



