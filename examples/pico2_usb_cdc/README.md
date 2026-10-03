# clipilot on a Raspberry Pi Pico 2 (USB CDC console)

A clipilot console exposed over the Pico 2's **USB CDC** port, driving two
LEDs. No UART wiring is needed — the same cable that flashes the board carries
the console.

| Item | Value |
| --- | --- |
| Board | Raspberry Pi Pico 2 (RP2350) |
| Core | Cortex-M33 |
| Console | USB CDC (`stdio_usb`) |
| LED1 | GPIO15 |
| LED2 | GPIO14 |

> **Verified, but not hardware-tested.** The example compiles clean for
> `cortex-m33` under `-Wall -Wextra -Werror -Wconversion`, and its command
> table, handlers and main loop were exercised on the host against a stub SDK —
> every transcript below is real output. No Pico 2 and no Pico SDK were
> available on the machine where it was written, so the SDK calls themselves
> follow the documented Pico SDK API but have not been run on silicon. Treat
> the first flash as the hardware bring-up.

## Wiring

Each LED needs a series resistor. With the anode toward the GPIO:

```
GPIO15 ---[ 330R ]---|>|--- GND      LED1
GPIO14 ---[ 330R ]---|>|--- GND      LED2
```

330 Ω suits a typical red LED at 3.3 V (~5 mA). Use 220 Ω for a brighter LED
or 1 kΩ for a dimmer one. Both LEDs are active-high: `gpio_put(pin, true)`
lights them.

The Pico 2's own on-board LED is *not* used, so the example behaves the same on
a bare RP2350 board.

## Building

You need the [Pico SDK](https://github.com/raspberrypi/pico-sdk) (2.0.0 or
later — earlier versions have no RP2350 support) and the Arm GNU toolchain.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk

cd examples/pico2_usb_cdc
cmake -B build -G Ninja
cmake --build build
```

This is a **standalone** project: it is deliberately not part of the top-level
clipilot build, which targets the host. It pulls clipilot in with
`add_subdirectory(../..)` and turns the host tests and demo off.

`CMakeLists.txt` looks for `pico_sdk_import.cmake` next to itself, then falls
back to `$PICO_SDK_PATH/external/pico_sdk_import.cmake`. Copy the file locally
if you prefer to pin it.

The build produces `build/clipilot_pico2.uf2`.

## Using this example in your own repository

The only thing tying this example to the clipilot tree is the line
`add_subdirectory(../.. clipilot_build)`, which reaches *outside* the project
directory. Once the example lives in its own repository, clipilot has to come
in from somewhere else — as a submodule, for instance.

Add it:

```sh
git submodule add https://<host>/clipilot.git external/clipilot
git commit -m "build: add clipilot as a submodule"
```

Then the only change in `CMakeLists.txt` is which directory is added:

```diff
-# clipilot lives two directories up. Its tests and host demo are host
-# programs and must not be built for the target.
+# clipilot comes from the submodule. Its tests and host demo are host
+# programs and must not be built for the target.
 set(CLI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
 set(CLI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
-add_subdirectory(../.. clipilot_build)
+add_subdirectory(external/clipilot)
```

The second argument disappears with it. `clipilot_build` was only needed
because `../..` sits outside the source tree, and CMake then cannot derive a
binary directory on its own; `external/clipilot` is inside the tree, so the
default is fine.

**Nothing else changes.** In particular the tuning block stays exactly as it
is, still targeting `clipilot` and still `PUBLIC`:

```cmake
target_compile_definitions(clipilot PUBLIC
    CLI_MAX_LINE_LEN=64
    ...
)
```

The target name is `clipilot` however the sources arrived, so moving to a
submodule does not rename it. Keeping `PUBLIC` still matters for the same
reason as before — see [Tuning](#tuning) below.

### Worth adding: a guard for the un-cloned submodule

A submodule is empty until it is initialised, and the failure is otherwise a
confusing "no CMakeLists.txt" from deep inside the build. Three lines turn it
into an instruction:

```cmake
if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/external/clipilot/CMakeLists.txt)
    message(FATAL_ERROR
        "external/clipilot is empty. Run:\n"
        "  git submodule update --init --recursive")
endif()
```

Put it just above the `add_subdirectory()`. Tell whoever clones the repository:

```sh
git clone --recurse-submodules <your-repo>
# or, in an existing clone:
git submodule update --init --recursive
```

CI needs the same step; most providers have a "submodules: recursive" option.

### Verified

The submodule arrangement above was built for real — clipilot added as an
actual submodule of a separate repository, configured and compiled. The
consumer reported `sizeof(cli_t) = 648`, matching the library, and the `-DCLI_*`
flags appeared on all of clipilot's own objects. Omitting the submodule and
re-configuring produced the intended error rather than a confusing one.

Only the Pico SDK portion could not be exercised here, and it is unaffected by
where clipilot comes from.

### If you would rather not use a submodule

`FetchContent` needs no repository plumbing and no clone-time ceremony, at the
price of network access when CMake configures:

```cmake
include(FetchContent)
FetchContent_Declare(clipilot
    GIT_REPOSITORY https://<host>/clipilot.git
    GIT_TAG        <commit-or-tag>)    # pin it; never a branch name
set(CLI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CLI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(clipilot)
```

This replaces the `add_subdirectory()` line and, again, changes nothing else.
[doc/integration.md](../../doc/integration.md) compares all four approaches,
including `git subtree` and a vendored copy, and explains why
`find_package(clipilot)` is not offered.

## Flashing

Hold **BOOTSEL** while plugging the board in, then copy the UF2 to the
`RP2350` mass-storage volume that appears:

```sh
cp build/clipilot_pico2.uf2 /media/$USER/RP2350/
```

The board reboots and enumerates as a USB serial device. You can skip the
button after the first flash — `sys reboot bootsel` puts the running firmware
back into BOOTSEL mode.

## Connecting

Open the CDC port with any terminal at any baud rate (the rate is ignored for
USB CDC):

```sh
picocom /dev/ttyACM0          # Linux
screen /dev/tty.usbmodem*     # macOS
```

On Windows use PuTTY or Tera Term on the assigned `COM` port. PuTTY must be in
**Serial** mode with local echo *off* — clipilot echoes every character itself.

Pressing Enter on a fresh connection is enough to get a prompt.

## Sample session

Captured by running this example's real command table:

```
clipilot on Pico 2 - type 'help'
pico2> help
commands:
  led      LED control
  gpio     raw GPIO access
  sys      system commands
  help     list commands or describe one
  history  show recently entered lines

type 'help <command>' for details
pico2> led on led1
led1 on
pico2> led blink led2 200
led2 blinking every 200 ms
pico2> led status
led1  GPIO15  on
led2  GPIO14  on   blinking 200 ms
pico2> gpio set 16 on
GPIO16 = 1
pico2> sys info
board   : Raspberry Pi Pico 2 (RP2350, Cortex-M33)
console : USB CDC
uptime  : 12483 ms
cli_t   : 648 bytes
pico2> sys reboot bootsel
rebooting into BOOTSEL
```

`help <command>` describes a subtree, and the argument types come straight from
the table:

```
pico2> help led
LED control
usage: led <subcommand>
subcommands:
  on      switch on
  off     switch off
  toggle  invert
  blink   blink in the background
  status  show LED state

pico2> help led blink
blink in the background
usage: led blink [led:led1|led2|all] [period:unsigned integer] [count:integer]
  [led:led1|led2|all]  which LED (default all)
  [period:unsigned integer]  half period in ms (default 250)
  [count:integer]  blinks, omit for forever
```

Tab completion works on commands and subcommands, the arrow keys walk the
history and move within the line, and `Ctrl-A`/`Ctrl-E`/`Ctrl-U`/`Ctrl-K`
behave as in a POSIX shell. See [doc/line-editing.md](../../doc/line-editing.md)
for the full key map.

Bad input is rejected before a handler ever runs:

```
pico2> led on led3
error: argument 1 <led>: 'led3' is not a valid choice (expected one of: led1 led2 all)
```

## Commands

Every LED argument is optional and defaults to `all`.

| Command | Description |
| --- | --- |
| `led on [led1\|led2\|all]` | Switch on, cancelling any blink |
| `led off [led1\|led2\|all]` | Switch off |
| `led toggle [led1\|led2\|all]` | Invert |
| `led blink [led] [period] [count]` | Blink; half period defaults to 250 ms, omit `count` for forever |
| `led status` | Pin and state of both LEDs |
| `gpio get <pin>` | Read a pin |
| `gpio set <pin> <level>` | Drive a pin as an output; level is `on`/`off`, `1`/`0`, `yes`/`no` |
| `sys info` | Board, console, uptime and `sizeof(cli_t)` |
| `sys reboot [bootsel]` | Reboot, optionally into the bootloader |


## What this example demonstrates

Four things that matter when porting clipilot to any USB-CDC target:

**A non-blocking character source.** `getchar_timeout_us(0)` returns
`PICO_ERROR_TIMEOUT` (a negative value) when no byte is waiting, which is
exactly the contract `cli_getc_fn` expects — return `< 0` for "nothing yet".

**`putchar_raw()`, not `putchar()`.** The SDK's `putchar()` performs its own
`\n` → `\r\n` translation. Combined with clipilot's `cfg.crlf`, that emits two
carriage returns and double-spaces the output. `putchar_raw()` writes the byte
untouched and leaves newline handling to clipilot.

**Handlers must not block.** `led blink` stores a period and returns
immediately; the actual toggling happens in `led_tick()`, called from the main
loop. A handler that slept would freeze the console, because `cli_poll()` runs
on the same thread.

**No globals in the command handlers.** The board state is passed through
`cfg.user`, which arrives as each handler's third parameter
(`board_t *b = (board_t *)user;`), so the command table stays `static const`
and the example would work unchanged with two CLI instances.

The USB-connect edge is also handled: output written before the host opens the
port is discarded, so the banner is re-sent and the prompt redrawn every time a
terminal attaches.

## Footprint

Measured with `arm-none-eabi-gcc` 14.2.1,
`-mcpu=cortex-m33 -mthumb -Os -ffunction-sections`, using the tuning in this
example's `CMakeLists.txt`:

| Item | Size |
| --- | --- |
| clipilot `.text` | 7 164 B |
| `cli_t` instance (RAM) | 648 B |

Both figures exclude the example's own code and the Pico SDK. The RAM figure is
the entire CLI state — line buffer, history and parse tables — and is
statically allocated inside `main.c`. clipilot never calls `malloc`.

## Tuning

The CLI is trimmed in `CMakeLists.txt`:

```cmake
target_compile_definitions(clipilot PUBLIC
    CLI_MAX_LINE_LEN=64
    CLI_HISTORY_DEPTH=4
    CLI_MAX_TOKENS=8
    CLI_MAX_ARGS=6
    CLI_ENABLE_FLOAT=0
)
```

Note the target: the defines go on **`clipilot`**, with **`PUBLIC`** — not on
`clipilot_pico2`. Putting them on the executable would compile your code
against a 648-byte `cli_t` while the library still used the 1 716-byte default
layout, and `cli_init()`'s `memset` would silently run off the end of the
instance. [doc/integration.md](../../doc/integration.md) covers this trap in
detail, and [doc/configuration.md](../../doc/configuration.md) lists every
option.
