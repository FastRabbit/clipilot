# Using clipilot

A walkthrough from an empty project to a working console. For the formal
description of each function see the [API reference](api-reference.md); for
getting the sources into your build see [integration](integration.md).

## The mental model

You describe your commands as a **static table**. The library reads bytes from
your device, edits the line, splits it, converts each argument to the declared
type, and calls your function with the values already validated. You never
write parsing code.

```
        your table                    the library                 your handler
  { "led", "blink", <period> }  →  edit, split, convert  →  led_blink(cli, args)
```

## The four pieces

Every integration has exactly these:

1. **Two I/O functions** — read a byte, write a byte.
2. **A command table** — what the console can do.
3. **An instance** — one `cli_t` variable.
4. **A call in your main loop** — `cli_poll()`.

## A first command

```c
#include "cli.h"

static int ping(cli_t *cli, const cli_args_t *args, void *user) {
    (void)args;
    (void)user;
    cli_write(cli, "pong\n");
    return 0;
}

static const cli_cmd_t commands[] = {
    { "ping", "reply with pong", NULL, 0, NULL, 0, ping },
};
```

The `cli_cmd_t` fields in order are `name`, `help`, `subcmds`, `subcmd_count`,
`args`, `arg_count`, `handler`. This command has no subcommands and no
arguments, hence the `NULL, 0, NULL, 0`.

Now the instance and the loop:

```c
static cli_t cli;

void console_init(void) {
    cli_config_t cfg = cli_config_default();

    cfg.commands      = commands;
    cfg.command_count = sizeof(commands) / sizeof(commands[0]);
    cfg.getc_fn       = uart_getc;
    cfg.putc_fn       = uart_putc;
    cfg.prompt        = "stm32> ";

    cli_init(&cli, &cfg);
}

void console_poll(void) {  /* call from the main loop */
    cli_poll(&cli);
}
```

Always start from `cli_config_default()` rather than a zeroed struct — it sets
`echo` and `crlf`, and keeps working if fields are added later.

The two I/O functions are the only target-specific code; worked examples for
polled UART, DMA rings and RTOS use are in [porting](porting.md).

Typing `ping` now prints `pong`. `help` already works, and so does TAB
completion — both read the same table.

## Adding arguments

Declare what you expect and the library enforces it:

```c
static int led_blink(cli_t *cli, const cli_args_t *args, void *user) {
    uint32_t period = cli_arg_uint(args, 0, 500);   /* required */
    int32_t  count  = cli_arg_int (args, 1, 10);    /* optional → default 10 */

    (void)user;
    cli_printf(cli, "blinking %ld times every %lu ms\n",
               (long)count, (unsigned long)period);
    return 0;
}

static const cli_arg_spec_t blink_args[] = {
    { "period", CLI_ARG_UINT, false, false, NULL, "milliseconds" },
    { "count",  CLI_ARG_INT,  true,  false, NULL, "number of blinks" },
};
```

Spec fields in order: `name`, `type`, `optional`, `variadic`, `enum_values`,
`help`.

The handler has no validation code because it needs none. The library has
already rejected a missing `period`, a non-numeric value and a third argument,
printing a usage line each time:

```
stm32> led blink
error: missing argument
usage: led blink <period:unsigned integer> [count:integer]
  <period:unsigned integer>  milliseconds
  [count:integer]  number of blinks
```

The fallback passed to each accessor is what you get when an optional argument
was omitted, so the common case is a single line with no `if`.

### Argument types

| Type | Accepts | Read with |
|---|---|---|
| `CLI_ARG_INT` | `-42`, `0x1F`, `0b1010`, `1_000` | `cli_arg_int()` |
| `CLI_ARG_UINT` | as above, unsigned | `cli_arg_uint()` |
| `CLI_ARG_HEX` | `48`, `0x48` | `cli_arg_uint()` |
| `CLI_ARG_FLOAT` | `3.5`, `-0.25`, `2.5e-2` | `cli_arg_float()` |
| `CLI_ARG_STR` | any word; quotes and escapes resolved | `cli_arg_str()` |
| `CLI_ARG_BOOL` | `on/off`, `true/false`, `yes/no`, `1/0` | `cli_arg_bool()` |
| `CLI_ARG_ENUM` | one of your listed words | `cli_arg_enum()` → index |

Rules: optional arguments must come **last**, and only the final spec may be
`variadic` (which lets it absorb every remaining word, as `echo` does).

The exact number grammar is in [parsing](parsing.md).

### Enums are worth using

An enum argument gets you validation, a helpful error listing the valid words,
TAB completion of the values, and an index you can `switch` on:

```c
static const char *const modes[] = { "idle", "run", "turbo", NULL };

static const cli_arg_spec_t set_args[] = {
    { "mode", CLI_ARG_ENUM, false, false, modes, "operating mode" },
};

/* in the handler */
switch (cli_arg_enum(args, 0, -1)) {
case 0: enter_idle();  break;
case 1: enter_run();   break;
case 2: enter_turbo(); break;
}
```

A `CLI_ARG_STR` compared with `strcmp` gives you none of that.

## Grouping with subcommands

Nest a table to group related commands:

```c
static const cli_cmd_t led_subs[] = {
    { "on",    "switch on",  NULL, 0, NULL,       0, led_on    },
    { "off",   "switch off", NULL, 0, NULL,       0, led_off   },
    { "blink", "blink it",   NULL, 0, blink_args, 2, led_blink },
};

static const cli_cmd_t commands[] = {
    { "led", "LED control", led_subs, 3, NULL, 0, NULL },
};
```

`led` has subcommands and **no handler**, so it requires one:

```
stm32> led
error: 'led' requires a subcommand
subcommands: on off blink
```

Give a node both a handler and subcommands and the rule changes: a word that
does not match a subcommand becomes its first argument instead. That is
occasionally useful and occasionally surprising —
[dispatch](dispatch.md#the-handlersubcommand-overlap-rule) has the full table.

Nesting has no depth limit.

## Writing output

| Function | Use for |
|---|---|
| `cli_write(cli, "text\n")` | fixed strings — always available |
| `cli_write_int()`, `cli_write_uint()` | numbers without `printf` |
| `cli_printf(cli, "...", ...)` | formatted output, when enabled |

Write `"\n"`, never `"\r\n"` — the library expands it when `cfg.crlf` is set,
and a literal `\r\n` would produce a double carriage return.

`cli_printf()` is convenient but pulls `vsnprintf` into the image. On a tight
part, build with `CLI_ENABLE_PRINTF=0` and use the integer writers; see
[configuration](configuration.md#cutting-flash).

## Reaching your own data

Set `cfg.user` and every handler receives it:

```c
cfg.user = &board_state;

static int motor_stop(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *board = (board_t *)user;
    ...
}
```

This keeps handlers free of globals, and lets two consoles (UART and USB) share
one command table while acting on different contexts.

## What users get for free

Nothing below needs any code from you.

| | |
|---|---|
| `help`, `help led blink` | generated from the table, never out of date |
| `history` | recent commands |
| TAB | completes commands, subcommands and enum values |
| ↑ ↓ | browse history |
| ← → Home End | move the cursor |
| Ctrl-A/E/B/F/K/U/W/D | readline-style editing |
| Ctrl-C | abandon the line |

Full key table in [line editing](line-editing.md).

## Handler rules

Three things to remember when writing a handler:

* **Return 0 on success.** A non-zero return is reported to the user as
  `error: 'led blink' returned 2`, so reserve it for real failures.
* **Copy strings you keep.** `cli_arg_str()` points into an internal buffer
  that the next command overwrites. Copy before storing.
* **Never call `cli_exec()` from a handler.** It reuses the buffers the running
  handler is reading from. It is for running lines programmatically — at
  start-up, from a script, or in tests.

Handlers run inside `cli_poll()`, so a slow handler delays your main loop. Kick
off long work and report progress rather than blocking.

## Trying it without hardware

The repository builds a host demo:

```sh
cmake -S . -B build
cmake --build build
./build/examples/host_demo/clipilot_demo
```

It has a small tree (`led`, `i2c`, `echo`, `set`, `quit`) and is the quickest
way to see completion and editing behave before wiring anything to a board.

## Common mistakes

| Symptom | Cause |
|---|---|
| Nothing happens; console silent | `getc_fn` blocks instead of returning a negative value when idle |
| Output double-spaced | writing `"\r\n"` while `cfg.crlf` is on |
| Arrow keys print `^[[A` | terminal not sending ANSI sequences, or `cfg.echo` is false |
| Random corruption at start-up | `CLI_*` macros not applied to the library — see [integration](integration.md#the-one-real-trap) |
| `help` shows a command that fails with "cannot be executed directly" | node has subcommands but no handler, and none was given |
| Counts wrong after editing a table | `arg_count` / `subcmd_count` no longer match the array |
