# Testing

The tests are plain C with a ~100-line harness in
[`tests/test_util.h`](../tests/test_util.h) — no external framework, so they
build anywhere the library does.

## Layout

| File | Contents |
|---|---|
| `tests/test_main.c` | entry point; runs each suite and prints the totals |
| `tests/test_util.{h,c}` | assertion macros and the loopback terminal fixture |
| `tests/test_cmds.{h,c}` | the shared command tree the suites exercise |
| `tests/test_parse.c` | tokenizer, quoting, escapes, number and enum conversion |
| `tests/test_dispatch.c` | lookup, subcommand walk, arity and error messages |
| `tests/test_edit.c` | cursor movement, kills, history, escape sequences |
| `tests/test_complete.c` | TAB behaviour for commands, subcommands and enums |

The binary exits non-zero on any failure and prints `N checks, M failures`.

## Running

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug `
      -DCMAKE_C_COMPILER=C:/DevProg/cygwin/bin/gcc.exe
cmake --build build
.\build\tests\clipilot_tests.exe
```

Naming the compiler explicitly matters on this machine: CMake's default search
picks `cc`, which Ninja cannot launch. The binaries link Cygwin DLLs, so
`C:\DevProg\cygwin\bin` must be on `PATH` to run them — including under
`ctest --test-dir build --output-on-failure`. Full details and the recovery
steps are in
[`.github/skills/clipilot-build-test`](../.github/skills/clipilot-build-test/SKILL.md).

## The fixture

`fixture_t` is a loopback terminal wrapped around a real `cli_t`: scripted
bytes are handed out by a real `cli_getc_fn`, and output is captured by a real
`cli_putc_fn`. Nothing is mocked — the tests drive the same code path as a
UART.

| Goal | Init | Drive with |
|---|---|---|
| tokenizer, conversion, dispatch | `fx_init(&fx, cmds, n, false)` — echo **off** | `cli_exec(&fx.cli, "led on")` |
| editing, history, completion | `fx_init(&fx, cmds, n, true)` — echo **on** | `fx_type(&fx, "led o\t")` |

Echo off keeps the captured output free of escape sequences, so assertions read
cleanly. Echo on is required whenever the terminal output *is* the subject.

Two views of the captured bytes:

* `fx_plain(&fx)` — ANSI escapes and `\r` stripped. Use for text assertions.
* `fx_out(&fx)` — raw. Use only when the escape bytes themselves matter, such
  as checking that a bell (`\a`) was emitted.

`fx_clear(&fx)` discards everything captured so far — call it after setup so
the prompt and earlier output do not pollute a `CHECK_HAS`.

## Assertions

`CHECK(cond)`, `CHECK_STREQ(actual, expected)`, `CHECK_HAS(haystack, needle)`
and `CHECK_HASNT(haystack, needle)`. Each bumps a global counter and reports
file, line and the offending value; none abort, so one run reports every
failure.

## Adding a test

```c
static fixture_t fx;

static void setup(void) {
    size_t count = 0u;
    const cli_cmd_t *cmds = test_commands(&count);

    fx_init(&fx, cmds, count, true);   /* echo on */
    test_log_reset();
}

static void t_home_then_delete(void) {
    setup();
    fx_clear(&fx);

    fx_type(&fx, "abcd" KEY_HOME KEY_DEL);
    CHECK_STREQ(fx.cli.line, "bcd");
}

void suite_edit(void) {
    RUN(t_home_then_delete);
}
```

`test_commands(&count)` returns the shared tree and its size;
`test_log_reset()` clears `g_log`, which records the last handler invocation so
a dispatch test can assert *which* handler ran and with what arguments.

Register the function with `RUN()` inside its suite, and the suite in
`test_main.c`. Keystrokes are plain byte strings; define readable macros
(`#define KEY_UP "\x1b[A"`) rather than inlining escapes.

## What the tests do not cover

* **Only the default configuration is compiled.** The `#if` branches for
  `CLI_ENABLE_FLOAT=0`, `CLI_ENABLE_PRINTF=0`, `CLI_ENABLE_BUILTINS=0` and
  `CLI_HISTORY_DEPTH=0` are never seen by the test binary. Compile the matrix
  in the build/test skill after touching anything guarded by a `CLI_*` macro.
* **Real terminal plumbing.** The end-to-end check is the demo driven by piped
  stdin:

  ```powershell
  "help`nled blink 250 4`nquit" | .\build\examples\host_demo\clipilot_demo.exe
  ```

  The demo exits on stdin EOF, which is what makes this usable
  non-interactively — preserve that behaviour when editing it.

  This is not redundant with the unit tests. The `cli_set_prompt()` mid-handler
  repaint bug passed all 201 assertions and only surfaced here, because no unit
  test changed the prompt from inside a handler.

* **Target hardware.** Nothing exercises a real UART; `getc_fn`/`putc_fn` are
  the porting boundary and are assumed correct.

## When a test fails

Suspect the test before the library, especially for completion and handler
identity — the two failures during initial development were both wrong
expectations. `flag` and `fail` share only the prefix `f`, not `fa`, and a
subcommand handler logs its full path (`"sys stats"`), not the leaf word.
Re-derive the expected value by hand before editing `src/`.
