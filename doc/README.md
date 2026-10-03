# clipilot documentation

Reference documentation for the clipilot CLI library. The top-level
[README](../README.md) is the quick start; these pages describe how the code
actually works, why it is shaped that way, and how to change it safely.

## Reading order

| # | Document | Read it when |
|---|---|---|
| 1 | [Usage guide](usage.md) | You are writing commands for the first time — start here. |
| 2 | [Integration](integration.md) | You are adding clipilot to another git project or build. |
| 3 | [Architecture](architecture.md) | You want the module map, the data flow of one keystroke, and the invariants the library relies on. |
| 4 | [API reference](api-reference.md) | You are writing application code against `cli.h`. |
| 5 | [Configuration](configuration.md) | You are tuning RAM/flash usage or disabling features. |
| 6 | [Command model and dispatch](dispatch.md) | You are designing a command tree and need the exact lookup and error rules. |
| 7 | [Tokenizer and argument conversion](parsing.md) | You need the precise quoting, escaping and number-literal grammar. |
| 8 | [Line editing and history](line-editing.md) | You are debugging terminal behaviour or adding a key binding. |
| 9 | [Tab completion](completion.md) | You want to know what is completed where, and why. |
| 10 | [Extending the library](extending.md) | You are adding a command, an argument type, or a key. |
| 11 | [Porting](porting.md) | You are wiring the CLI to a UART, USB CDC, RTT or a ring buffer. |
| 12 | [Testing](testing.md) | You are adding tests or diagnosing a failure. |

## Source layout

```
include/
  cli.h            public API: types, command tables, handlers, accessors
  cli_config.h     compile-time tuning macros
src/
  cli.c            lifecycle, I/O helpers, dispatch, argument accessors
  cli_parse.c      tokenizer, typed conversion, arity validation
  cli_edit.c       key handling, escape decoding, history ring
  cli_complete.c   TAB completion
  cli_builtin.c    help/usage rendering, `help` and `history` commands
  cli_internal.h   cross-file declarations (not public API)
tests/             host unit tests with a loopback terminal fixture
examples/host_demo interactive demo using raw-mode stdin
examples/pico2_usb_cdc  Raspberry Pi Pico 2 console over USB CDC
```

Contributor conventions live in
[`.github/instructions`](../.github/instructions) and the agent skills in
[`.github/skills`](../.github/skills).
