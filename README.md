# pc20iii
Commodore PC 20-III Emulator

This emulator is specifically made to emulate one of the XT-compatible PCs from Commodore in a Linux terminal.

Features and notes:
* This emulator is NOT cycle accurate! Hacks implemented to make things run.
* Intel 8088 CPU almost fully emulated except LOCK and WAIT instructions.
* The ESC instruction, usually used for 8087 FPU, does nothing.
* Configured for 640K RAM, 2 floppy drives and CGA 80 column mode.
* CGA screen buffer at 0xB8000 drawn through curses, with color.
* ACS (Alternative Character Set) used for "graphical" CP437 characters.
* XT keyboard scan codes converted from curses counterparts.
* F11 mapped to "Left Alt" commonly used to to bring down menus in programs.
* F12 mapped to "Left Ctrl" + "Scroll Lock" for doing a break in BASIC.
* MOS 5720 mouse emulation, but only left/right mouse buttons and no movement.
* Faraday FE2010 chipset emulated as needed.
* OKI MSM6242 RTC emulated and routed to host system clock.
* Standard Microsystems FDC 9268 floppy controller mostly emulated.
* Basic read and write to floppies up to 2.88M.
* Autodetect of sectors-per-track from floppy image boot sector.
* Western Digital 93024-X 20 MB hard drive emulation.
* Hard disk image expects layout matching C/H/S values of 615/4/17.
* Ctrl+C in the terminal breaks into a debugger for dumping data.
* CPU trace enabled/disabled by compile time define flag.
* Host CPU can be relaxed by intercepting int16h and waiting for stdin.
* By default expects BIOS ROM: cbm-pc10sd-bios-v4.38-318085-05-C72A.bin
* Booting from floppy disk image or hard disk image should work.
* Passthrough of RS-232 on COM1 to real serial TTY on host.
* NE2000 compatible Ethernet card (DP8390) emulated at port 0x300 and IRQ 3.
* Network emulated with internal stack supporting TCP and UDP connections.
* IP addresses hardcoded to 10.0.0.1 for host/gateway and 10.0.0.2 for client.

Information on my blog:
* [Commodore PC 20-III Emulator](https://kobolt.github.io/article-232.html)
* [Commodore PC 20-III Emulator Update](https://kobolt.github.io/article-234.html)

YouTube:
* [GW-BASIC](https://www.youtube.com/watch?v=PFVnMGIvJB8)
* [Rogue](https://www.youtube.com/watch?v=a-5ppUwTYrw)
* [RS-232 passthrough](https://www.youtube.com/watch?v=IQRhXtzYWrA)

