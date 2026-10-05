---
name: clipilot-architecture
description: Architecture, invariants and extension recipes for the clipilot embedded CLI library. Use when adding or changing commands, argument types, editing keys, completion behaviour, or when touching anything in src/ or include/.
---

# clipilot Architecture

clipilot is an allocation-free command line interface for MCUs (STM32 class).
The application supplies two callbacks — a non-blocking character source and a
character sink — and a statically declared command table.

> Long-form reference documentation lives in [`doc/`](../../../doc/README.md):
> per-module internals, the exact parsing grammar, the escape state machine,
> completion rules and the API reference. This skill is the condensed version
> plus the extension recipes; consult `doc/` when you need the detail.

## Module map

| File | Responsibility |
|---|---|
| `include/cli.h` | The only public header. Types, command/arg specs, `cli_t`, all API. |
| `include/cli_config.h` | Compile-time size knobs and feature switches. Every macro is `#ifndef` guarded so it can be overridden from the build. |
| `src/cli_internal.h` | Cross-TU declarations. **Not public** — never include from application code. |
| `src/cli.c` | Lifecycle (`cli_init`/`cli_poll`/`cli_feed`), output helpers, command/subcommand lookup and dispatch, argument accessors. |
| `src/cli_parse.c` | Tokenizer (quotes + escapes) and typed token conversion + arity validation. |
| `src/cli_edit.c` | Byte-level line editor: escape state machine, cursor motion, kill keys, history ring. |
| `src/cli_complete.c` | TAB completion over commands, subcommands and enum argument values. |
| `src/cli_builtin.c` | Usage/help rendering plus the `help` and `history` built-ins. |

Data flows strictly one way:
`cli_poll` → `cli_edit_feed` → (on Enter) `cli_submit_line` → `cli_tokenize` →
`cli_dispatch` → `cli_build_args` → handler.

## Hard invariants

These are the rules that keep the library usable on a microcontroller. Breaking
one is a defect even if the tests still pass.

1. **No dynamic allocation.** No `malloc`, `calloc`, `strdup` or VLAs anywhere.
   Every buffer is a fixed array inside `cli_t`, sized by a `CLI_*` macro.
2. **No mutable globals.** All state lives in the caller-owned `cli_t` so that
   several instances (UART + USB CDC) can run concurrently. Command tables and
   enum value lists are `static const` — read-only, shareable.
3. **No unbounded libc.** `strcpy`, `sprintf`, `atoi`, `strtol` are banned. Use
   the local `str_copy` helper, `cli_write_*`, and the hand-rolled converters in
   `cli_parse.c`. `vsnprintf` is allowed only inside `#if CLI_ENABLE_PRINTF`.
4. **Every feature macro must still compile when switched off.** Guard new code
   with the matching `#if` and verify (see the `clipilot-build-test` skill).
5. **Handlers never see invalid input.** Arity and type validation happen in
   `cli_build_args` before the handler is called.

## Adding a command

Command and subcommand are the same type, `cli_cmd_t`, so nesting is free.
Field order is `name, help, subcmds, subcmd_count, args, arg_count, handler`.

```c
static int cmd_uart_baud(cli_t *cli, const cli_args_t *args, void *user)
{
    app_t *app = (app_t *)user;

    app->baud = cli_arg_uint(args, 0u, 115200u);
    cli_printf(cli, "baud = %u\n", (unsigned)app->baud);
    return 0;                       /* non-zero is reported to the terminal */
}

static const cli_arg_spec_t baud_args[] = {
    /* name,   type,          optional, variadic, enum_values, help */
    { "rate",  CLI_ARG_UINT,  false,    false,    NULL,        "bits per second" },
};

static const cli_cmd_t uart_subs[] = {
    { "baud", "set the baud rate", NULL, 0u, baud_args, 1u, cmd_uart_baud },
};

static const cli_cmd_t commands[] = {
    { "uart", "UART settings", uart_subs, 1u, NULL, 0u, NULL },
};
```

Semantics worth remembering:

* A command with subcommands and **no** handler *requires* a subcommand.
* A command with subcommands **and** a handler treats an unmatched second word
  as its first argument instead of erroring.
* `optional` arguments must be trailing; test them with `cli_arg_present()`.
* `variadic` is only meaningful on the **last** spec, where it soaks up the
  remaining tokens up to `CLI_MAX_ARGS`.
* Help text and usage lines are generated from the spec — never hand-write a
  usage string in a handler.

## Adding an argument type

A new type touches five places. Missing one produces a silent misbehaviour
rather than a compile error, so work the list:

1. `include/cli.h` — add the `CLI_ARG_*` enumerator **and** a member in the
   `cli_arg_t` union.
2. `src/cli_parse.c` — write the converter and add a `case` to
   `cli_convert_token()`.
3. `src/cli.c` — add a `case` to `cli_type_name()` (used in error messages) and
   add a `cli_arg_<type>()` accessor.
4. `src/cli_builtin.c` — extend `write_arg_spec()` if the type needs special
   rendering in usage output (as `CLI_ARG_ENUM` does).
5. `src/cli_complete.c` — extend `collect()` if the type can offer completion
   candidates.

Converters return `bool` and must reject trailing garbage (`"12ab"` is not an
integer) and detect overflow before it happens, e.g.
`if (acc > ((0xFFFFFFFFu - d) / base)) return false;`.

## Output from handlers

Prefer the allocation-free helpers so a firmware build can drop printf
entirely: `cli_write()`, `cli_putc()`, `cli_write_int()`, `cli_write_uint()`.
`cli_printf()` exists but is compiled out when `CLI_ENABLE_PRINTF=0`, so the
core itself must never depend on it.

`cli_putc()` expands `'\n'` to `"\r\n"` when `cfg.crlf` is set. Write plain
`"\n"` in library and handler code and let the core do the translation.

## Escape sequence handling

`cli_edit_feed()` is a three-state machine: `CLI_ESC_NONE` → `CLI_ESC_GOT`
(after `ESC`) → `CLI_ESC_SEQ` (after `[` or `O`), terminated by a byte in
`0x40..0x7E`. Unknown sequences are swallowed, never echoed. The buffer is
capped at `CLI_MAX_ESC_LEN` so a runaway sequence cannot wedge the parser.

Keys arrive as raw bytes, so both the ANSI form and the control-character form
must be handled for the same action (`ESC [ A` and `Ctrl-P` both mean "history
previous").

## Consuming the library from another build

The `CLI_*` macros change the layout of `cli_t`. They must therefore be applied
to the **library target**, `PUBLIC`, so the library and the application compile
against the same struct:

```cmake
target_compile_definitions(clipilot PUBLIC CLI_HISTORY_DEPTH=4)   # right
target_compile_definitions(my_firmware PRIVATE CLI_HISTORY_DEPTH=4) # WRONG
```

The wrong form compiles and links silently. The application then sees a
648-byte `cli_t` while the library still uses the 1716-byte default layout, and
`cli_init()`'s `memset` runs 516 bytes past the end of the caller's instance.
The symptom is unrelated data corrupting at start-up.

The target is named `clipilot` however the sources arrive — submodule,
subtree, `FetchContent` or vendored — so this does not change with the
integration method. `find_package(clipilot)` is not supported: there are no
`install()` rules, by design. See [`doc/integration.md`](../../../doc/integration.md).

## Learnings

* **Guard terminal repaints that can happen mid-command.** `cli_set_prompt()`
  originally called `cli_edit_refresh()` unconditionally. When a handler such as
  `set prompt foo` called it, the editor repainted the *old* input line in the
  middle of command output, corrupting the display. The fix is the `executing`
  flag on `cli_t`, set around `cli_dispatch()`. Any new API that a handler might
  call and that touches the screen needs the same guard.
* **`cli_exec()` must not be called from a handler.** It reuses the instance's
  `parse_buf`, `tokens` and `args`, so re-entering it clobbers the arguments of
  the command currently running. This is documented on the declaration; keep it
  that way if the function is changed.
* **Distinguish "no candidates" from "nothing more to insert" in completion.**
  With candidates `flag` and `fail`, the longest common prefix of `f` is still
  `f`, so nothing can be inserted and the correct behaviour is to *list* the
  candidates. Only fall back to listing when `lcp <= prefix_len`, and beep only
  when the candidate count is genuinely zero.
* **Handlers must return promptly; they share the thread with `cli_poll()`.**
  Anything periodic belongs in the main loop with the handler only recording
  intent. The Pico example's `led blink` stores a period and returns; the
  toggling happens in `led_tick()`. A handler that slept would freeze the
  console for the duration.
* **Let exactly one layer translate newlines.** `cli_putc()` already expands
  `'\n'` to `"\r\n"` when `cfg.crlf` is set, so the sink must write the byte
  raw. On the Pico SDK that means `putchar_raw()`, not `putchar()`, which
  performs its own translation and yields a doubled carriage return.
* **Pass application state through `cfg.user`, never a global.** It arrives as
  the handler's third parameter, so a handler casts it directly:
  `board_t *b = (board_t *)user;`. This is what keeps command tables
  `static const` and lets two instances share one table with different state.
