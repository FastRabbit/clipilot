---
name: clipilot-build-test
description: How to configure, build, test and verify the clipilot project on this machine, including the CMake/Cygwin toolchain pitfall, the feature-macro compile matrix and the test fixture patterns. Use before building or when a build or test command fails.
---

# Building and Testing clipilot

> See also [`doc/testing.md`](../../../doc/testing.md) for the test
> architecture, fixture patterns and known coverage gaps. This skill covers
> the machine-specific toolchain details.

## Toolchain pitfall — read this first

This machine has CMake and Ninja from the STM32CubeCLT install and GCC from
Cygwin. CMake's default compiler search resolves to `C:/DevProg/cygwin/bin/cc`,
which Ninja cannot launch:

```
CreateProcess failed. Command attempted:
"C:\DevProg\cygwin\bin\cc  -o CMakeFiles\cmTC_7a7f4.dir\testCCompiler.c.obj ..."
ninja: fatal: CreateProcess: Access is denied.
```

**Always name the compiler explicitly**, and delete a half-configured `build/`
before retrying — a failed configure leaves a cache that poisons the next run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug `
      -DCMAKE_C_COMPILER=C:/DevProg/cygwin/bin/gcc.exe
cmake --build build
```

The produced executables link against Cygwin DLLs, so **Cygwin must be on PATH
to run them**, including under `ctest`:

```powershell
$env:PATH = "C:\DevProg\cygwin\bin;$env:PATH"
ctest --test-dir build --output-on-failure
.\build\tests\clipilot_tests.exe
```

PowerShell here has no `&&`, `||` or `?.`. Chain with `;` and gate with
`if ($?) { ... }`.

## The verification loop

Run all three before declaring work done:

```powershell
# 1. build with warnings as errors (the library target uses -Werror)
cmake --build build

# 2. unit tests - exits non-zero on failure and prints "N checks, M failures"
.\build\tests\clipilot_tests.exe

# 3. end-to-end smoke test of the demo via piped stdin
"help`nled blink 250 4`ni2c read 0x48 4`nquit" | .\build\examples\host_demo\clipilot_demo.exe
```

The demo exits on stdin EOF (`stdin_eof` in the POSIX `term_getc`), which is
what makes step 3 usable non-interactively. Keep that behaviour when editing
the demo — it is the only test that exercises real terminal plumbing, and it is
what caught the `cli_set_prompt()` repaint bug.

## Feature-macro compile matrix

The library must compile clean with every feature switched off. The test binary
only covers the default configuration, so check the variants explicitly after
touching anything guarded by a `CLI_*` macro:

```powershell
$flags = '-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wundef ' +
         '-Wstrict-prototypes -Wmissing-prototypes -Iinclude -c'
$variants = @{
  'minimal' = '-DCLI_ENABLE_FLOAT=0 -DCLI_ENABLE_PRINTF=0 -DCLI_ENABLE_BUILTINS=0 -DCLI_HISTORY_DEPTH=0'
  'nohist'  = '-DCLI_HISTORY_DEPTH=0'
  'nofloat' = '-DCLI_ENABLE_FLOAT=0'
  'tiny'    = '-DCLI_MAX_LINE_LEN=32 -DCLI_MAX_TOKENS=4 -DCLI_MAX_ARGS=3 -DCLI_HISTORY_DEPTH=2'
}
foreach ($k in $variants.Keys) {
  foreach ($f in 'cli','cli_parse','cli_edit','cli_complete','cli_builtin') {
    gcc $flags.Split(' ') $variants[$k].Split(' ') "src\$f.c" -o "$env:TEMP\$k-$f.o"
    if ($LASTEXITCODE -ne 0) { Write-Host "FAIL $k/$f" }
  }
}
```

Put scratch output in `$env:TEMP`, not in the repo.

## Checking the RAM footprint

`sizeof(cli_t)` is the headline number for an MCU user and is quoted in the
README. Re-measure and update the README when buffer fields change:

```powershell
'#include <stdio.h>
#include "cli.h"
int main(void){ printf("%u\n",(unsigned)sizeof(cli_t)); return 0; }' |
  Out-File -Encoding ascii $env:TEMP\size.c
gcc -std=c11 -Iinclude $env:TEMP\size.c src/cli.c src/cli_parse.c `
    src/cli_edit.c src/cli_complete.c src/cli_builtin.c -o $env:TEMP\size.exe
& $env:TEMP\size.exe
```

Note that PowerShell does **not** expand wildcards for native commands, so
`src\*.c` reaches gcc verbatim and fails — list the sources explicitly.

Reference values: 1716 bytes with the defaults, 648 bytes with
`CLI_MAX_LINE_LEN=64 CLI_HISTORY_DEPTH=4 CLI_MAX_TOKENS=8 CLI_MAX_ARGS=6
CLI_ENABLE_FLOAT=0` (64-bit host).

## Cross-compiling for ARM

Two toolchains are installed; the second supports Cortex-M33 and is the one to
prefer:

```
C:\ST\STM32CubeCLT_1.19.0\GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe
C:\DevProg\arm\armgcc-14_2_rel1\bin\arm-none-eabi-gcc.exe      (14.2.1)
```

Use these to check target-specific warnings (`-Wconversion` behaves differently
where `int` is 32-bit) and to measure flash. `arm-none-eabi-size -t *.o` on the
five library objects gives the `.text` total; `nm -S` on a `char probe[sizeof
(cli_t)];` translation unit gives the RAM figure without needing to link or run.

Measured `-Os` over the five library objects. Treat the byte counts as
**toolchain-specific**: the same sources give 7 572 B under the CubeCLT gcc
13.3.1 and 7 552 B under armgcc 14.2.1, and `-ffunction-sections` moves it
again. Always record the compiler and flags next to any figure you publish, or
it cannot be reproduced.

| Target | Config | Flash | `cli_t` |
|---|---|---|---|
| Cortex-M4 | all features | ~7.5 kB | 1 716 B |
| Cortex-M4 | float, printf, builtins and history off | ~6.0 kB | 648 B |
| Cortex-M33 | the Pico 2 example's tuning | 7 164 B | 648 B |

The M33 row reproduces exactly because the example's README pins the toolchain
(armgcc 14.2.1, `-mcpu=cortex-m33 -mthumb -Os -ffunction-sections`).

`sizeof(cli_t)` is **identical on 32-bit ARM and the 64-bit host** — 1716 with
the defaults, 648 tuned — so the cheap host measurement is valid for a target
datasheet. That is a useful cross-check that the size knobs really reached the
library.

## The two examples

`examples/host_demo` is part of the top-level build. **`examples/pico2_usb_cdc`
is not**, deliberately: it is a standalone Pico SDK project that would break the
host build. It is also not buildable on this machine — there is no Pico SDK
here.

To verify it anyway, stub the SDK. Compile-checking catches warnings:

```powershell
# headers declaring only what main.c uses, under $env:TEMP\picostub
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -Os -std=c11 `
    -Wall -Wextra -Werror -Wconversion `
    -Iinclude "-I$env:TEMP\picostub" -c examples\pico2_usb_cdc\main.c -o ...
```

Better, give the stubs *bodies* and link the example against the real library
on the host. `getchar_timeout_us` reads stdin and `exit(0)`s at EOF, `gpio_put`
writes a `bool pins[48]`, `to_ms_since_boot` returns a counter. The example's
own `main()` then runs, so scripted input exercises the real command table:

```powershell
"help`nled on led1`nsys info`n" | & $env:TEMP\picohost\picodemo.exe
```

That harness is how every transcript in the example's README was captured, and
it found a genuine bug (`sys reboot bootsel` rejected, because the argument was
declared `CLI_ARG_BOOL`). Delete the stubs afterwards — they belong in
`$env:TEMP`, never in the repo.

Also worth checking that the example only depends on symbols that exist:
`nm -u` the example object and confirm every `cli_*` undefined symbol appears
in `nm --defined-only` over the library objects. The remainder should be
recognisably SDK calls.

## Verifying integration instructions

Claims about how a *consumer* consumes clipilot are easy to get wrong and
cannot be checked by reading. Build the scenario in `$env:TEMP`: a throwaway
repo, clipilot added as a real submodule
(`git -c protocol.file.allow=always submodule add E:/clipilot.bare ...`), a
`main.c` that prints `sizeof(cli_t)`, and a CMakeLists following the documented
recipe.

Two things to assert: the consumer's `sizeof(cli_t)` matches the library's, and
the `-DCLI_*` flags appear on clipilot's own object compiles in `build.ninja`.
This is what proves `target_compile_definitions(clipilot PUBLIC ...)` is
required rather than merely recommended.

## Writing tests

Tests live in `tests/` and use a tiny harness (`test_util.h`) rather than an
external framework. A `fixture_t` is a loopback terminal: scripted bytes go in
through the real `cli_getc_fn`, output is captured through `cli_putc_fn`.

Pick the right entry point:

| Goal | Setup | Drive with |
|---|---|---|
| Tokenizer / conversion / dispatch | `fx_init(&fx, cmds, count, false)` — echo **off** | `cli_exec(&fx.cli, "led on")` |
| Editing, history, completion | `fx_init(&fx, cmds, count, true)` — echo **on** | `fx_type(&fx, "led o\t")` |

Assertion helpers: `CHECK`, `CHECK_STREQ`, `CHECK_HAS`, `CHECK_HASNT`. Register
a test with `RUN(t_name)` inside its suite function, and the suite in
`test_main.c`.

Inspect output with `fx_plain(&fx)`, which strips ANSI escapes and `\r`, when
asserting on *text*. Use `fx_out(&fx)` only when the escape bytes themselves are
the subject (for example checking that a bell `\a` was emitted). Call
`fx_clear(&fx)` to drop the prompt and prior output before the interesting step.

Keystrokes are plain byte strings — define them as macros for readability:

```c
#define KEY_UP   "\x1b[A"
#define KEY_HOME "\x1b[H"
#define KEY_DEL  "\x1b[3~"

fx_type(&fx, "abcd" KEY_HOME KEY_DEL);
CHECK_STREQ(fx.cli.line, "bcd");
```

## Learnings

* **Suspect the test before the code when an assertion is about completion or
  handler identity.** Both initial test failures in this project were wrong
  expectations, not library bugs: `flag`/`fail` share only the prefix `f` (not
  `fa`), and a subcommand handler logs its full path `"sys stats"` rather than
  the leaf word. Re-derive the expected value by hand before changing `src/`.
* **`fx_type()` must reset the input buffer once consumed.** The fixture's input
  array is fixed size and would otherwise overflow across many `fx_type()` calls
  in one test. The reset happens when `in_pos >= in_len`; keep it if the
  fixture is reworked.
* **A clean unit-test run is not sufficient evidence.** The mid-command repaint
  bug passed all 201 assertions and only showed up in the piped demo run, since
  no unit test called `cli_set_prompt()` from inside a handler. Always finish
  with the end-to-end smoke test.
* **Never hand-write a console transcript into documentation — capture it.**
  Every hand-written transcript drafted in this project was wrong, often in
  several places at once: invented `help` layouts, wrong argument spellings,
  wrong defaults, and column alignment that the code does not actually produce.
  Pipe a script into the demo (or a stubbed example) and paste the real output.
  Twice this turned a documentation error into a discovered code bug.
* **When output and documentation disagree, decide which one is wrong.** For
  `sys reboot bootsel` the *code* was wrong — the natural spelling was better
  than the `CLI_ARG_BOOL` the handler declared — so the fix was an enum
  argument, not a reworded README. Changing the docs to match awkward behaviour
  is the lazy direction.
* **Check markdown links after any docs change**, including in-page `#anchors`,
  by globbing `*.md`, resolving relative targets and slugifying every heading.
  A cross-reference added in one commit broke a heading renamed in another.
