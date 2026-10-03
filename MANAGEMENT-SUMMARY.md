# clipilot — Management Summary

**Status:** complete and verified · **Date:** 2026-09-30 · **Version:** initial release

## What it is

clipilot is a reusable command line interface for microcontrollers such as the
STM32 family. It gives a device a serial console over an existing UART or USB
link, so that engineers can query state, change settings and trigger actions on
real hardware without a debugger attached and without building a custom tool
for each project.

## Why it matters

| Benefit | Detail |
|---|---|
| **Faster bring-up and diagnosis** | Field and lab staff can interrogate a board over a serial cable instead of rebuilding firmware to add a probe point. |
| **Reuse across products** | One library, dropped into any project; each product only declares its own command list. |
| **Lower defect risk** | Input validation lives in the library, so each new command is a few lines of application code with no hand-written parsing to get wrong. |
| **No hardware dependency** | The library needs two functions from the target — read a byte, write a byte — so it runs on any MCU, any transport, and on a PC for testing. |

## Deliverables

| Item | Size | Purpose |
|---|---|---|
| Library | 8 files, ~2 000 lines of C | the product |
| Automated tests | ~1 000 lines, 201 assertions | proof of correctness, run in seconds |
| Documentation | 17 documents, ~1 650 lines | user guide, internals reference, contributor rules |
| Demo application | 1 file | runs on a PC; lets a newcomer try the CLI with no hardware |

## Verification

All checks pass as of this commit.

* **201 automated assertions, 0 failures**, covering parsing, command dispatch,
  line editing, history and auto-completion.
* **Zero compiler warnings** under the strictest practical settings
  (`-Wall -Wextra -Werror -Wconversion`), for **five** different feature
  configurations, so optional features can be switched off safely.
* **End-to-end test** driving the real console code, which caught one display
  defect that the unit tests alone had missed.
* **Documentation claims verified against the running code**, not just against
  a reading of the source.

## Resource cost on target hardware

Measured, not estimated — Cortex-M4, size-optimised build:

| Configuration | Flash | RAM per console |
|---|---|---|
| All features enabled | ~7.4 kB | ~1.7 kB |
| Reduced feature set | ~5.9 kB | ~0.6 kB |

For context, a typical STM32 part carries 64–512 kB of flash, so the full
feature set costs in the order of **1–10 % of a small device's program
memory**, and less on larger ones. Nothing is allocated while running, which
removes a common class of long-term reliability failure in embedded systems.

## Key technical decisions

* **No dynamic memory.** All working memory is fixed at build time. This rules
  out memory fragmentation and out-of-memory faults during long deployments —
  a frequent cause of field failures that only appear after weeks of uptime.
* **Never blocks.** The console shares the processor cooperatively rather than
  holding it, so it can be added to existing firmware without disturbing
  timing-critical work.
* **Adjustable footprint.** Features such as floating-point arguments,
  formatted output and command history can each be switched off at build time
  for cost-sensitive parts.
* **Multiple consoles supported.** The same firmware can expose a console on a
  UART and on USB simultaneously.

## Risks and limitations

| Item | Assessment |
|---|---|
| Not yet run on physical hardware | All testing is on a PC. The hardware-specific layer is two functions, documented with worked STM32 examples; a bring-up on a real board is the recommended next step. |
| Text is ASCII only | Accented and non-Latin characters are not handled. Normal for a diagnostic console; a change would affect the editing logic. |
| Not thread-safe by itself | If used under an RTOS, access must be confined to one task or protected by a lock. This is documented. |
| No access control | Any command is available to anyone with a cable. If the console ships in a production unit, authentication or a build-time switch to exclude it should be added. |

## Suggested next steps

1. Validate on a physical STM32 board (expected to be a small effort — two
   functions to supply).
2. Decide the policy for production units: remove the console, or restrict it.
3. Adopt in a pilot project and gather feedback before wider rollout.

## Further reading

* [README](README.md) — getting started and the full feature list
* [doc/](doc/README.md) — technical reference
* [doc/porting.md](doc/porting.md) — what a hardware bring-up involves
