# Extending the library

## Adding a command

Nothing in the core needs to change — commands are pure data.

```c
static int flash_erase(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *board = (board_t *)user;           /* cfg.user */
    uint32_t addr = cli_arg_uint(args, 0, 0u);
    uint32_t len  = cli_arg_uint(args, 1, 4096u);   /* optional */

    if (!board_erase(board, addr, len)) {
        cli_write(cli, "erase failed\n");
        return 1;                                /* reported to the terminal */
    }
    cli_printf(cli, "erased %u bytes at 0x%08x\n", (unsigned)len, (unsigned)addr);
    return 0;
}

static const cli_arg_spec_t flash_erase_args[] = {
    { "addr", CLI_ARG_HEX,  false, false, NULL, "start address" },
    { "len",  CLI_ARG_UINT, true,  false, NULL, "bytes to erase" },
};

static const cli_cmd_t flash_subs[] = {
    { "erase", "erase a region", NULL, 0, flash_erase_args, 2, flash_erase },
};

static const cli_cmd_t commands[] = {
    { "flash", "flash operations", flash_subs, 1, NULL, 0, NULL },
};
```

Checklist:

* Tables are `static const` so they live in flash.
* `arg_count` and `subcmd_count` must match the arrays — use
  `sizeof(x) / sizeof(x[0])` where the table is not a literal.
* Optional specs go last; only the final spec may be `variadic`.
* Return 0 on success. A non-zero return is printed as
  `error: '<path>' returned <n>`, so use it for real failures only.
* Never call `cli_exec()` from a handler — see
  [architecture](architecture.md#re-entrancy-rules).
* Copy any `cli_arg_str()` result you need after the handler returns.

Nothing needs to be registered: `help` and completion both read the same
tables, so a new command is documented and completable the moment it compiles.

## Adding an argument type

This is the one change that touches several files, and a miss shows up as a
runtime misbehaviour rather than a compile error. All five:

| # | File | What to add |
|---|---|---|
| 1 | `include/cli.h` | the enumerator in `cli_arg_type_t`, and a member in the `cli_arg_t` union if no existing one fits |
| 2 | `src/cli_parse.c` | the converter function and its `case` in `cli_convert_token()` |
| 3 | `src/cli.c` | the label in `cli_type_name()` and a `cli_arg_*()` accessor |
| 4 | `src/cli_builtin.c` | any special rendering in `write_arg_spec()` |
| 5 | `src/cli_complete.c` | candidate generation in `collect()`, if the type has a known value set |

Steps 4 and 5 are the ones that get forgotten: skip 4 and usage text shows a
wrong or missing type name; skip 5 and TAB silently offers nothing.

If the type is guarded by a feature macro, guard **every one** of the five
sites with the same macro and add a variant to the compile matrix.

Reuse an existing union member where the representation matches — that is why
`CLI_ARG_HEX` shares `val.u` with `CLI_ARG_UINT` and needs no new accessor.

## Adding a key binding

1. Define the byte as a `CLI_CH_*` macro at the top of
   [`src/cli_edit.c`](../src/cli_edit.c).
2. Add a `case` to the `switch` in `cli_edit_feed()`.
3. For an escape sequence, extend `handle_escape()` instead — dispatch on the
   final byte, or on the numeric parameter for a `~` sequence.

Keep the action in a small `static` helper next to the existing `move_*` and
`kill_*` functions, and make it a no-op rather than an error when it cannot
apply (e.g. moving left at column 0). Guard any terminal output with
`cli->cfg.echo`.

## Adding a built-in

Built-ins live in the `builtins[]` table in
[`src/cli_builtin.c`](../src/cli_builtin.c), inside `#if CLI_ENABLE_BUILTINS`.
They are ordinary commands; the only difference is that `cli_lookup_top()`
searches them **after** the application table, so an application command of the
same name wins.

A built-in that depends on another feature needs its own guard — `history` is
wrapped in `#if CLI_HISTORY_DEPTH > 0u` as well.

## Style and validation

Code conventions are in
[`.github/instructions/c-style.instructions.md`](../.github/instructions/c-style.instructions.md).
The build uses `-Wall -Wextra -Werror -Wconversion`, so integer narrowing needs
an explicit cast and unused parameters need `(void)x;`.

Before committing, run the tests and the compile matrix described in
[testing](testing.md).
