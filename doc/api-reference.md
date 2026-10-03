# API reference

Everything public lives in [`include/cli.h`](../include/cli.h); the tuning
macros are in [`include/cli_config.h`](../include/cli_config.h). No other
header should be included by an application — `src/cli_internal.h` is private.

## Types

### `cli_status_t`

Negative status codes produced by the core. A handler may return its own
values; anything non-zero is reported to the terminal.

| Value | Meaning |
|---|---|
| `CLI_OK` (0) | success |
| `CLI_ERR_UNKNOWN_CMD` (-1) | no command with that name |
| `CLI_ERR_UNKNOWN_SUB` (-2) | no such subcommand, or the node cannot be executed directly |
| `CLI_ERR_ARG_COUNT` (-3) | too few or too many arguments |
| `CLI_ERR_ARG_TYPE` (-4) | an argument failed conversion |
| `CLI_ERR_PARSE` (-5) | malformed line (unterminated quote) |
| `CLI_ERR_OVERFLOW` (-6) | token or argument limit exceeded |
| `CLI_ERR_EMPTY` (-7) | the line held no tokens |

`CLI_ERR_EMPTY` never reaches the application through `cli_exec()`: a blank
line is silently treated as success.

### `cli_cmd_t`

Describes a command and, through `subcmds`, a whole tree. The same struct is
used at every level.

| Field | Meaning |
|---|---|
| `name` | command word; must not contain spaces |
| `help` | one-line description for `help`; may be `NULL` |
| `subcmds` / `subcmd_count` | optional child table, nestable to any depth |
| `args` / `arg_count` | optional argument specification |
| `handler` | called once arity and types validate |

See [dispatch](dispatch.md) for how `handler` and `subcmds` interact.

### `cli_arg_spec_t`

Declaration order matters when using positional initialisers:
`name`, `type`, `optional`, `variadic`, `enum_values`, `help`.

* `optional` arguments must be **trailing** — the core counts non-optional
  specs to derive the minimum arity, so a gap in the middle makes the count
  meaningless.
* `variadic` is only meaningful on the **last** spec; it lets that spec absorb
  every remaining token, up to `CLI_MAX_ARGS`.
* `enum_values` is a `NULL`-terminated array of `const char *`, required for
  `CLI_ARG_ENUM` and also used as the source of completion candidates.

### `cli_args_t` / `cli_arg_t`

`args->count` is the number of arguments actually supplied. Read values with
the accessors rather than touching the union — they validate both the index and
the stored type.

### `cli_config_t`

| Field | Default from `cli_config_default()` | Notes |
|---|---|---|
| `commands` / `command_count` | `NULL` / 0 | must outlive the instance |
| `getc_fn` | `NULL` | required for `cli_poll()`; returns `<0` when idle |
| `putc_fn` | `NULL` | required for any output |
| `io_ctx` | `NULL` | passed to both I/O hooks |
| `user` | `NULL` | passed to every handler |
| `prompt` | `CLI_DEFAULT_PROMPT` | copied into the instance, truncated to `CLI_MAX_PROMPT_LEN` |
| `echo` | `true` | `false` silences echo and all escape sequences |
| `crlf` | `true` | expand `'\n'` to `"\r\n"` on output |

Always start from `cli_config_default()` so that fields added later keep sane
values.

## Functions

### Lifecycle

```c
cli_config_t cli_config_default(void);
void cli_init(cli_t *cli, const cli_config_t *cfg);
```

`cli_init()` zeroes the instance, copies `cfg`, and prints the first prompt.
The config struct itself need not outlive the call; the **command table it
points to must**.

### Input

```c
void cli_poll(cli_t *cli);
void cli_feed(cli_t *cli, char c);
void cli_feed_buf(cli_t *cli, const char *data, size_t len);
```

`cli_poll()` calls `getc_fn` repeatedly until it returns a negative value or
`CLI_POLL_BUDGET` bytes have been consumed, whichever comes first. The budget
bounds worst-case latency when a fast link keeps the source non-empty; set
`CLI_POLL_BUDGET` to 0 to always drain fully.

`cli_feed*()` push bytes in without a source callback — useful from an ISR or
a DMA completion. Handlers then execute in that context.

Note that a command handler runs **inside** these calls, so their duration is
the duration of the slowest handler.

### Execution

```c
int cli_exec(cli_t *cli, const char *line);
void cli_set_prompt(cli_t *cli, const char *prompt);
```

`cli_exec()` runs a line as if typed, bypassing the editor and history, and
returns a `cli_status_t` or the handler's return value. It is the entry point
used by most unit tests. **It must not be called from a handler** — it reuses
the instance's parse buffers.

`cli_set_prompt()` is safe from a handler; the repaint is deferred to the next
line.

### Output

```c
void cli_write(cli_t *cli, const char *s);
void cli_write_len(cli_t *cli, const char *s, size_t len);
void cli_putc(cli_t *cli, char c);
void cli_write_int(cli_t *cli, int32_t value);
void cli_write_uint(cli_t *cli, uint32_t value, unsigned base);
int  cli_printf(cli_t *cli, const char *fmt, ...);   /* CLI_ENABLE_PRINTF */
int  cli_vprintf(cli_t *cli, const char *fmt, va_list ap);
```

`cli_write()` and `cli_write_len()` tolerate a `NULL` string. `cli_write_uint()`
clamps an out-of-range base to 10 and emits lowercase digits. The integer
writers exist so that a firmware build can drop `CLI_ENABLE_PRINTF` and with it
the `vsnprintf` dependency — see [configuration](configuration.md).

`cli_printf()` formats into a `CLI_PRINTF_BUF_LEN` stack buffer and **truncates**
rather than growing; it returns the number of bytes written after truncation, or
a negative value if `vsnprintf` failed.

### Help

```c
void cli_print_help(cli_t *cli);
void cli_print_usage(cli_t *cli, const cli_cmd_t *cmd, const char *path);
```

Both render from the command table, so usage text never goes stale. `path` is
the full invocation path (`"led blink"`); passing `NULL` falls back to
`cmd->name`, which loses the parent prefix for a subcommand.

### Argument accessors

```c
bool        cli_arg_present(const cli_args_t *args, size_t index);
int32_t     cli_arg_int  (const cli_args_t *args, size_t index, int32_t fallback);
uint32_t    cli_arg_uint (const cli_args_t *args, size_t index, uint32_t fallback);
const char *cli_arg_str  (const cli_args_t *args, size_t index, const char *fallback);
bool        cli_arg_bool (const cli_args_t *args, size_t index, bool fallback);
int         cli_arg_enum (const cli_args_t *args, size_t index, int fallback);
float       cli_arg_float(const cli_args_t *args, size_t index, float fallback);
```

Each returns `fallback` when the index is out of range **or** the stored value
has a different type, so a handler never needs to bounds check. That makes
optional arguments a one-liner:

```c
uint32_t period = cli_arg_uint(args, 0, 500);   /* 500 when omitted */
```

`cli_arg_uint()` accepts both `CLI_ARG_UINT` and `CLI_ARG_HEX` values, since
both are stored in `val.u`. `cli_arg_enum()` returns the **index** into
`enum_values`, which is what makes an enum argument usable in a `switch`.

Use `cli_arg_present()` when the distinction between "omitted" and "supplied a
value that equals the fallback" matters.

`cli_arg_str()` returns a pointer into the instance's parse buffer. It is
invalidated by the next line; copy it if it must persist.

## Minimal application skeleton

```c
static int reboot_cmd(cli_t *cli, const cli_args_t *args, void *user) {
    (void)user;
    cli_printf(cli, "rebooting in %u s\n", (unsigned)cli_arg_uint(args, 0, 0));
    return 0;
}

static const cli_arg_spec_t reboot_args[] = {
    { "delay", CLI_ARG_UINT, true, false, NULL, "seconds to wait" },
};

static const cli_cmd_t commands[] = {
    { "reboot", "restart the MCU", NULL, 0, reboot_args, 1, reboot_cmd },
};

static cli_t cli;

void cli_task(void) {
    cli_config_t cfg = cli_config_default();

    cfg.commands      = commands;
    cfg.command_count = sizeof(commands) / sizeof(commands[0]);
    cfg.getc_fn       = uart_getc;
    cfg.putc_fn       = uart_putc;

    cli_init(&cli, &cfg);
    for (;;) {
        cli_poll(&cli);
    }
}
```
