/*
  host simulation of the bootloader input pin

  The real bootloader/main.c is compiled for the host against the
  headers in sitl/Inc. Everything it does to the input pin and the
  microsecond timer lands here, so a test can drive a synthetic
  waveform onto the pin and see whether the bootloader decides to boot
  the main firmware.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the bootloader entry point, main() renamed by sitl/Inc/main.h */
int bootloader_main(void);

/* -------------------------------------------------------------- */
/* virtual clock                                                    */
/* -------------------------------------------------------------- */

/*
  the simulation has no real time in it. Time only moves when the
  bootloader reads the pin or the timer, which is what the polling
  loops on a real Cortex-M0 do. The cost of one read is the cost of
  one pass around the tightest polling loop.
 */
typedef struct {
    const char *name;
    uint32_t gpio_read_ns;  /* time charged for one gpio_read() */
    uint32_t timer_read_ns; /* time charged for one bl_timer_us() */
} sitl_cpu_speed_t;

uint64_t sitl_now_ns(void);
void sitl_advance_ns(uint64_t ns);
void sitl_set_cpu_speed(const sitl_cpu_speed_t *speed);

/* -------------------------------------------------------------- */
/* input pin                                                        */
/* -------------------------------------------------------------- */

typedef enum {
    SITL_PULL_NONE = 0,
    SITL_PULL_UP = 1,
    SITL_PULL_DOWN = 2,
} sitl_pull_t;

void sitl_pin_input(sitl_pull_t pull);
void sitl_pin_output(void);
void sitl_pin_write(bool level);
bool sitl_pin_read(void);

/* microsecond timer as the bootloader sees it, 16 bit and wrapping */
uint16_t sitl_timer_us(void);

/* -------------------------------------------------------------- */
/* simulated flash                                                  */
/* -------------------------------------------------------------- */

#define SITL_FLASH_BASE 0x08000000UL
#define SITL_FLASH_SIZE (32 * 1024)

/*
  map a block of memory at the MCU flash address so the address
  arithmetic in main.c (eeprom reads, app header checks) runs
  unmodified
 */
void sitl_flash_init(void);
void *sitl_flash_ptr(uint32_t address, uint32_t len);

/* -------------------------------------------------------------- */
/* running the bootloader                                           */
/* -------------------------------------------------------------- */

typedef enum {
    SITL_JUMPED = 1,   /* bootloader jumped to the main firmware */
    SITL_TIMEOUT = 2,  /* still in the bootloader at the deadline */
    SITL_RETURNED = 3, /* main() returned, should not happen */
} sitl_outcome_t;

const char *sitl_outcome_name(sitl_outcome_t outcome);

/* called from jump_to_application() in sitl/Inc/blutil.h */
void sitl_jump(void);

/*
  run the bootloader until it jumps or until the virtual clock passes
  deadline_ns
 */
sitl_outcome_t sitl_run(uint64_t deadline_ns);

/* virtual time at which the jump happened, only valid after SITL_JUMPED */
uint64_t sitl_jump_time_ns(void);

/* -------------------------------------------------------------- */
/* capture of what the bootloader transmitted                       */
/* -------------------------------------------------------------- */

/*
  decode the bytes the bootloader drove onto the pin as 19200 8N1.
  Returns the number of bytes decoded.
 */
size_t sitl_decode_tx(uint8_t *out, size_t max_out);
