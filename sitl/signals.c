#include "signals.h"

#include <string.h>

static sitl_stimulus_t stimulus;

void sitl_set_stimulus(const sitl_stimulus_t *stim)
{
    stimulus = *stim;
}

/*
  throttle values cycled through frame by frame so the bit pattern is
  not the same every frame. 0 is what a disarmed flight controller
  sends, the rest cover a spread of bit patterns.
 */
static const uint16_t throttle_values[] = {0, 48, 1046, 2047, 300, 1500, 999, 63};
#define NUM_THROTTLE_VALUES (sizeof(throttle_values) / sizeof(throttle_values[0]))

/*
  build a 16 bit DShot frame: 11 bit throttle, telemetry bit, 4 bit
  CRC. Bidirectional DShot inverts the CRC as well as the line.
 */
static uint16_t dshot_frame(uint64_t frame_index, bool inverted)
{
    const uint16_t throttle = throttle_values[frame_index % NUM_THROTTLE_VALUES];
    const uint16_t packet = (uint16_t)(throttle << 1); /* telemetry bit clear */
    uint16_t crc = (uint16_t)((packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0F);

    if (inverted) {
        crc = (uint16_t)(~crc & 0x0F);
    }

    return (uint16_t)((packet << 4) | crc);
}

static bool dshot_level(const sitl_waveform_t *w, uint64_t t_ns)
{
    const uint64_t frame_index = t_ns / w->period_ns;
    const uint64_t offset = t_ns % w->period_ns;
    const uint64_t frame_ns = (uint64_t)w->bit_ns * 16;
    bool level;

    if (offset >= frame_ns) {
        /* between frames the line idles */
        level = false;
    } else {
        const uint16_t frame = dshot_frame(frame_index, w->inverted);
        const unsigned bit_index = (unsigned)(offset / w->bit_ns);
        const uint64_t in_bit = offset % w->bit_ns;
        /* DShot is sent MSB first, a 1 is 3/4 duty and a 0 is 3/8 */
        const bool bit_set = (frame >> (15 - bit_index)) & 1;
        const uint64_t high_ns =
            bit_set ? (uint64_t)w->bit_ns * 3 / 4 : (uint64_t)w->bit_ns * 3 / 8;
        level = in_bit < high_ns;
    }

    /*
      bidirectional DShot is the same waveform inverted, so it idles
      high. That is what makes it indistinguishable from a bootloader
      client holding the line high if you only sample the level.
     */
    return w->inverted ? !level : level;
}

static bool pulse_level(const sitl_waveform_t *w, uint64_t t_ns)
{
    const uint64_t frame_index = t_ns / w->period_ns;
    const uint64_t offset = t_ns % w->period_ns;
    const uint16_t throttle = throttle_values[frame_index % NUM_THROTTLE_VALUES];
    const uint64_t span = w->pulse_max_ns - w->pulse_min_ns;
    const uint64_t width = w->pulse_min_ns + (span * throttle) / 2047;

    return offset < width;
}

static bool uart_level(const sitl_waveform_t *w, uint64_t t_ns)
{
    const uint64_t byte_ns = (uint64_t)w->bit_ns * 10;
    const uint64_t packet_ns = byte_ns * w->tx_len;
    uint64_t t = t_ns;

    if (w->tx_repeat_gap_ns > 0) {
        t %= packet_ns + w->tx_repeat_gap_ns;
    }

    if (t >= packet_ns) {
        return true; /* idle high, either the gap or after the last byte */
    }

    const size_t byte_index = (size_t)(t / byte_ns);
    const unsigned bit_index = (unsigned)((t % byte_ns) / w->bit_ns);
    const uint8_t byte = w->tx[byte_index];

    if (bit_index == 0) {
        return false; /* start bit */
    }
    if (bit_index == 9) {
        return true; /* stop bit */
    }

    return (byte >> (bit_index - 1)) & 1; /* LSB first */
}

bool sitl_stimulus_level(uint64_t t_ns)
{
    const sitl_waveform_t *w = stimulus.wave;

    if (w == NULL) {
        return true;
    }
    if (t_ns < stimulus.start_ns) {
        return stimulus.pre_level;
    }

    const uint64_t t = t_ns - stimulus.start_ns + stimulus.phase_ns;

    switch (w->kind) {
    case SIG_CONST:
        return w->level;
    case SIG_DSHOT:
        return dshot_level(w, t);
    case SIG_PULSE:
        return pulse_level(w, t);
    case SIG_UART:
        return uart_level(w, t);
    }

    return true;
}

bool sitl_stimulus_is_half_duplex(void)
{
    /*
      a bootloader client shares the one wire with us and stops
      talking while we answer. A flight controller driving DShot does
      not, but the bootloader never transmits in those tests.
     */
    return stimulus.wave != NULL && stimulus.wave->kind == SIG_UART;
}

uint64_t sitl_waveform_period_ns(const sitl_waveform_t *wave)
{
    if (wave == NULL) {
        return 0;
    }

    switch (wave->kind) {
    case SIG_CONST:
        return 0;
    case SIG_DSHOT:
    case SIG_PULSE:
        return wave->period_ns;
    case SIG_UART:
        /* a retrying client repeats, a one shot packet does not */
        if (wave->tx_repeat_gap_ns == 0) {
            return 0;
        }
        return (uint64_t)wave->bit_ns * 10 * wave->tx_len + wave->tx_repeat_gap_ns;
    }

    return 0;
}

/* ---------------------------------------------------------------- */
/* the catalogue                                                     */
/* ---------------------------------------------------------------- */

#define US 1000ULL
#define MS 1000000ULL

/* one bit time in ns for a DShot rate in kbit/s */
#define DSHOT_BIT_NS(kbit) ((uint32_t)(1000000000ULL / ((kbit)*1000ULL)))

const uint8_t sitl_bl_init_packet[17] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D,
    'B',  'L',  'H',  'e',  'l',  'i',  0xF4, 0x7D,
};

const sitl_waveform_t sig_idle_high = {
    .name = "idle high",
    .kind = SIG_CONST,
    .level = true,
};

const sitl_waveform_t sig_idle_low = {
    .name = "idle low",
    .kind = SIG_CONST,
    .level = false,
};

#define DSHOT(sym, label, kbit, inv, rate_hz)             \
    const sitl_waveform_t sym = {                         \
        .name = label,                                    \
        .kind = SIG_DSHOT,                                \
        .bit_ns = DSHOT_BIT_NS(kbit),                     \
        .inverted = (inv),                                \
        .period_ns = 1000000000ULL / (rate_hz),           \
    }

DSHOT(sig_dshot150_4k, "DShot150 @ 4kHz", 150, false, 4000);
DSHOT(sig_dshot300_4k, "DShot300 @ 4kHz", 300, false, 4000);
DSHOT(sig_dshot600_8k, "DShot600 @ 8kHz", 600, false, 8000);
DSHOT(sig_dshot1200_8k, "DShot1200 @ 8kHz", 1200, false, 8000);

DSHOT(sig_dshot150_bidir_1k, "bidir DShot150 @ 1kHz", 150, true, 1000);
DSHOT(sig_dshot300_bidir_2k, "bidir DShot300 @ 2kHz", 300, true, 2000);
DSHOT(sig_dshot300_bidir_4k, "bidir DShot300 @ 4kHz", 300, true, 4000);
DSHOT(sig_dshot600_bidir_4k, "bidir DShot600 @ 4kHz", 600, true, 4000);
DSHOT(sig_dshot600_bidir_8k, "bidir DShot600 @ 8kHz", 600, true, 8000);
DSHOT(sig_dshot1200_bidir_8k, "bidir DShot1200 @ 8kHz", 1200, true, 8000);

#define PULSE(sym, label, min_ns, max_ns, rate_hz)  \
    const sitl_waveform_t sym = {                   \
        .name = label,                              \
        .kind = SIG_PULSE,                          \
        .pulse_min_ns = (min_ns),                   \
        .pulse_max_ns = (max_ns),                   \
        .period_ns = 1000000000ULL / (rate_hz),     \
    }

PULSE(sig_oneshot125_1k, "Oneshot125 @ 1kHz", 125 * US, 250 * US, 1000);
PULSE(sig_oneshot125_2k, "Oneshot125 @ 2kHz", 125 * US, 250 * US, 2000);
PULSE(sig_oneshot42_4k, "Oneshot42 @ 4kHz", 42 * US, 84 * US, 4000);
PULSE(sig_multishot_16k, "Multishot @ 16kHz", 5 * US, 25 * US, 16000);
PULSE(sig_multishot_32k, "Multishot @ 32kHz", 5 * US, 25 * US, 32000);
PULSE(sig_pwm_50hz, "servo PWM @ 50Hz", 1000 * US, 2000 * US, 50);
PULSE(sig_pwm_400hz, "servo PWM @ 400Hz", 1000 * US, 2000 * US, 400);

/* 19200 baud, one bit is 52.083us */
#define UART_BIT_NS 52083

const sitl_waveform_t sig_uart_bl_once = {
    .name = "bootloader UART init",
    .kind = SIG_UART,
    .bit_ns = UART_BIT_NS,
    .tx = sitl_bl_init_packet,
    .tx_len = sizeof(sitl_bl_init_packet),
    .tx_repeat_gap_ns = 0,
};

const sitl_waveform_t sig_uart_bl_repeat = {
    .name = "bootloader UART init, retried",
    .kind = SIG_UART,
    .bit_ns = UART_BIT_NS,
    .tx = sitl_bl_init_packet,
    .tx_len = sizeof(sitl_bl_init_packet),
    .tx_repeat_gap_ns = 100 * MS,
};
