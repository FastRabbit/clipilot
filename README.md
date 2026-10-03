# clipilot

A small command line interface for embedded targets such as STM32. No dynamic
allocation, no globals, and only two required hooks: a non-blocking character
source and a character sink.

* Commands, optional subcommands (nested to any depth) and zero or more
  arguments.
* **Typed arguments** — `int`, `uint`, `hex`, `float`, `string`, `bool` and
  `enum` are parsed and validated by the core, so a handler never sees bad
  input.
* **Line editing** — arrow keys, Home/End, Delete, and the usual
  `Ctrl-A/B/D/E/F/K/L/U/W` bindings.
* **History** — up/down through a fixed size ring.
* **Tab completion** — for commands, subcommands and `enum` argument values,
  with longest-common-prefix insertion and candidate listing.
* **Built-ins** — `help` (usage generated from the argument spec) and `history`.
* Everything lives in a caller-owned `cli_t`; several instances (UART, USB CDC,
  …) can run side by side.

## Quick start

```c
#include "cli.h"

static int led_on(cli_t *cli, const cli_args_t *args, void *user) {
    (void)args; (void)user;
    cli_write(cli, "LED on\n");
    return 0;
}

static int led_blink(cli_t *cli, const cli_args_t *args, void *user) {
    (void)user;
    cli_printf(cli, "blink every %u ms\n", (unsigned)cli_arg_uint(args, 0, 500));
    return 0;
}

static const cli_arg_spec_t blink_args[] = {
    { "period", CLI_ARG_UINT, false, false, NULL, "period in milliseconds" },
    { "count",  CLI_ARG_INT,  true,  false, NULL, "number of blinks" },
};

static const cli_cmd_t led_subs[] = {
    { "on",    "switch the LED on", NULL, 0, NULL, 0, led_on },
    { "blink", "blink the LED",     NULL, 0, blink_args, 2, led_blink },
};

static const cli_cmd_t commands[] = {
    { "led", "LED control", led_subs, 2, NULL, 0, NULL },
};

static cli_t cli;

void app_main(void) {
    cli_config_t cfg = cli_config_default();

    cfg.commands      = commands;
    cfg.command_count = sizeof(commands) / sizeof(commands[0]);
    cfg.getc_fn       = uart_getc;   /* returns <0 when no byte is available */
    cfg.putc_fn       = uart_putc;
    cfg.prompt        = "stm32> ";

    cli_init(&cli, &cfg);

    for (;;) {
        cli_poll(&cli);   /* non-blocking, call from the main loop */
    }
}
```

## Defining commands

`cli_cmd_t` describes both commands and subcommands:

| field | meaning |
|---|---|
| `name` | the command word, no spaces |
| `help` | one line description shown by `help` |
| `subcmds` / `subcmd_count` | optional child table, nestable |
| `args` / `arg_count` | optional argument specification |
| `handler` | called after arity and type validation succeed |

A command with subcommands but no handler *requires* a subcommand. If it also
has a handler, an unmatched second word is treated as the first argument of the
parent command instead.

## Argument specification

`cli_arg_spec_t` fields, in declaration order:
`name`, `type`, `optional`, `variadic`, `enum_values`, `help`.

| type | accepts | accessor |
|---|---|---|
| `CLI_ARG_INT` | `-42`, `+7`, `0x1F`, `0b1010`, `0o17`, `1_000` | `cli_arg_int()` |
| `CLI_ARG_UINT` | as above, unsigned | `cli_arg_uint()` |
| `CLI_ARG_HEX` | `48`, `0x48` | `cli_arg_uint()` |
| `CLI_ARG_FLOAT` | `3.5`, `-0.25`, `2.5e-2` | `cli_arg_float()` |
| `CLI_ARG_STR` | any token, quotes/escapes resolved | `cli_arg_str()` |
| `CLI_ARG_BOOL` | `on/off`, `true/false`, `yes/no`, `1/0`, `enable/disable` | `cli_arg_bool()` |
| `CLI_ARG_ENUM` | one of `enum_values` (case insensitive) | `cli_arg_enum()` → index |

* `optional` arguments must be trailing; use `cli_arg_present()` to test them.
* `variadic` on the **last** spec lets it soak up all remaining tokens (up to
  `CLI_MAX_ARGS`), which is how `echo one two three` works.
* Accessors return the supplied fallback when the index is out of range, so a
  handler never needs to bounds check.

The line is tokenized on whitespace with support for `"double quotes"`,
`'single quotes'` (no escape processing inside) and backslash escapes
(`\n`, `\t`, `\ `, `\"`, …).

## Key bindings

| key | action |
|---|---|
| `Enter` (`CR`, `LF` or `CRLF`) | execute the line |
| `Backspace` / `Delete` (`0x08`, `0x7F`) | delete left of the cursor |
| `←` `→` / `Ctrl-B` `Ctrl-F` | move the cursor |
| `Home` `End` / `Ctrl-A` `Ctrl-E` | jump to start / end |
| `Del` (`ESC [ 3 ~`) / `Ctrl-D` | delete under the cursor |
| `↑` `↓` / `Ctrl-P` `Ctrl-N` | browse history |
| `Ctrl-U` / `Ctrl-K` | kill to start / to end of line |
| `Ctrl-W` | kill the previous word |
| `Ctrl-L` | clear the screen |
| `Ctrl-C` | abandon the line |
| `Tab` | complete |

Completion inserts the remainder when a single candidate matches (plus a
trailing space), inserts the longest common prefix when several share one, and
otherwise lists the candidates and repaints the line.

## Configuration

All macros in `include/cli_config.h` can be overridden on the compiler command
line.

| macro | default | effect |
|---|---|---|
| `CLI_MAX_LINE_LEN` | 128 | longest editable line |
| `CLI_HISTORY_DEPTH` | 8 | history entries (0 disables history) |
| `CLI_MAX_TOKENS` | 16 | tokens per line |
| `CLI_MAX_ARGS` | 12 | arguments passed to a handler |
| `CLI_MAX_COMPLETIONS` | 32 | candidates considered per `Tab` |
| `CLI_MAX_PROMPT_LEN` | 16 | prompt characters |
| `CLI_POLL_BUDGET` | 64 | bytes consumed per `cli_poll()` (0 = drain) |
| `CLI_ENABLE_FLOAT` | 1 | `CLI_ARG_FLOAT` support |
| `CLI_ENABLE_PRINTF` | 1 | `cli_printf()` (pulls in `vsnprintf`) |
| `CLI_ENABLE_BUILTINS` | 1 | `help` and `history` commands |
| `CLI_DEFAULT_PROMPT` | `"> "` | prompt when none is configured |

RAM is dominated by `sizeof(cli_t)`, essentially
`(CLI_HISTORY_DEPTH + 3) × CLI_MAX_LINE_LEN` bytes for the line, history ring,
stash and tokenizer scratch, plus the token and argument arrays. Measured on a
64-bit host: ~1.7 kB with the defaults, and 648 bytes with
`CLI_MAX_LINE_LEN=64 CLI_HISTORY_DEPTH=4 CLI_MAX_TOKENS=8 CLI_MAX_ARGS=6
CLI_ENABLE_FLOAT=0`. A 32-bit target is somewhat smaller because of the
pointer arrays. Nothing is allocated at runtime.

`cli_config_t::echo` can be set to `false` for dumb terminals — the CLI then
reads lines silently and emits no escape sequences. `cli_config_t::crlf`
controls whether `'\n'` in output is expanded to `"\r\n"` (on by default).

## Porting to STM32 (HAL, UART)

```c
static int uart_getc(void *ctx) {
    uint8_t byte;

    (void)ctx;
    if (HAL_UART_Receive(&huart2, &byte, 1, 0) != HAL_OK) {
        return -1;                 /* nothing pending */
    }
    return (int)byte;
}

static void uart_putc(void *ctx, char c) {
    (void)ctx;
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)&c, 1, HAL_MAX_DELAY);
}
```

Polling the UART register directly, or draining a DMA/interrupt ring buffer, is
usually preferable:

```c
static int ring_getc(void *ctx) {
    ring_t *r = (ring_t *)ctx;

    return ring_empty(r) ? -1 : (int)ring_pop(r);
}
```

Feed bytes from an interrupt handler with `cli_feed()` / `cli_feed_buf()`
instead of `cli_poll()` if that suits the application better — but note that
handlers then run in interrupt context, so draining the ring from the main loop
is normally the better choice.

Add the sources to your build:

```cmake
target_sources(my_firmware PRIVATE
    clipilot/src/cli.c
    clipilot/src/cli_parse.c
    clipilot/src/cli_edit.c
    clipilot/src/cli_complete.c
    clipilot/src/cli_builtin.c
)
target_include_directories(my_firmware PRIVATE clipilot/include)
```

## Building and testing on the host

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`build/examples/host_demo/clipilot_demo` is an interactive demo with a small
command tree (`led`, `i2c`, `echo`, `set`, `quit`) that puts the terminal into
raw mode, so completion and line editing can be tried out directly.

[`examples/pico2_usb_cdc`](examples/pico2_usb_cdc) is a target example: a
console on a Raspberry Pi Pico 2 (RP2350, Cortex-M33) over USB CDC, driving two
LEDs. It is a standalone Pico SDK project and is not part of this build.

## API overview

| function | purpose |
|---|---|
| `cli_config_default()` | configuration pre-filled with the defaults |
| `cli_init()` | initialise an instance and print the first prompt |
| `cli_poll()` | pull and process pending input bytes |
| `cli_feed()` / `cli_feed_buf()` | push received bytes into the CLI |
| `cli_exec()` | run a line programmatically |
| `cli_set_prompt()` | change the prompt |
| `cli_write()`, `cli_putc()`, `cli_write_int()`, `cli_write_uint()` | output without `printf` |
| `cli_printf()`, `cli_vprintf()` | formatted output (optional) |
| `cli_print_help()`, `cli_print_usage()` | render help and usage text |
| `cli_arg_*()` | read converted arguments |

## Documentation

* [Management summary](MANAGEMENT-SUMMARY.md) — non-technical overview, status,
  resource cost and risks.

The [`doc/`](doc/README.md) folder documents the code itself — architecture and
invariants, the full API reference, the exact parsing grammar, the line editor
and its escape state machine, completion rules, configuration trade-offs,
porting and testing.

| Page | Contents |
|---|---|
| [Usage guide](doc/usage.md) | writing commands, arguments, subcommands and handlers |
| [Integration](doc/integration.md) | submodule, subtree, FetchContent or vendored copy; CMake, Makefile and STM32CubeIDE wiring |
| [Architecture](doc/architecture.md) | module map, keystroke data flow, invariants, re-entrancy rules |
| [API reference](doc/api-reference.md) | every public type and function |
| [Configuration](doc/configuration.md) | tuning macros, RAM/flash trade-offs |
| [Dispatch](doc/dispatch.md) | command tree resolution, arity rules, error messages |
| [Parsing](doc/parsing.md) | quoting, escapes, number literals, conversion |
| [Line editing](doc/line-editing.md) | key bindings, escape decoder, history ring |
| [Completion](doc/completion.md) | what is completed where, and why |
| [Extending](doc/extending.md) | adding a command, argument type or key |
| [Porting](doc/porting.md) | UART, DMA ring, RTOS and freestanding notes |
| [Testing](doc/testing.md) | harness, fixture patterns, coverage gaps |

## Repository conventions

Contributor and agent guidance lives alongside the code:

* [`.github/skills/clipilot-architecture`](.github/skills/clipilot-architecture/SKILL.md)
  — module map, library invariants, and recipes for adding a command or an
  argument type.
* [`.github/skills/clipilot-build-test`](.github/skills/clipilot-build-test/SKILL.md)
  — build and verification workflow, the feature-macro compile matrix, and the
  test fixture patterns.
* [`.github/instructions/c-style.instructions.md`](.github/instructions/c-style.instructions.md)
  — C coding conventions for `src/` and `include/`.
* [`.github/instructions/git-workflow.instructions.md`](.github/instructions/git-workflow.instructions.md)
  — branch naming and commit message rules enforced by the git hooks.
