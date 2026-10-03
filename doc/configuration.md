# Configuration

Every macro in [`include/cli_config.h`](../include/cli_config.h) is wrapped in
`#ifndef`, so it can be overridden on the compiler command line
(`-DCLI_MAX_LINE_LEN=64`) or by defining it before the header is reached. There
is no generated config header and no build-system magic.

| Macro | Default | Effect |
|---|---|---|
| `CLI_MAX_LINE_LEN` | 128 | longest editable line, in bytes |
| `CLI_HISTORY_DEPTH` | 8 | history entries; **0 removes history entirely** |
| `CLI_MAX_TOKENS` | 16 | tokens accepted on one line |
| `CLI_MAX_ARGS` | 12 | arguments passed to a handler |
| `CLI_MAX_COMPLETIONS` | 32 | candidates considered per TAB |
| `CLI_MAX_PROMPT_LEN` | 16 | prompt characters (longer prompts are truncated) |
| `CLI_MAX_ESC_LEN` | 8 | bytes buffered while decoding one escape sequence |
| `CLI_POLL_BUDGET` | 64 | bytes consumed per `cli_poll()`; 0 = drain until starved |
| `CLI_ENABLE_FLOAT` | 1 | `CLI_ARG_FLOAT` and `cli_arg_float()` |
| `CLI_ENABLE_PRINTF` | 1 | `cli_printf()` / `cli_vprintf()` |
| `CLI_PRINTF_BUF_LEN` | 128 | stack buffer used by `cli_printf()` |
| `CLI_ENABLE_BUILTINS` | 1 | the `help` and `history` commands |
| `CLI_DEFAULT_PROMPT` | `"> "` | prompt when `cfg.prompt` is `NULL` |

> All translation units must see the **same** values. Pass the overrides as
> global compile definitions, never per-file — a mismatched `CLI_MAX_LINE_LEN`
> between `cli.c` and `cli_edit.c` changes `sizeof(cli_t)` and corrupts memory.

## Cutting RAM

`sizeof(cli_t)` dominates the RAM cost and is roughly

```
(CLI_HISTORY_DEPTH + 3) × (CLI_MAX_LINE_LEN + 1)   line, history, stash, parse_buf
+ CLI_MAX_TOKENS × sizeof(char *)
+ CLI_MAX_ARGS   × sizeof(cli_arg_t)
+ prompt, escape buffer and bookkeeping
```

Measured on a 64-bit host:

| Configuration | `sizeof(cli_t)` |
|---|---|
| defaults | 1716 B |
| `CLI_MAX_LINE_LEN=64 CLI_HISTORY_DEPTH=4 CLI_MAX_TOKENS=8 CLI_MAX_ARGS=6 CLI_ENABLE_FLOAT=0` | 648 B |

A 32-bit target is smaller still, because `tokens[]` and the pointers inside
`cli_arg_t` halve. Nothing is allocated at runtime, so this figure is the whole
story — put the instance in `.bss` as a `static cli_t`.

History is by far the biggest lever: each entry costs a full line buffer.
`CLI_HISTORY_DEPTH=0` removes the ring, the stash, the browse logic and the
`history` command.

## Cutting flash

| Switch | Removes |
|---|---|
| `CLI_ENABLE_PRINTF=0` | `cli_printf`/`cli_vprintf` and the `vsnprintf` dependency — usually the single largest saving, since it drags in most of libc's formatting machinery |
| `CLI_ENABLE_FLOAT=0` | the float converter and any soft-float helpers it would pull in on a core without an FPU |
| `CLI_ENABLE_BUILTINS=0` | `help`, `history` and the table-rendering code |
| `CLI_HISTORY_DEPTH=0` | the ring and browse logic |

With `CLI_ENABLE_PRINTF=0` the output helpers `cli_write()`,
`cli_write_int()` and `cli_write_uint()` remain, which is enough for most
firmware messages.

Note that `cli_print_usage()` and `cli_print_help()` stay available even with
`CLI_ENABLE_BUILTINS=0` — only the commands that call them are removed, so an
application can still expose its own help.

## Runtime configuration

Two `cli_config_t` fields change behaviour without recompiling:

* **`echo`** — `false` suppresses all echo and escape output. The CLI still
  edits the line internally, so a host script can drive it over a raw link and
  see only command output. This is also what most unit tests use.
* **`crlf`** — `true` expands `'\n'` to `"\r\n"` in `cli_putc()`. Turn it off
  when the transport already does the translation, or when talking to a program
  rather than a terminal.

## Tuning `CLI_POLL_BUDGET`

`cli_poll()` stops after this many bytes even if more are pending, bounding how
long one call can hold the main loop when a fast link keeps the source busy.
A paste of 4 kB into a terminal is the realistic case. Set it to 0 only if the
loop has nothing else time-critical to do.

The budget does **not** bound handler execution time: a single byte can be the
`Enter` that runs a long command.

## Verifying a configuration

Any combination you ship should be compiled at least once, because the
`#if` blocks are otherwise never exercised. The unit tests only cover the
default configuration — see
[what the tests do not cover](testing.md#what-the-tests-do-not-cover) and the
ready-made compile matrix in
[`.github/skills/clipilot-build-test`](../.github/skills/clipilot-build-test/SKILL.md).
