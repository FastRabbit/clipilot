# Line editing and history

Implemented in [`src/cli_edit.c`](../src/cli_edit.c). Entry point:
`cli_edit_feed()`, which receives every byte from `cli_poll()` or `cli_feed()`.

## The byte dispatcher

`cli_edit_feed()` is a three-way decision, checked in this order:

1. **Escape decoder active?** The byte belongs to an escape sequence.
2. **Control character or TAB / CR / LF?** Run the bound action.
3. **Printable (`0x20 … 0x7E`)?** Insert at the cursor.

Anything else — including bytes ≥ `0x80` — is **silently dropped**. There is no
UTF-8 support: a multi-byte character would otherwise corrupt the cursor
arithmetic, which counts bytes, not glyphs.

## Key bindings

| Key | Byte / sequence | Action |
|---|---|---|
| `Enter` | `CR`, `LF`, or `CRLF` | submit the line |
| `Backspace` | `0x08`, `0x7F` | delete left of the cursor |
| `Tab` | `0x09` | [complete](completion.md) |
| `Ctrl-A` / `Home` | `0x01`, `ESC[H`, `ESC[1~`, `ESC[7~` | cursor to start |
| `Ctrl-E` / `End` | `0x05`, `ESC[F`, `ESC[4~`, `ESC[8~` | cursor to end |
| `Ctrl-B` / `←` | `0x02`, `ESC[D` | cursor left |
| `Ctrl-F` / `→` | `0x06`, `ESC[C` | cursor right |
| `Ctrl-D` / `Del` | `0x04`, `ESC[3~` | delete under the cursor |
| `Ctrl-K` | `0x0B` | kill to end of line |
| `Ctrl-U` | `0x15` | kill to start of line |
| `Ctrl-W` | `0x17` | kill the previous word |
| `Ctrl-P` / `↑` | `0x10`, `ESC[A` | older history entry |
| `Ctrl-N` / `↓` | `0x0E`, `ESC[B` | newer history entry |
| `Ctrl-L` | `0x0C` | clear the screen and repaint |
| `Ctrl-C` | `0x03` | print `^C`, abandon the line |

`Ctrl-D` deletes a character; it is **not** an end-of-input signal. A firmware
CLI has no meaningful "close stdin", and binding it to delete matches readline.

### CR/LF handling

A `CR` submits the line and sets `skip_lf`. If the next byte is `LF` it is
swallowed, so a `CRLF` terminal submits once. Any other byte clears the flag,
so a lone `LF` later still works. This makes the CLI indifferent to whether the
terminal sends `CR`, `LF` or `CRLF`.

## Escape decoding

A three-state machine, with the state in `cli->esc_state`:

```
CLI_ESC_NONE ──ESC──► CLI_ESC_GOT ──'[' or 'O'──► CLI_ESC_SEQ
      ▲                    │                            │
      │              any other byte              final byte 0x40..0x7E
      │                    │                     → handle_escape()
      └────────────────────┴────────────────────────────┘
```

* In `CLI_ESC_GOT`, a byte that is neither `[` nor `O` returns to `NONE` and is
  discarded — an ESC keypress alone is a no-op, not a stuck state.
* In `CLI_ESC_SEQ`, bytes accumulate into `esc_buf` until a final byte in
  `0x40 … 0x7E` arrives. Accepting `O` as an introducer covers the SS3
  sequences some terminals send for arrows in application cursor mode.
* If `CLI_MAX_ESC_LEN` bytes accumulate without a final byte, the machine
  **resets anyway**. Without that escape hatch a malformed or truncated
  sequence on a noisy UART would wedge the parser permanently and the CLI would
  appear dead.

`handle_escape()` dispatches on the final byte. For `~` it parses the numeric
parameter out of `esc_buf` (1/7 = Home, 4/8 = End, 3 = Delete), covering the
variants different terminals emit.

## Screen updates

Two primitives, and choosing the wrong one is the usual source of display bugs:

| Function | Emits | Use when |
|---|---|---|
| `cli_draw_prompt()` | prompt + line, no cursor positioning | after a newline, when the cursor is already at column 0 |
| `cli_edit_refresh()` | `CR`, `ESC[K` (erase to end of line), prompt, line, then `ESC[<n>D` to place the cursor | the line changed in place |

Insertion at the end of the line is optimised: the inserted text is echoed
directly instead of repainting. Any edit **not** at the end calls
`cli_edit_refresh()`. Both are no-ops when `cfg.echo` is false, so a dumb
terminal or a machine-to-machine link never receives escape sequences.

`csi_move()` omits the numeric parameter when it is 1 (`ESC[D` rather than
`ESC[1D`) and emits nothing for 0, which keeps the byte count down on a slow
UART.

The whole line is repainted on a single terminal row. A line longer than the
terminal width will wrap and the cursor arithmetic will not account for it;
keep `CLI_MAX_LINE_LEN` at or below the expected width if that matters.

## Overflow handling

`cli_edit_insert()` inserts as much as fits, sets `cli->overflow` when it had
to truncate, and beeps (`\a`) when nothing fits at all. The flag is checked at
submit time: an overflowed line is **discarded** with `error: line too long`
rather than executing a silently truncated command — which could otherwise turn
`flash erase 0x8000000 4096` into something with a different meaning.

## History

Compiled out entirely when `CLI_HISTORY_DEPTH` is 0.

* A fixed ring of `CLI_HISTORY_DEPTH` slots, each `CLI_MAX_LINE_LEN + 1` bytes.
  `hist_head` is the next slot to write; `hist_count` saturates at the depth.
* `cli_hist_push()` skips empty lines and a line identical to the newest entry,
  so holding Enter or repeating a command does not flood the ring.
* `cli_hist_get(0)` is the newest entry.
* `hist_pos` is `-1` while editing a fresh line, and the index into history
  while browsing.
* On the **first** up-arrow the in-progress line is saved to `hist_stash`;
  coming back down past the newest entry restores it. Browsing history
  therefore never loses what you were typing.
* Pushing a line resets `hist_pos`, so the next up-arrow starts from the top.

History is stored as the raw line text, before tokenizing, so recalling and
re-editing a quoted line works exactly as typed.
