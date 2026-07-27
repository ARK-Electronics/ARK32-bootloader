/*
  SITL replacement for the per MCU main.h

  This is what bootloader/main.c picks up when it is compiled for the
  host. It has to provide the same things the real MCU headers do
  without pulling in any vendor HAL.
 */
#ifndef __MAIN_H
#define __MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
  the bootloader's main() becomes bootloader_main() so the test
  harness can own the real main()
 */
#define main bootloader_main

/*
  Flash base as an unsigned long. main.c casts flash addresses
  straight to pointers, and on a 64 bit host an int sized constant
  would trip -Wint-to-pointer-cast under -Werror. The harness maps
  real memory here so those casts work.
 */
#define MCU_FLASH_START 0x08000000UL

/*
  port names only ever appear in the input_port macro in main.c, they
  are never dereferenced because the SITL gpio helpers work on the
  single simulated pin
 */
#define GPIOA 0
#define GPIOB 1
#define GPIOC 2

#endif /* __MAIN_H */
