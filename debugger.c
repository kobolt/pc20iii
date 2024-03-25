#include "debugger.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "i8088.h"
#include "i8088_trace.h"
#include "mem.h"
#include "fe2010.h"
#include "fdc9268.h"
#include "xthdc.h"

#define DEBUGGER_ARGS 3



static void debugger_help(void)
{ 
  fprintf(stdout, "Debugger Commands:\n");
  fprintf(stdout, "  q              - Quit\n");
  fprintf(stdout, "  ? | h          - Help\n");
  fprintf(stdout, "  c              - Continue\n");
  fprintf(stdout, "  s              - Step\n");
  fprintf(stdout, "  t [extended]   - CPU Trace\n");
  fprintf(stdout, "  i              - Interrupt Trace\n");
  fprintf(stdout, "  d <addr> [end] - Dump Memory\n");
  fprintf(stdout, "  g              - FE2010 Status\n");
  fprintf(stdout, "  f              - FDC9268 Trace\n");
  fprintf(stdout, "  x              - XT HDC Trace\n");
  fprintf(stdout, "  a <filename>   - Load Floppy A:\n");
  fprintf(stdout, "  b <filename>   - Load Floppy B:\n");
  fprintf(stdout, "  A <filename>   - Save Floppy A:\n");
  fprintf(stdout, "  B <filename>   - Save Floppy B:\n");
}



bool debugger(i8088_t *cpu, mem_t *mem, fe2010_t *fe2010, fdc9268_t *fdc9268)
{
  char input[128];
  char *argv[DEBUGGER_ARGS];
  int argc;
  int value1;
  int value2;

  fprintf(stdout, "\n");
  while (1) {
    fprintf(stdout, "%04X:%04X> ", cpu->cs, cpu->ip);

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
        fdc9268_image_save(fdc9268, 0, argv[1]);
      } else {
        fprintf(stdout, "Missing argument!\n");
      }

    } else if (strncmp(argv[0], "B", 1) == 0) {
      if (argc >= 2) {
        fdc9268_image_save(fdc9268, 1, argv[1]);
      } else {
        fprintf(stdout, "Missing argument!\n");
      }
    }
  }
} 



