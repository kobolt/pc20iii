#include "console.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <curses.h>

#include "mem.h"
#include "fe2010.h"
#include "mos5720.h"

#define CGA_CRTC_SELECT     0x3D4
#define CGA_CRTC_REGISTER   0x3D5
#define CGA_MODE_REGISTER   0x3D8
#define CGA_STATUS_REGISTER 0x3DA

#define CONSOLE_SCANCODE_FIFO_SIZE 8

static const short console_color_map[8] = {
  COLOR_BLACK,
  COLOR_BLUE,
  COLOR_GREEN,
  COLOR_CYAN,
  COLOR_RED,
  COLOR_MAGENTA,
  COLOR_YELLOW,
  COLOR_WHITE,
};

static uint8_t console_cga_mode = 0;
static uint8_t console_crtc_register_select = 0;
static uint8_t console_crtc_register[UINT8_MAX] = {0};

static uint8_t console_scancode_fifo[CONSOLE_SCANCODE_FIFO_SIZE] = {0};
static int console_scancode_fifo_head = 0;
static int console_scancode_fifo_tail = 0;



static uint8_t console_scancode_fifo_read(void)
{
  uint8_t scancode;

  if (console_scancode_fifo_tail == console_scancode_fifo_head) {
    return 0; /* Empty */
  }

  scancode = console_scancode_fifo[console_scancode_fifo_tail];
  console_scancode_fifo_tail = (console_scancode_fifo_tail + 1) %
    CONSOLE_SCANCODE_FIFO_SIZE;

  return scancode;
}



static void console_scancode_fifo_write(uint8_t scancode)
{
  if (((console_scancode_fifo_head + 1) %
    CONSOLE_SCANCODE_FIFO_SIZE) == console_scancode_fifo_tail) {
    return; /* Full */
  }

  console_scancode_fifo[console_scancode_fifo_head] = scancode;
  console_scancode_fifo_head = (console_scancode_fifo_head + 1) %
    CONSOLE_SCANCODE_FIFO_SIZE;
}



static chtype console_graphic(uint8_t byte)
{
  if (byte >= 0x20 && byte < 0x7F) {
    return byte; /* Return all ASCII symbols as-is. */
  }

  /* Try to convert from code page 437 to curses symbol. */
  switch (byte) {
  case 0xB3:
  case 0xBA:
    return ACS_VLINE;
  case 0xC4:
  case 0xCD:
    return ACS_HLINE;
  case 0xC5:
  case 0xCE:
  case 0xD7:
  case 0xD8:
    return ACS_PLUS;
  case 0xC9:
  case 0xD5:
  case 0xD6:
  case 0xDA:
    return ACS_ULCORNER;
  case 0xB7:
  case 0xB8:
  case 0xBB:
  case 0xBF:
    return ACS_URCORNER;
  case 0xC0:
  case 0xC8:
  case 0xD3:
  case 0xD4:
    return ACS_LLCORNER;
  case 0xBC:
  case 0xBD:
  case 0xBE:
  case 0xD9:
    return ACS_LRCORNER;
  case 0xC1:
  case 0xCA:
  case 0xCF:
  case 0xD0:
    return ACS_BTEE;
  case 0xC2:
  case 0xCB:
  case 0xD1:
  case 0xD2:
    return ACS_TTEE;
  case 0xC3:
  case 0xC6:
  case 0xC7:
  case 0xCC:
    return ACS_LTEE;
  case 0xB4:
  case 0xB5:
  case 0xB6:
  case 0xB9:
    return ACS_RTEE;
  case 0xB0:
  case 0xB1:
  case 0xB2:
    return ACS_CKBOARD;
  case 0xDB:
    return ACS_BLOCK;
  case 0x07:
  case 0x09:
  case 0x0A:
    return ACS_BULLET;
  case 0x19:
  case 0x1F:
    return ACS_DARROW;
  case 0x18:
  case 0x1E:
    return ACS_UARROW;
  case 0x1B:
    return ACS_LARROW;
  case 0x1A:
    return ACS_RARROW;
  case 0xF8:
    return ACS_DEGREE;
  case 0xF2:
    return ACS_GEQUAL;
  case 0xF3:
    return ACS_LEQUAL;
  case 0xE3:
    return ACS_PI;
  case 0xF1:
    return ACS_PLMINUS;
  default:
    break;
  }
  return '.'; /* Unknown */
}



static uint8_t console_xt_keyboard_scancode(int ch)
{
  switch (ch) {
  case '1': return 0x02;
  case '2': return 0x03;
  case '3': return 0x04;
  case '4': return 0x05;
  case '5': return 0x06;
  case '6': return 0x07;
  case '7': return 0x08;
  case '8': return 0x09;
  case '9': return 0x0A;
  case '0': return 0x0B;

  case 'a': return 0x1E;
  case 'b': return 0x30;
  case 'c': return 0x2E;
  case 'd': return 0x20;
  case 'e': return 0x12;
  case 'f': return 0x21;
  case 'g': return 0x22;
  case 'h': return 0x23;
  case 'i': return 0x17;
  case 'j': return 0x24;
  case 'k': return 0x25;
  case 'l': return 0x26;
  case 'm': return 0x32;
  case 'n': return 0x31;
  case 'o': return 0x18;
  case 'p': return 0x19;
  case 'q': return 0x10;
  case 'r': return 0x13;
  case 's': return 0x1F;
  case 't': return 0x14;
  case 'u': return 0x16;
  case 'v': return 0x2F;
  case 'w': return 0x11;
  case 'x': return 0x2D;
  case 'y': return 0x15;
  case 'z': return 0x2C;

  case KEY_F(1):  return 0x3B;
  case KEY_F(2):  return 0x3C;
  case KEY_F(3):  return 0x3D;
  case KEY_F(4):  return 0x3E;
  case KEY_F(5):  return 0x3F;
  case KEY_F(6):  return 0x40;
  case KEY_F(7):  return 0x41;
  case KEY_F(8):  return 0x42;
  case KEY_F(9):  return 0x43;
  case KEY_F(10): return 0x44;

  case KEY_BACKSPACE: return 0x0E;
  case KEY_UP:        return 0x48;
  case KEY_DOWN:      return 0x50;
  case KEY_LEFT:      return 0x4B;
  case KEY_RIGHT:     return 0x4D;
  case KEY_HOME:      return 0x47;
  case KEY_END:       return 0x4F;
  case KEY_NPAGE:     return 0x51;
  case KEY_PPAGE:     return 0x49;

  case ' ':  return 0x39;
  case ',':  return 0x33;
  case '-':  return 0x0C;
  case '.':  return 0x34;
  case '/':  return 0x35;
  case ';':  return 0x27;
  case '=':  return 0x0D;
  case '[':  return 0x1A;
  case '\'': return 0x28;
  case '\\': return 0x2B;
  case '\n': return 0x1C;
  case '\t': return 0x0F;
  case ']':  return 0x1B;
  case '`':  return 0x29;
  case 0x1B: return 0x01;

  case '!': return 0x02;
  case '@': return 0x03;
  case '#': return 0x04;
  case '$': return 0x05;
  case '%': return 0x06;
  case '^': return 0x07;
  case '&': return 0x08;
  case '*': return 0x09;
  case '(': return 0x0A;
  case ')': return 0x0B;

  case 'A': return 0x1E;
  case 'B': return 0x30;
  case 'C': return 0x2E;
  case 'D': return 0x20;
  case 'E': return 0x12;
  case 'F': return 0x21;
  case 'G': return 0x22;
  case 'H': return 0x23;
  case 'I': return 0x17;
  case 'J': return 0x24;
  case 'K': return 0x25;
  case 'L': return 0x26;
  case 'M': return 0x32;
  case 'N': return 0x31;
  case 'O': return 0x18;
  case 'P': return 0x19;
  case 'Q': return 0x10;
  case 'R': return 0x13;
  case 'S': return 0x1F;
  case 'T': return 0x14;
  case 'U': return 0x16;
  case 'V': return 0x2F;
  case 'W': return 0x11;
  case 'X': return 0x2D;
  case 'Y': return 0x15;
  case 'Z': return 0x2C;

  case '<': return 0x33;
  case '_': return 0x0C;
  case '>': return 0x34;
  case '?': return 0x35;
  case ':': return 0x27;
  case '+': return 0x0D;
  case '{': return 0x1A;
  case '"': return 0x28;
  case '|': return 0x2B;
  case '}': return 0x1B;
  case '~': return 0x29;

  case KEY_F(11): return 0x38; /* Left Alt */

  default:
    return 0;
  }
}



static bool console_character_is_shifted(int ch)
{
  if isupper(ch) {
    return true;
  }

  if (ch == '!' ||
      ch == '@' ||
      ch == '#' ||
      ch == '$' ||
      ch == '%' ||
      ch == '^' ||
      ch == '&' ||
      ch == '*' ||
      ch == '(' ||
      ch == ')' ||
      ch == '<' ||
      ch == '_' ||
      ch == '>' ||
      ch == '?' ||
      ch == ':' ||
      ch == '+' ||
      ch == '{' ||
      ch == '"' ||
      ch == '|' ||
      ch == '}' ||
      ch == '~') {
    return true;
  }

  return false;
}



void console_pause(void)
{
  endwin();
  timeout(-1);
}



void console_resume(void)
{
  timeout(0);
  refresh();
}



void console_exit(void)
{
  endwin();
}



static uint8_t cga_status_read(void *dummy, uint16_t port)
{
  (void)dummy;
  (void)port;
  static bool toggle = false;
  if (toggle) {
    toggle = false;
    return 0x00;
  } else {
    toggle = true;
    return 0x09; /* Toggle retrace and vsync status bits. */
  }
}



static void cga_mode_write(void *dummy, uint16_t port, uint8_t value)
{
  (void)dummy;
  (void)port;
  console_cga_mode = value;
}



static void cga_crtc_select_write(void *dummy, uint16_t port, uint8_t value)
{
  (void)dummy;
  (void)port;
  console_crtc_register_select = value;
}



static void cga_crtc_register_write(void *dummy, uint16_t port, uint8_t value)
{
  (void)dummy;
  (void)port;
  console_crtc_register[console_crtc_register_select] = value;
}



static uint8_t cga_crtc_register_read(void *dummy, uint16_t port)
{
  (void)dummy;
  (void)port;
  return console_crtc_register[console_crtc_register_select];
}



void console_init(io_t *io)
{
  int bg;
  int fg;

  io->read[CGA_STATUS_REGISTER].func = cga_status_read;
  io->write[CGA_MODE_REGISTER].func  = cga_mode_write;
  io->write[CGA_CRTC_SELECT].func    = cga_crtc_select_write;
  io->write[CGA_CRTC_REGISTER].func  = cga_crtc_register_write;
  io->read[CGA_CRTC_REGISTER].func   = cga_crtc_register_read;

  initscr();
  atexit(console_exit);
  noecho();
  keypad(stdscr, TRUE);
  timeout(0);
#ifdef NCURSES_MOUSE_VERSION
  mousemask(ALL_MOUSE_EVENTS, NULL);
#endif /* NCURSES_MOUSE_VERSION */

  if (has_colors()) {
    start_color();
    for (bg = 0; bg < 8; bg++) {
      for (fg = 0; fg < 8; fg++) {
        init_pair((bg * 8) + fg + 1,
          console_color_map[fg], console_color_map[bg]);
      }
    }
  }
}



void console_execute_keyboard(fe2010_t *fe2010, mos5720_t *mos5720)
{
  uint8_t scancode;
  int ch;
#ifdef NCURSES_MOUSE_VERSION
  MEVENT mouse_event;
#endif /* NCURSES_MOUSE_VERSION */

  /* Keyboard scancode handling. */
  scancode = console_scancode_fifo_read();
  if (scancode == 0) {
    /* Nothing in FIFO, check for input. */
    ch = getch();
#ifdef NCURSES_MOUSE_VERSION
    if (ch == KEY_MOUSE) {
      getmouse(&mouse_event);
      if (mouse_event.bstate == BUTTON1_PRESSED) {
        mos5720_mouse_data(mos5720, 0x60); /* Left Button Pressed */
      } else if (mouse_event.bstate == BUTTON3_PRESSED) {
        mos5720_mouse_data(mos5720, 0xC0); /* Right Button Pressed */
      } else {
        mos5720_mouse_data(mos5720, 0xE0); /* No Buttons Pressed */
      }
      return;
    }
#endif /* NCURSES_MOUSE_VERSION */
    if (ch != ERR) {
      if (ch == KEY_F(12)) { /* Special Ctrl+SL for breaking BASIC. */
        fe2010_keyboard_press(fe2010, 0x1D); /* Left Ctrl Make */
        console_scancode_fifo_write(0x46); /* Scroll Lock Make */
        console_scancode_fifo_write(0xC6); /* Scroll Lock Break */
        console_scancode_fifo_write(0x9D); /* Left Ctrl Break */
        return;
      }
      scancode = console_xt_keyboard_scancode(ch);
      if (console_character_is_shifted(ch)) {
        fe2010_keyboard_press(fe2010, 0x2A);          /* Left Shift Make */
        console_scancode_fifo_write(scancode);        /* Make */
        console_scancode_fifo_write(scancode + 0x80); /* Break */
        console_scancode_fifo_write(0xAA);            /* Left Shift Break */
      } else {
        fe2010_keyboard_press(fe2010, scancode);      /* Make */
        console_scancode_fifo_write(scancode + 0x80); /* Break */
      }
    }
  } else {
    fe2010_keyboard_press(fe2010, scancode);
  }
}



void console_execute_screen(mem_t *mem)
{
  uint8_t ch;
  uint8_t attrib;
  uint16_t pos;
  int columns;
  int bg;
  int fg;
  bool bold;
  bool blink;
  int i;

  /* Draw CGA screen buffer. */
  columns = (console_cga_mode & 1) ? 80 : 40;
  for (i = 0; i < (25 * columns); i++) {
    ch = mem_read(mem, 0xB8000 + (i * 2));
    attrib = mem_read(mem, 0xB8000 + (i * 2) + 1);
    fg    =  attrib       & 0x7;
    bold  = (attrib >> 3) & 1;
    bg    = (attrib >> 4) & 0x7;
    blink = (attrib >> 7) & 1;

    if (bold) {
      attron(A_BOLD);
    }
    if (blink) {
      attron(A_BLINK);
    }
    if (has_colors()) {
      attron(COLOR_PAIR((bg * 8) + fg + 1));
    }

    mvaddch(i / columns, i % columns, console_graphic(ch));

    if (has_colors()) {
      attroff(COLOR_PAIR((bg * 8) + fg + 1));
    }
    if (blink) {
      attroff(A_BLINK);
    }
    if (bold) {
      attroff(A_BOLD);
    }
  }

  /* Move cursor. */
  pos = console_crtc_register[0xF] + (console_crtc_register[0xE] * 0x100);
  move(pos / columns, pos % columns);

  /* Update screen. */
  refresh();
}



