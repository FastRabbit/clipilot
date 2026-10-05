---
description: C coding conventions for the clipilot embedded CLI sources.
applyTo: '**/*.{c,h}'
---

# C Style for clipilot

Target is C11, freestanding-friendly, compiled with
`-Wall -Wextra -Werror -Wshadow -Wconversion -Wundef -Wstrict-prototypes
-Wmissing-prototypes -Wpointer-arith`. Code that does not survive that set is
not acceptable.

## Embedded constraints

* No dynamic allocation, no VLAs, no mutable globals. State belongs in the
  caller-owned `cli_t`; tables are `static const`.
* No unbounded or locale-dependent libc: not `strcpy`, `strcat` on untrusted
  lengths, `sprintf`, `atoi`, `strtol`, `isalpha`. Use the local helpers.
* Anything pulling in `printf` or soft-float must sit behind `CLI_ENABLE_PRINTF`
  or `CLI_ENABLE_FLOAT`.
* Buffer sizes come from `CLI_*` macros in `cli_config.h`, never from literals.

## Formatting

* 4 spaces, no tabs. Soft limit 90 columns.
* K&R braces; braces on **every** `if`/`for`/`while`, including one-liners.
* Declarations at the top of a block, separated from statements by a blank line.
* `/* ... */` comments. Doxygen `/** ... */` on every public declaration in
  `include/cli.h` and on internal declarations in `src/cli_internal.h`.
* Comment only what needs clarifying — the *why*, not a restatement of the code.

## Types and conversions

* `-Wconversion` is on, so make every narrowing explicit and deliberate:
  `cli->hist_head = (uint8_t)((cli->hist_head + 1u) % CLI_HISTORY_DEPTH);`
  and `raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);`
* Use `size_t` for indices and lengths, and suffix unsigned literals with `u`
  when they meet an unsigned operand: `for (i = 0u; i < count; i++)`.
* Use the fixed-width types from `stdint.h` for values that cross the API
  boundary (`int32_t`, `uint32_t`), not bare `int`/`long`.
* Compare pointers explicitly: `if (p != NULL)`, not `if (p)`.

## Functions

* Every non-static function needs a prototype in a header —
  `-Wmissing-prototypes` enforces it. Anything not in `cli.h` or
  `cli_internal.h` must be `static`.
* Use `(void)` for empty parameter lists, never `()`.
* Silence deliberately unused parameters with `(void)name;` at the top of the
  body; handlers routinely ignore `args` or `user`.
* Prefer early return for error paths over deep nesting.

## Switch statements

Handle every enumerator or provide a `default`, and mark intentional no-ops so
the reader knows they are deliberate:

```c
} else if (spec->type != CLI_ARG_STR) {
    cli_write(cli, ":");
    cli_write(cli, cli_type_name(spec->type));
} else {
    /* plain strings need no type annotation */
}
```

## Naming

* Public API: `cli_` prefix, `snake_case` (`cli_arg_uint`).
* Types: `_t` suffix (`cli_cmd_t`, `cli_arg_spec_t`).
* Macros and enumerators: `CLI_` prefix, `SCREAMING_SNAKE_CASE`.
* File-static helpers: short unprefixed `snake_case` (`str_copy`, `parse_u32`).
