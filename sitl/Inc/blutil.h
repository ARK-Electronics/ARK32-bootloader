/*
  SITL replacement for the per MCU blutil.h

  Same interface the real MCU headers give bootloader/main.c, wired to
  the simulated pin and clock in sitl_hw.c.
 */
#pragma once

#include "sitl.h"

/*
  model an F051, which is the ARK 4IN1 target: 32k flash, 8k ram
 */
#define RAM_BASE 0x20000000
#define RAM_SIZE (8 * 1024)
#define BOARD_FLASH_SIZE 32

#define GPIO_PIN(n) (1U << (n))

#define GPIO_PULL_NONE SITL_PULL_NONE
#define GPIO_PULL_UP SITL_PULL_UP
#define GPIO_PULL_DOWN SITL_PULL_DOWN

#define GPIO_OUTPUT_PUSH_PULL 0

static inline void gpio_mode_set_input(uint32_t pin, uint32_t pull_up_down)
{
    sitl_pin_input((sitl_pull_t)pull_up_down);
}

static inline void gpio_mode_set_output(uint32_t pin, uint32_t output_mode)
{
    sitl_pin_output();
}

static inline void gpio_set(uint32_t pin)
{
    sitl_pin_write(true);
}

static inline void gpio_clear(uint32_t pin)
{
    sitl_pin_write(false);
}

static inline bool gpio_read(uint32_t pin)
{
    return sitl_pin_read();
}

static inline void bl_timer_init(void)
{
}

static inline void bl_timer_disable(void)
{
}

static inline uint16_t bl_timer_us(void)
{
    return sitl_timer_us();
}

static inline void bl_clock_config(void)
{
}

static inline void bl_gpio_init(void)
{
    sitl_pin_input(SITL_PULL_UP);
}

/*
  a power on reset, which is what matters for the boot decision. On a
  software reset checkForSignal() deliberately stays in the bootloader.
 */
static inline bool bl_was_software_reset(void)
{
    return false;
}

static inline void jump_to_application(void)
{
    sitl_jump();
}
