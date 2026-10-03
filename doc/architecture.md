# Architecture

## Design goals

clipilot targets microcontrollers, so the design is driven by four constraints:

1. **No dynamic allocation.** All state lives in a caller-owned `cli_t`.
2. **No mutable globals.** Several instances (UART, USB CDC, RTT) can run
   concurrently without interfering.
3. **Non-blocking.** `cli_poll()` returns promptly so it can sit in a
   cooperative main loop next to other work.
4. **Handlers never see invalid input.** Arity and type checking happen in the
   core, so a handler body is free of validation boilerplate.

## Module map

| File | Responsibility | Depends on |
|---|---|---|
| `src/cli.c` | Instance lifecycle, output primitives, command lookup and dispatch, argument accessors | all others |
| `src/cli_parse.c` | Tokenizer, scalar converters, arity validation | `cli.c` (output, `cli_print_usage`) |
| `src/cli_edit.c` | Byte dispatcher, key bindings, escape decoder, history ring | `cli.c`, `cli_complete.c` |
| `src/cli_complete.c` | TAB completion over commands, subcommands and enum values | `cli.c`, `cli_edit.c` |
| `src/cli_builtin.c` | Usage/help rendering, `help` and `history` commands | `cli.c` |

`src/cli_internal.h` declares everything shared between these translation
units. It is **not** installed and must not be included by applications.

The only mutable state is `cli_t`. Command tables are `static const` and belong
in flash.

## Data flow of one keystroke

```
application main loop
        │
        ▼
   cli_poll()  ──── cfg.getc_fn() ───► byte (or <0 → return)
        │
        ▼
 cli_edit_feed(byte)                       [cli_edit.c]
        │
        ├─ escape state machine active? ──► collect / handle_escape()
        ├─ control character?            ──► cursor move, kill, history, …
        ├─ TAB?                          ──► cli_complete()      [cli_complete.c]
        ├─ printable 0x20..0x7E?         ──► cli_edit_insert()
        └─ CR / LF?                      ──► cli_submit_line()   [cli.c]
                                                   │
                                                   ▼
                                          cli_hist_push()
                                          cli_run_line()
                                                   │
                        ┌──────────────────────────┤
                        ▼                          ▼
                 cli_tokenize()            cli_dispatch()
                 [cli_parse.c]             [cli.c]
                        │                          │
                        │            walk subcommand chain
                        │                          │
                        │                  cli_build_args()  [cli_parse.c]
                        │                          │
                        │                  cli_convert_token() per token
                        │                          │
                        └──────────────────────────┤
                                                   ▼
                                          cmd->handler(cli, args, user)
                                                   │
                                                   ▼
                                          cli_edit_reset() + new prompt
```

Output always funnels through `cli_putc()`, which performs the optional
`'\n'` → `"\r\n"` expansion. **Library code therefore writes plain `"\n"`
only** — writing `"\r\n"` directly would double the carriage return on a
`crlf` configuration.

## State ownership inside `cli_t`

| Group | Fields | Notes |
|---|---|---|
| Configuration | `cfg`, `prompt` | `cfg` is copied by `cli_init()`; the command table it points at must outlive the instance |
| Edit buffer | `line`, `len`, `cursor`, `overflow` | `line` is always NUL-terminated at `len` |
| Escape decoder | `esc_state`, `esc_len`, `esc_buf`, `skip_lf` | see [line editing](line-editing.md) |
| History | `hist`, `hist_stash`, `hist_count`, `hist_head`, `hist_pos` | compiled out when `CLI_HISTORY_DEPTH == 0` |
| Parse scratch | `parse_buf`, `tokens`, `token_count`, `args` | valid only during one dispatch |
| Flags | `started`, `executing` | `executing` guards re-entrant output |

`parse_buf` holds the NUL-separated token text carved out of `line`; the
`tokens[]` pointers and every `cli_arg_t::raw` / `val.s` point **into
`parse_buf`**. They are therefore only valid for the duration of the handler
call. A handler that needs a string beyond its own return must copy it.

## Invariants

These are relied on throughout the code. Breaking one tends to fail silently
rather than at compile time.

1. **No `malloc`/`free`, no VLAs, no recursion** in `src/`. The dispatch walk
   over subcommands is an iterative loop precisely so that stack depth is
   independent of tree depth.
2. **No mutable file-scope state.** Everything reachable from a public function
   is either `const` or reached through the `cli_t *` parameter.
3. **Bounded libc only.** `strlen`, `strcmp`, `memcpy`, `memmove`, `memset`,
   `strncpy`, `strcat` on checked bounds. No `strtol`, no `sscanf`, no `strtok`
   — the converters in `cli_parse.c` exist to avoid dragging those in and to
   keep behaviour identical across toolchains.
4. **Every feature macro must compile when switched off.** `CLI_ENABLE_FLOAT`,
   `CLI_ENABLE_PRINTF`, `CLI_ENABLE_BUILTINS` and `CLI_HISTORY_DEPTH == 0` are
   all exercised by the compile matrix in [testing](testing.md).
5. **Handlers receive validated arguments only.** `cli_build_args()` runs
   before the handler, so `args->count` is within the declared arity and each
   `args->v[i]` converted successfully.

## Re-entrancy rules

* `cli_exec()` **must not** be called from inside a handler. It overwrites
  `line`, `parse_buf`, `tokens` and `args` — the very buffers the running
  handler is reading from.
* `cli_set_prompt()` *is* safe inside a handler. It checks the `executing`
  flag and skips the repaint; the new prompt appears with the next line. This
  guard exists because of a real bug: without it, a handler that changed the
  prompt repainted the already-submitted input line in the middle of its own
  output.
* `cli_write*()` and `cli_printf()` are safe anywhere.
* Feeding bytes from an ISR with `cli_feed()` means handlers run in interrupt
  context. Prefer an ISR-filled ring buffer drained by `cli_poll()` from the
  main loop — see [porting](porting.md).

## Error reporting

Diagnostics are emitted by the core, not by handlers. Every message starts with
the `error: ` prefix written by `cli_error_prefix()`, and argument-count
failures are followed by the generated usage line, so the terminal output for a
mistyped command is self-explanatory without any per-command help text.

`cli_status_t` values are negative; a handler's own non-zero return is reported
as `'<path>' returned <n>` and passed back out of `cli_exec()`.
