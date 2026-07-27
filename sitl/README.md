# Bootloader input signal tests

A host build of the bootloader that answers one question for every kind of
signal that can appear on the ESC signal pin:

> does the bootloader boot the main firmware, or does it stay in the
> bootloader?

Getting that wrong in the "stay" direction is what leaves an ESC silent on a
quad with no motors spinning, so it is worth testing off hardware.

```
make sitl_test          # build and run
make sitl_test SITL_TEST_ARGS=-v   # print every combination
```

The tests take about a second and need nothing but a host C compiler. They run
in CI from `.github/workflows/CI_sitl.yml`.

## How it works

`bootloader/main.c` is compiled **unmodified** for the host. The per MCU
headers it includes are replaced by the ones in `sitl/Inc`:

| header           | provides                                                  |
| ---------------- | --------------------------------------------------------- |
| `Inc/main.h`     | `MCU_FLASH_START`, and renames `main()` to `bootloader_main()` |
| `Inc/blutil.h`   | `gpio_*`, `bl_timer_*`, `jump_to_application()`             |
| `Inc/eeprom.h`   | `read_flash_bin()`, `save_flash_nolib()`                    |

The simulated target is an F051 on PB4 with 32k of flash, which is the
ARK 4IN1. The boot decision code is shared by every target, so the coverage
carries, with one exception: the G431 builds that have to fit a 4k bootloader
region set `DETECT_SERIAL_CLIENT=0` because the check does not fit, and the
"UART client already talking" case does not apply to them. The G431 DroneCAN
build links against 16k and keeps it.

`sitl_hw.c` mmaps real memory at `0x08000000`, so the eeprom read and the
application header check in `jump()` run against a plausible flashed board
rather than a stub. `jump_to_application()` longjmps back out to the test,
which is how a "booted the app" result is detected.

### Time

Nothing sleeps. The virtual clock only moves when the bootloader reads the pin
or the timer, charged at the cost of one pass around the polling loop it is
sitting in. That is the right model for this code: it has no interrupts and no
input capture, it decides everything by spinning on `gpio_read()`, so the rate
it can spin at is exactly what decides which edges it can see.

Every case is repeated at three loop speeds (100, 165 and 330 ns per read; an
F051 at 48 MHz is around 165) and at seven phase offsets across the waveform,
so a pass cannot be an accident of alignment. Each combination runs in a forked
child, so the bootloader's static state starts clean and a wedged run cannot
affect the next one.

### Signals

`signals.c` generates the line level as a function of time:

- static high and static low
- DShot 150/300/600/1200, normal and bidirectional, 1 to 8 kHz. Real 16 bit
  frames with a throttle value, telemetry bit and CRC, cycling through a
  spread of throttle values so the bit pattern changes frame to frame.
  Bidirectional DShot is the inverted waveform, so it idles **high**.
- Oneshot125, Oneshot42, Multishot and servo PWM as active high pulses
- 19200 8N1 bootloader traffic, the 17 byte init packet a configurator sends

A scenario can also start the source late, which covers the common case of a
flight controller that boots more slowly than the ESC.

For the UART cases the harness also decodes what the bootloader drove back
onto the wire, and the bootloader session test requires the correct 9 byte
device info reply. That checks the boot decision code has not broken the
protocol it exists to serve.

### Boot state

A scenario also sets what the board comes up as: whether this was a software
reset, and what the eeprom holds. That covers the paths that decide whether
there is anything worth booting:

- an **erased eeprom** with a valid application still has to boot, otherwise a
  power cut during a settings write leaves a board that only a configurator
  can rescue
- an eeprom that says **not configured** still waits for a client
- the **version update** only runs on a software reset, and rewrites one 256
  byte chunk rather than the whole page, which is checked by counting the
  bytes the bootloader programs

## What the tests assert

A flight controller driving the pin means the ESC has to run the main
firmware, within 500 ms of power up. Anything else means stay in the
bootloader, answering a client if there is one.

## Proving the tests catch the bug

The `regression_proof` job in CI builds the same harness against
`bootloader/main.c` from a pinned pre-fix commit and requires the suite to
**fail**. To run it locally:

```
git show 8a53777:bootloader/main.c > /tmp/main_pre_fix.c
make sitl SITL_BOOTLOADER=/tmp/main_pre_fix.c SITL_OBJ=obj/sitl-pre-fix
obj/sitl-pre-fix/test_input_signal
```
