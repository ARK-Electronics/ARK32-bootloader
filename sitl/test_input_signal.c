/*
  bootloader input signal tests

  Compiles the real bootloader/main.c for the host against the
  simulated MCU in sitl/, drives every kind of signal a flight
  controller or a configurator can put on the signal pin, and checks
  that the bootloader makes the right boot decision.

  The rule being tested:

    a flight controller driving the pin means the ESC has to run the
    main firmware, anything else means stay in the bootloader

  Each case runs in a forked child so the bootloader's static state
  starts clean, and is repeated over a sweep of waveform phases and
  polling loop speeds so a pass is not an accident of alignment.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <version.h>

#include "signals.h"
#include "sitl.h"

/*
  the ESC has to be running the main firmware within this long after
  power up or the motors never spin
 */
#define DEADLINE_NS (500 * 1000000ULL)

/* when a scenario has the flight controller start driving late */
#define LATE_START_NS (60 * 1000000ULL)

/*
  device info the bootloader answers a configurator with, for the
  F051 / PB4 target this is built for: '4','7','1', pin code, flash
  size code, 0x06, 0x06, protocol version, 0x30
 */
static const uint8_t expected_devinfo[9] = {'4',  '7',  '1',  0x14, 0x1F,
                                            0x06, 0x06, 0x02, 0x30};

typedef struct {
    const char *name;
    const sitl_waveform_t *wave;
    uint64_t start_ns;       /* when the source starts driving */
    uint64_t start_sweep_ns; /* spread start_ns over this much */
    bool pre_level;          /* level the line sits at before that */
    uint64_t stop_ns;        /* 0, or when the source stops driving */
    bool post_level;         /* level the line holds after stop_ns */
    sitl_outcome_t expect;
    bool expect_devinfo;       /* must also answer a configurator */
    bool needs_serial_client;  /* only meaningful with the framing check compiled in */

    /* the state the board powers up in */
    bool software_reset;
    bool set_eeprom_byte0;  /* otherwise the board is configured, 0x01 */
    uint8_t eeprom_byte0;
    uint8_t eeprom_version; /* 0 means the default of 18 */
    bool eeprom_spill;      /* place a settings byte past the first chunk */

    /* checks on what the bootloader did to the eeprom page */
    bool check_eeprom;
    uint8_t expect_version;      /* eeprom byte 2 after the run */
    uint32_t expect_flash_bytes; /* how much of the page it rewrote */

    const char *why;
} test_case_t;

static const test_case_t cases[] = {
    /* ---- static levels ---- */
    {
        .name = "input high",
        .wave = &sig_idle_high,
        .expect = SITL_TIMEOUT,
        .why = "a client holding the line high wants the bootloader",
    },
    {
        .name = "input low",
        .wave = &sig_idle_low,
        .expect = SITL_JUMPED,
        .why = "line held low is the normal boot to firmware case",
    },

    /* ---- DShot, normal polarity, idles low ---- */
    {.name = "DShot150",
     .wave = &sig_dshot150_4k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "DShot300",
     .wave = &sig_dshot300_4k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "DShot600",
     .wave = &sig_dshot600_8k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "DShot1200",
     .wave = &sig_dshot1200_8k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},

    /* ---- bidirectional DShot, inverted, idles high ---- */
    {.name = "bidir DShot150 1kHz",
     .wave = &sig_dshot150_bidir_1k,
     .expect = SITL_JUMPED,
     .why = "idles high, cannot be told from a client by level alone"},
    {.name = "bidir DShot300 2kHz",
     .wave = &sig_dshot300_bidir_2k,
     .expect = SITL_JUMPED,
     .why = "idles high, cannot be told from a client by level alone"},
    {.name = "bidir DShot300 4kHz",
     .wave = &sig_dshot300_bidir_4k,
     .expect = SITL_JUMPED,
     .why = "idles high, cannot be told from a client by level alone"},
    {.name = "bidir DShot600 4kHz",
     .wave = &sig_dshot600_bidir_4k,
     .expect = SITL_JUMPED,
     .why = "idles high, cannot be told from a client by level alone"},
    {.name = "bidir DShot600 8kHz",
     .wave = &sig_dshot600_bidir_8k,
     .expect = SITL_JUMPED,
     .why = "idles high, cannot be told from a client by level alone"},
    {.name = "bidir DShot1200 8kHz",
     .wave = &sig_dshot1200_bidir_8k,
     .expect = SITL_JUMPED,
     .why = "idles high, cannot be told from a client by level alone"},

    /* ---- the pulse protocols ---- */
    {.name = "Oneshot125 1kHz",
     .wave = &sig_oneshot125_1k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "Oneshot125 2kHz",
     .wave = &sig_oneshot125_2k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "Oneshot42 4kHz",
     .wave = &sig_oneshot42_4k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "Multishot 16kHz",
     .wave = &sig_multishot_16k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "Multishot 32kHz",
     .wave = &sig_multishot_32k,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "servo PWM 50Hz",
     .wave = &sig_pwm_50hz,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},
    {.name = "servo PWM 400Hz",
     .wave = &sig_pwm_400hz,
     .expect = SITL_JUMPED,
     .why = "flight controller is driving"},

    /* ---- flight controller that only starts driving after we boot ---- */
    {.name = "DShot600 starts late",
     .wave = &sig_dshot600_8k,
     .start_ns = LATE_START_NS,
     .start_sweep_ns = 20 * 1000000ULL,
     .pre_level = true,
     .expect = SITL_JUMPED,
     .why = "flight controller boots slower than the ESC"},
    {.name = "bidir DShot600 starts late",
     .wave = &sig_dshot600_bidir_8k,
     .start_ns = LATE_START_NS,
     .start_sweep_ns = 20 * 1000000ULL,
     .pre_level = true,
     .expect = SITL_JUMPED,
     .why = "the reported field failure: line idles high until the FC boots"},
    {.name = "Multishot starts late",
     .wave = &sig_multishot_32k,
     .start_ns = LATE_START_NS,
     .start_sweep_ns = 20 * 1000000ULL,
     .pre_level = true,
     .expect = SITL_JUMPED,
     .why = "flight controller boots slower than the ESC"},

    /* ---- bootloader UART, which must not look like a fast signal ---- */
    {
        .name = "UART bootloader session",
        .wave = &sig_uart_bl_once,
        .start_ns = LATE_START_NS,
        .start_sweep_ns = 5 * 1000000ULL,
        .pre_level = true,
        .expect = SITL_TIMEOUT,
        .expect_devinfo = true,
        .why = "configurator connects after boot, must be answered",
    },
    {
        .name = "UART client already talking",
        .wave = &sig_uart_bl_repeat,
        .expect = SITL_TIMEOUT,
        // without the framing check (4k G431) this is the pre-existing
        // phase-dependent behaviour, with no single right answer, so it
        // is only asserted on targets that compile the check in
        .needs_serial_client = true,
        .why = "a client sending at power up must not be read as a low pin",
    },

    /* ---- the eeprom must never be able to trap a bootable board ---- */
    {
        .name = "erased eeprom, app present",
        .wave = &sig_idle_low,
        .set_eeprom_byte0 = true,
        .eeprom_byte0 = 0xFF,
        .eeprom_version = 0xFF,
        .expect = SITL_JUMPED,
        .why = "a lost eeprom must not stop firmware that is there",
    },
    {
        .name = "erased eeprom, DShot600",
        .wave = &sig_dshot600_8k,
        .set_eeprom_byte0 = true,
        .eeprom_byte0 = 0xFF,
        .eeprom_version = 0xFF,
        .expect = SITL_JUMPED,
        .why = "a lost eeprom must not stop firmware that is there",
    },
    {
        .name = "unconfigured eeprom",
        .wave = &sig_idle_low,
        .set_eeprom_byte0 = true,
        .eeprom_byte0 = 0x00,
        .expect = SITL_TIMEOUT,
        .why = "a deliberately unprogrammed board still waits for a client",
    },

    /* ---- the version update must keep the page rewrite small ---- */
    {
        .name = "eeprom version update",
        .wave = &sig_idle_high,
        .software_reset = true,
        .eeprom_version = 18,
        .expect = SITL_TIMEOUT,
        .check_eeprom = true,
        .expect_version = BOOTLOADER_VERSION,
        .expect_flash_bytes = 256,
        .why = "one chunk rewritten, so the settings are exposed for less time",
    },
    {
        .name = "eeprom version already right",
        .wave = &sig_idle_high,
        .software_reset = true,
        .eeprom_version = BOOTLOADER_VERSION,
        .expect = SITL_TIMEOUT,
        .check_eeprom = true,
        .expect_version = BOOTLOADER_VERSION,
        .expect_flash_bytes = 0,
        .why = "nothing to do, so the page is never erased",
    },
    {
        .name = "eeprom update, power on reset",
        .wave = &sig_idle_high,
        .eeprom_version = 18,
        .expect = SITL_TIMEOUT,
        .check_eeprom = true,
        .expect_version = 18,
        .expect_flash_bytes = 0,
        .why = "only a software reset may touch the settings",
    },
    {
        .name = "eeprom settings past first chunk",
        .wave = &sig_idle_high,
        .software_reset = true,
        .eeprom_version = 18,
        .eeprom_spill = true,
        .expect = SITL_TIMEOUT,
        .check_eeprom = true,
        .expect_version = 18,
        .expect_flash_bytes = 0,
        .why = "settings past the first chunk block the update rather than risk them",
    },

    /*
      A short DShot burst at power up on a software reset, then the line
      goes idle high. Only the fast detector at the top of
      checkForSignal() can see the burst; by the time the level phases
      run the line is quiet, so they decide to stay. If that top
      detector jumps on a software reset it boots immediately and skips
      update_EEPROM(), so the version byte never records the fix. Guarded
      correctly, the burst is ignored, update_EEPROM() runs, and we wait.
      This is the case the finding-1 guard exists for.
    */
    {
        .name = "software reset, DShot burst",
        .wave = &sig_dshot600_bidir_8k,
        .software_reset = true,
        .pre_level = true,
        .stop_ns = 4 * 1000000ULL,
        .post_level = true,
        .eeprom_version = 18,
        .expect = SITL_TIMEOUT,
        .check_eeprom = true,
        .expect_version = BOOTLOADER_VERSION,
        .expect_flash_bytes = 256,
        .why = "the top fast detector must not boot past update_EEPROM() on a soft reset",
    },
};

#define NUM_CASES (sizeof(cases) / sizeof(cases[0]))

/*
  the same waveform sampled by a slightly faster or slower polling
  loop. The bootloader has no interrupts and no input capture, it just
  spins, so how fast it spins decides which edges it can see.
 */
static const sitl_cpu_speed_t speeds[] = {
    {"fast", 100, 100},
    {"nominal", 165, 165},
    {"slow", 330, 330},
};
#define NUM_SPEEDS (sizeof(speeds) / sizeof(speeds[0]))

/* phase offsets swept across one waveform period */
#define NUM_PHASES 7

typedef struct {
    int outcome;
    uint64_t jump_ns;
    uint32_t flash_bytes;
    uint8_t eeprom_byte0;
    uint8_t eeprom_version;
    uint8_t tx_len;
    uint8_t tx[16];
} run_result_t;

static bool verbose;

/*
  run one combination in a child process so the bootloader's static
  state, and any wedged state it leaves behind, cannot leak into the
  next one
 */
static bool run_child(const test_case_t *tc, const sitl_cpu_speed_t *speed,
                      uint64_t phase_ns, uint64_t start_ns, run_result_t *result)
{
    int fds[2];

    if (pipe(fds) != 0) {
        perror("pipe");
        exit(2);
    }

    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(2);
    }

    if (pid == 0) {
        close(fds[0]);

        const sitl_stimulus_t stim = {
            .wave = tc->wave,
            .start_ns = start_ns,
            .pre_level = tc->pre_level,
            .phase_ns = phase_ns,
            .stop_ns = tc->stop_ns,
            .post_level = tc->post_level,
        };
        sitl_set_stimulus(&stim);
        sitl_set_cpu_speed(speed);
        sitl_set_boot_state(tc->software_reset,
                            tc->set_eeprom_byte0 ? tc->eeprom_byte0 : 0x01,
                            tc->eeprom_version ? tc->eeprom_version : 18);
        if (tc->eeprom_spill) {
            sitl_eeprom_poke(300, 0xAB);
        }

        run_result_t r;
        memset(&r, 0, sizeof(r));
        r.outcome = (int)sitl_run(DEADLINE_NS);
        r.jump_ns = sitl_jump_time_ns();
        r.flash_bytes = sitl_flash_written_bytes();
        r.eeprom_byte0 = sitl_eeprom_byte(0);
        r.eeprom_version = sitl_eeprom_byte(2);

        uint8_t tx[64];
        const size_t n = sitl_decode_tx(tx, sizeof(tx));
        r.tx_len = (uint8_t)(n > sizeof(r.tx) ? sizeof(r.tx) : n);
        memcpy(r.tx, tx, r.tx_len);

        const ssize_t written = write(fds[1], &r, sizeof(r));
        close(fds[1]);
        _exit(written == (ssize_t)sizeof(r) ? 0 : 1);
    }

    close(fds[1]);

    size_t got = 0;
    while (got < sizeof(*result)) {
        const ssize_t n = read(fds[0], (uint8_t *)result + got, sizeof(*result) - got);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    return got == sizeof(*result);
}

static void describe(const test_case_t *tc, const sitl_cpu_speed_t *speed,
                     uint64_t phase_ns, const run_result_t *r, const char *note)
{
    printf("      %-24s %-8s phase %6" PRIu64 "us -> %-14s", tc->name, speed->name,
           phase_ns / 1000, sitl_outcome_name((sitl_outcome_t)r->outcome));
    if (r->outcome == SITL_JUMPED) {
        printf(" at %5" PRIu64 "ms", r->jump_ns / 1000000);
    } else {
        printf("          ");
    }
    if (r->tx_len > 0) {
        printf("  tx:");
        for (unsigned i = 0; i < r->tx_len; i++) {
            printf(" %02x", r->tx[i]);
        }
    }
    if (note != NULL) {
        printf("  %s", note);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else {
            fprintf(stderr, "usage: %s [-v]\n", argv[0]);
            return 2;
        }
    }

    /* map the simulated flash once so every child inherits it */
    sitl_flash_init();

    printf("bootloader input signal tests\n");
    printf("  deadline %" PRIu64 "ms, %zu speeds, up to %d phases per signal\n\n",
           (uint64_t)(DEADLINE_NS / 1000000), NUM_SPEEDS, NUM_PHASES);
    printf("  %-28s %-14s %-8s %s\n", "signal", "expected", "result", "detail");
    printf("  %-28s %-14s %-8s %s\n", "----------------------------",
           "--------------", "--------", "------");

    unsigned failed = 0;

    for (size_t c = 0; c < NUM_CASES; c++) {
        const test_case_t *tc = &cases[c];

#if !DETECT_SERIAL_CLIENT
        if (tc->needs_serial_client) {
            printf("  %-28s %-14s %-8s %s\n", tc->name,
                   tc->expect == SITL_JUMPED ? "booted app" : "in bootloader", "SKIP",
                   "framing check not compiled on this target");
            continue;
        }
#endif

        const uint64_t period = sitl_waveform_period_ns(tc->wave);
        const bool sweep = period > 0 || tc->start_sweep_ns > 0;
        const unsigned n_phases = sweep ? NUM_PHASES : 1;
        unsigned runs = 0;
        unsigned bad = 0;
        uint64_t worst_boot_ns = 0;
        char first_bad[160];

        first_bad[0] = 0;

        for (size_t s = 0; s < NUM_SPEEDS; s++) {
            for (unsigned p = 0; p < n_phases; p++) {
                const uint64_t phase_ns = period > 0 ? (period * p) / n_phases : 0;
                const uint64_t start_ns =
                    tc->start_ns + (tc->start_sweep_ns * p) / n_phases;
                run_result_t r;

                if (!run_child(tc, &speeds[s], phase_ns, start_ns, &r)) {
                    fprintf(stderr, "child failed for %s\n", tc->name);
                    return 2;
                }
                runs++;

                if (r.outcome == SITL_JUMPED && r.jump_ns > worst_boot_ns) {
                    worst_boot_ns = r.jump_ns;
                }

                const char *note = NULL;
                if (r.outcome != (int)tc->expect) {
                    note = "WRONG BOOT DECISION";
                } else if (tc->expect_devinfo &&
                           (r.tx_len != sizeof(expected_devinfo) ||
                            memcmp(r.tx, expected_devinfo, sizeof(expected_devinfo)) != 0)) {
                    note = "NO DEVICE INFO REPLY";
                } else if (tc->check_eeprom && r.flash_bytes != tc->expect_flash_bytes) {
                    note = "WRONG AMOUNT OF EEPROM REWRITTEN";
                } else if (tc->check_eeprom && (r.eeprom_version != tc->expect_version ||
                                                r.eeprom_byte0 != 0x01)) {
                    note = "EEPROM LEFT WRONG";
                }

                if (note != NULL) {
                    bad++;
                    if (first_bad[0] == 0) {
                        snprintf(first_bad, sizeof(first_bad), "%s (%s, phase %" PRIu64 "us): %s",
                                 note, speeds[s].name, phase_ns / 1000,
                                 sitl_outcome_name((sitl_outcome_t)r.outcome));
                    }
                }

                if (verbose || note != NULL) {
                    describe(tc, &speeds[s], phase_ns, &r, note);
                }
            }
        }

        const char *verdict;
        if (bad == 0) {
            verdict = "PASS";
        } else {
            verdict = "FAIL";
            failed++;
        }

        printf("  %-28s %-14s %-8s %2u/%-2u ok", tc->name,
               tc->expect == SITL_JUMPED ? "booted app" : "in bootloader", verdict,
               runs - bad, runs);
        if (tc->expect == SITL_JUMPED) {
            printf("  boot <=%4" PRIu64 "ms", worst_boot_ns / 1000000);
        } else {
            printf("              ");
        }
        if (bad > 0) {
            printf("  %s", first_bad);
        } else if (tc->why != NULL) {
            printf("  %s", tc->why);
        }
        printf("\n");
    }

    printf("\n");
    if (failed > 0) {
        printf("FAILED: %u of %zu signals give the wrong boot decision\n", failed, NUM_CASES);
        return 1;
    }
    printf("PASSED: all %zu signals give the right boot decision\n", NUM_CASES);
    return 0;
}
