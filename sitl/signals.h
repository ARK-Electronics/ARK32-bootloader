/*
  synthetic input signals for the bootloader SITL tests

  Everything a flight controller or a bootloader client can put on the
  signal pin: a static level, any DShot variant (normal or
  bidirectional), the pulse protocols (Oneshot, Multishot, servo PWM)
  and 19200 baud bootloader UART traffic.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SIG_CONST, /* a fixed level */
    SIG_DSHOT, /* 16 bit DShot frame, optionally inverted */
    SIG_PULSE, /* active high pulse: Oneshot, Multishot, servo PWM */
    SIG_UART,  /* 19200 8N1, idle high, LSB first */
} sig_kind_t;

typedef struct {
    const char *name;
    sig_kind_t kind;

    bool level; /* SIG_CONST */

    uint32_t bit_ns;  /* SIG_DSHOT: one bit time */
    bool inverted;    /* SIG_DSHOT: bidirectional DShot idles high */

    uint32_t pulse_min_ns; /* SIG_PULSE: pulse width at zero throttle */
    uint32_t pulse_max_ns; /* SIG_PULSE: pulse width at full throttle */

    uint64_t period_ns; /* SIG_DSHOT / SIG_PULSE: frame repeat period */

    const uint8_t *tx;         /* SIG_UART: bytes to send */
    size_t tx_len;             /* SIG_UART: number of bytes */
    uint64_t tx_repeat_gap_ns; /* SIG_UART: 0 sends the packet once */
} sitl_waveform_t;

typedef struct {
    const sitl_waveform_t *wave;
    uint64_t start_ns; /* when the source starts driving the waveform */
    bool pre_level;    /* level the line sits at before start_ns */
    uint64_t phase_ns; /* phase offset into the waveform */
} sitl_stimulus_t;

void sitl_set_stimulus(const sitl_stimulus_t *stim);

/* level of the driven line at virtual time t_ns */
bool sitl_stimulus_level(uint64_t t_ns);

/* true if the source pauses while the bootloader drives the line */
bool sitl_stimulus_is_half_duplex(void);

/* repeat period of the waveform, used to spread the phase sweep */
uint64_t sitl_waveform_period_ns(const sitl_waveform_t *wave);

/*
  the packet an AM32 or BLHeli configurator sends to wake the
  bootloader up. main.c looks for byte 8 == 13, byte 9 == 'B' and
  byte 16 == 0x7d, and answers with the 9 byte device info.
 */
extern const uint8_t sitl_bl_init_packet[17];

/* ---- the waveform catalogue ---- */
extern const sitl_waveform_t sig_idle_high;
extern const sitl_waveform_t sig_idle_low;

extern const sitl_waveform_t sig_dshot150_4k;
extern const sitl_waveform_t sig_dshot300_4k;
extern const sitl_waveform_t sig_dshot600_8k;
extern const sitl_waveform_t sig_dshot1200_8k;

extern const sitl_waveform_t sig_dshot150_bidir_1k;
extern const sitl_waveform_t sig_dshot300_bidir_2k;
extern const sitl_waveform_t sig_dshot300_bidir_4k;
extern const sitl_waveform_t sig_dshot600_bidir_4k;
extern const sitl_waveform_t sig_dshot600_bidir_8k;
extern const sitl_waveform_t sig_dshot1200_bidir_8k;

extern const sitl_waveform_t sig_oneshot125_1k;
extern const sitl_waveform_t sig_oneshot125_2k;
extern const sitl_waveform_t sig_oneshot42_4k;
extern const sitl_waveform_t sig_multishot_16k;
extern const sitl_waveform_t sig_multishot_32k;

extern const sitl_waveform_t sig_pwm_50hz;
extern const sitl_waveform_t sig_pwm_400hz;

extern const sitl_waveform_t sig_uart_bl_once;
extern const sitl_waveform_t sig_uart_bl_repeat;
