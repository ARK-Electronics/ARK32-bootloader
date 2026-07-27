/*
  the simulated MCU: virtual clock, input pin, flash and the jump out
  of the bootloader
 */
#include "sitl.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "signals.h"

/* ---------------------------------------------------------------- */
/* virtual clock                                                     */
/* ---------------------------------------------------------------- */

/*
  Nothing here sleeps. Time advances only when the bootloader reads
  the pin or the timer, charged at the cost of one pass around the
  polling loop it is sitting in. On an F051 at 48MHz the tightest of
  those loops is about 16 cycles, so a read costs a few hundred
  nanoseconds.
 */
static const sitl_cpu_speed_t default_speed = {"nominal", 165, 165};
static sitl_cpu_speed_t cpu = {"nominal", 165, 165};

static uint64_t now_ns;
static uint64_t deadline_ns;
static uint64_t jump_ns;
static bool running;
static jmp_buf run_env;

/* time the external source is held off while we drive the line */
static uint64_t stim_hold_ns;

uint64_t sitl_now_ns(void)
{
    return now_ns;
}

void sitl_set_cpu_speed(const sitl_cpu_speed_t *speed)
{
    cpu = speed ? *speed : default_speed;
}

/* ---------------------------------------------------------------- */
/* input pin                                                        */
/* ---------------------------------------------------------------- */

static bool pin_is_output;
static bool pin_out_level = true;

/*
  every level change the bootloader drives onto the pin, so the test
  can decode what it transmitted
 */
#define MAX_TX_EDGES 4096
static struct {
    uint64_t t_ns;
    bool level;
} tx_edges[MAX_TX_EDGES];
static size_t n_tx_edges;

static void record_tx_edge(bool level)
{
    if (n_tx_edges > 0 && tx_edges[n_tx_edges - 1].level == level) {
        return;
    }
    if (n_tx_edges >= MAX_TX_EDGES) {
        return;
    }
    tx_edges[n_tx_edges].t_ns = now_ns;
    tx_edges[n_tx_edges].level = level;
    n_tx_edges++;
}

void sitl_advance_ns(uint64_t ns)
{
    now_ns += ns;

    /*
      a bootloader client shares the single wire with us, so it holds
      off while we are transmitting
     */
    if (pin_is_output && sitl_stimulus_is_half_duplex()) {
        stim_hold_ns += ns;
    }

    if (running && now_ns > deadline_ns) {
        longjmp(run_env, SITL_TIMEOUT);
    }
}

void sitl_pin_input(sitl_pull_t pull)
{
    (void)pull;
    if (pin_is_output) {
        pin_is_output = false;
        /* released, the line goes back to whatever the source drives */
        record_tx_edge(true);
    }
}

void sitl_pin_output(void)
{
    pin_is_output = true;
    record_tx_edge(pin_out_level);
}

void sitl_pin_write(bool level)
{
    pin_out_level = level;
    if (pin_is_output) {
        record_tx_edge(level);
    }
}

bool sitl_pin_read(void)
{
    sitl_advance_ns(cpu.gpio_read_ns);

    if (pin_is_output) {
        return pin_out_level;
    }

    return sitl_stimulus_level(now_ns - stim_hold_ns);
}

uint16_t sitl_timer_us(void)
{
    sitl_advance_ns(cpu.timer_read_ns);
    return (uint16_t)(now_ns / 1000);
}

/* ---------------------------------------------------------------- */
/* simulated flash                                                   */
/* ---------------------------------------------------------------- */

static uint8_t *flash;

void sitl_flash_init(void)
{
    if (flash != NULL) {
        return;
    }

    void *p = mmap((void *)SITL_FLASH_BASE, SITL_FLASH_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED || p != (void *)SITL_FLASH_BASE) {
        perror("mmap of simulated flash");
        exit(2);
    }

    flash = (uint8_t *)p;
    memset(flash, 0xFF, SITL_FLASH_SIZE);

    /*
      a valid looking application at the start of the app region.
      jump() checks that word 0 is a plausible stack pointer and word
      1 a plausible entry point, otherwise it refuses to jump.
     */
    uint32_t *app = (uint32_t *)(flash + 0x1000);
    app[0] = 0x20002000; /* stack top in ram */
    app[1] = 0x08001101; /* thumb entry point just after the vectors */

    /*
      a programmed eeprom. Byte 0 must be 0x01 or jump() bails out,
      byte 2 is the bootloader version.
     */
    uint8_t *eeprom = flash + 0x7C00;
    memset(eeprom, 0x00, 1024);
    eeprom[0] = 0x01;
    eeprom[1] = 0x02;
    eeprom[2] = 18;
}

void *sitl_flash_ptr(uint32_t address, uint32_t len)
{
    if (flash == NULL) {
        return NULL;
    }
    if (address < SITL_FLASH_BASE || address + len > SITL_FLASH_BASE + SITL_FLASH_SIZE) {
        return NULL;
    }
    return flash + (address - SITL_FLASH_BASE);
}

void read_flash_bin(uint8_t *data, uint32_t add, int out_buff_len)
{
    const void *src = sitl_flash_ptr(add, (uint32_t)out_buff_len);

    if (src == NULL) {
        memset(data, 0xFF, (size_t)out_buff_len);
        return;
    }
    memcpy(data, src, (size_t)out_buff_len);
}

bool save_flash_nolib(const uint8_t *data, uint32_t length, uint32_t add)
{
    void *dst = sitl_flash_ptr(add, length);

    if (dst == NULL) {
        return false;
    }
    memcpy(dst, data, length);
    return true;
}

/* ---------------------------------------------------------------- */
/* running the bootloader                                            */
/* ---------------------------------------------------------------- */

void sitl_jump(void)
{
    jump_ns = now_ns;
    longjmp(run_env, SITL_JUMPED);
}

uint64_t sitl_jump_time_ns(void)
{
    return jump_ns;
}

const char *sitl_outcome_name(sitl_outcome_t outcome)
{
    switch (outcome) {
    case SITL_JUMPED:
        return "booted app";
    case SITL_TIMEOUT:
        return "in bootloader";
    case SITL_RETURNED:
        return "main returned";
    }
    return "?";
}

sitl_outcome_t sitl_run(uint64_t deadline)
{
    volatile sitl_outcome_t outcome;
    int reason;

    sitl_flash_init();

    now_ns = 0;
    jump_ns = 0;
    stim_hold_ns = 0;
    n_tx_edges = 0;
    pin_is_output = false;
    pin_out_level = true;
    deadline_ns = deadline;

    reason = setjmp(run_env);
    if (reason == 0) {
        running = true;
        bootloader_main();
        outcome = SITL_RETURNED;
    } else {
        outcome = (sitl_outcome_t)reason;
    }
    running = false;

    return outcome;
}

/* ---------------------------------------------------------------- */
/* decoding what the bootloader transmitted                          */
/* ---------------------------------------------------------------- */

static bool tx_level_at(uint64_t t_ns)
{
    bool level = true;

    for (size_t i = 0; i < n_tx_edges; i++) {
        if (tx_edges[i].t_ns > t_ns) {
            break;
        }
        level = tx_edges[i].level;
    }

    return level;
}

size_t sitl_decode_tx(uint8_t *out, size_t max_out)
{
    const uint64_t bit_ns = 52083; /* 19200 baud */
    size_t n = 0;
    size_t edge = 0;

    while (edge < n_tx_edges && n < max_out) {
        /* find the next falling edge, which is a start bit */
        while (edge < n_tx_edges && tx_edges[edge].level) {
            edge++;
        }
        if (edge >= n_tx_edges) {
            break;
        }

        const uint64_t start_ns = tx_edges[edge].t_ns;
        uint8_t byte = 0;

        for (unsigned bit = 0; bit < 8; bit++) {
            const uint64_t sample = start_ns + bit_ns + bit_ns / 2 + bit * bit_ns;
            if (tx_level_at(sample)) {
                byte |= (uint8_t)(1u << bit);
            }
        }

        /* the stop bit has to be high or the framing is wrong */
        if (!tx_level_at(start_ns + bit_ns * 9 + bit_ns / 2)) {
            edge++;
            continue;
        }

        out[n++] = byte;

        /* skip past the edges belonging to this byte */
        const uint64_t end_ns = start_ns + bit_ns * 9;
        while (edge < n_tx_edges && tx_edges[edge].t_ns < end_ns) {
            edge++;
        }
    }

    return n;
}
