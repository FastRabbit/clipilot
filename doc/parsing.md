# Tokenizer and argument conversion

Implemented in [`src/cli_parse.c`](../src/cli_parse.c).

## Tokenizer

`cli_tokenize()` splits `cli->line` into `cli->tokens[]`, copying the resolved
text into `cli->parse_buf` as a sequence of NUL-terminated strings. The source
line is left untouched, so the editor can still repaint it.

### Grammar

* Tokens are separated by spaces or tabs; runs of separators collapse.
* `"double quotes"` group a run of characters and **do** process backslash
  escapes.
* `'single quotes'` group without escape processing — a backslash inside single
  quotes is a literal backslash.
* A backslash outside quotes escapes the next character, including a space.
* Quotes may open and close mid-token; the quote characters are removed and
  only their content is kept, so `ab"cd ef"gh` is the single token
  `abcd efgh`.

### Escapes

`\n \r \t \a \b \f \v` map to the corresponding control character. Any other
escaped character yields itself, which is how `\ `, `\"`, `\'` and `\\` work.
A trailing backslash at the very end of the line is kept literally, since there
is no following character to escape.

### Results

| Outcome | Return |
|---|---|
| at least one token | `CLI_OK` |
| only whitespace | `CLI_ERR_EMPTY` (treated as success by the caller) |
| a quote was never closed | `CLI_ERR_PARSE` → `error: unterminated quote` |
| more than `CLI_MAX_TOKENS` tokens, or the text does not fit `parse_buf` | `CLI_ERR_OVERFLOW` → `error: line too complex` |

Because escape sequences and quote characters shrink the text, `parse_buf`
(same size as `line`) can always hold the result; the overflow check is a
belt-and-braces guard.

> The `tokens[]` pointers, and every `raw` / `val.s` derived from them, point
> into `parse_buf` and are only valid until the next line is parsed.

## Number literals

`CLI_ARG_INT` and `CLI_ARG_UINT` share one grammar:

```
[+|-] [0x|0X|0b|0B|0o|0O] digits
```

* Base prefixes select 16, 2 and 8; without a prefix the base is 10.
* `_` may appear anywhere among the digits as a grouping separator: `1_000_000`,
  `0b1010_1010`.
* The **whole token** must be consumed. `12abc` and `0x` are errors, unlike
  `strtol()` which would silently accept a prefix. This is deliberate: a typo
  should be reported, not truncated.
* Overflow is detected before it happens (`acc > (UINT32_MAX - d) / base`), so
  `0xFFFFFFFFF` is rejected rather than wrapping.
* `CLI_ARG_INT` accepts `-2147483648 … 2147483647`; a negative sign with a
  magnitude above `2^31` is rejected.
* `CLI_ARG_HEX` is always base 16 and accepts an optional `0x` prefix, so
  register values can be typed either way. The value lands in `val.u` and is
  read with `cli_arg_uint()`.

## Booleans

Case-insensitive, matched against fixed word lists:

| True | False |
|---|---|
| `1`, `on`, `true`, `yes`, `y`, `enable` | `0`, `off`, `false`, `no`, `n`, `disable` |

Anything else is a conversion error.

## Enums

Matched case-insensitively against the `NULL`-terminated `enum_values` array.
The stored value is the **index**, so a handler can `switch` on it:

```c
static const char *const modes[] = { "idle", "run", "stop", NULL };
/* ... */
switch (cli_arg_enum(args, 0, -1)) {
case 0: /* idle */ break;
case 1: /* run */  break;
}
```

Keep the enum table and the `switch` adjacent — nothing checks that they agree.

## Floats

Only compiled when `CLI_ENABLE_FLOAT` is 1.

The converter is hand-written (`[+|-] digits [. digits] [e[+|-]digits]`) rather
than `strtod`, to avoid pulling a large libc routine and a soft-float
dependency into a Cortex-M0 image. Consequences worth knowing:

* Accumulating digit-by-digit and scaling by powers of ten means the result can
  differ from a correctly-rounded `strtod` in the last bits. Fine for a
  setpoint, not for round-tripping.
* The exponent is capped at 38; beyond that the token is rejected instead of
  producing an infinity.
* `inf`, `nan` and hex floats are not accepted.
* As with integers, trailing junk is an error.

Set `CLI_ENABLE_FLOAT=0` when no command needs a float; the `val.f` union
member and the accessor disappear with it.

## Adding a type

Five places must change, and missing one fails at runtime rather than at
compile time. See [extending](extending.md#adding-an-argument-type).
