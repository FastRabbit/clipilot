# Porting

The library needs exactly two functions from the target: a **non-blocking**
character source and a character sink.

```c
int  (*cli_getc_fn)(void *io_ctx);   /* next byte, or <0 when none pending */
void (*cli_putc_fn)(void *io_ctx, char c);
```

Everything else is portable C11. Add the five sources and the include
directory:

```cmake
target_sources(my_firmware PRIVATE
    clipilot/src/cli.c
    clipilot/src/cli_parse.c
    clipilot/src/cli_edit.c
    clipilot/src/cli_complete.c
    clipilot/src/cli_builtin.c
)
target_include_directories(my_firmware PRIVATE clipilot/include)
```

## The one rule

**`getc_fn` must never block.** Returning a negative value when no byte is
available is how `cli_poll()` knows to stop. A blocking read turns the CLI into
a foreground task and stalls the rest of the main loop.

## STM32 HAL, polled UART

The simplest correct port — a zero timeout makes `HAL_UART_Receive()`
non-blocking:

```c
static int uart_getc(void *ctx) {
    uint8_t byte;

    (void)ctx;
    if (HAL_UART_Receive(&huart2, &byte, 1, 0) != HAL_OK) {
        return -1;                    /* nothing pending */
    }
    return (int)byte;
}

static void uart_putc(void *ctx, char c) {
    (void)ctx;
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)&c, 1, HAL_MAX_DELAY);
}
```

This drops bytes on overrun if the main loop is slow, and `uart_putc()` busy
waits per character. It is fine for bring-up; prefer a ring buffer in
production.

## Interrupt or DMA ring buffer

The recommended shape. The ISR only fills a ring; the CLI drains it from the
main loop, so handlers run in thread context:

```c
static int ring_getc(void *ctx) {
    ring_t *r = (ring_t *)ctx;

    return ring_empty(r) ? -1 : (int)ring_pop(r);
}

/* cfg.io_ctx = &rx_ring; */
```

If instead you push bytes straight from the ISR with `cli_feed()`, command
**handlers execute inside the interrupt**. That is rarely what you want: a
handler that reads a sensor or erases flash will blow the interrupt latency
budget. Use it only for trivially short handlers.

`cli_feed_buf()` is convenient from a DMA half-transfer callback, with the same
caveat.

## USB CDC

A CDC port behaves like a UART with one extra concern: the SDK's own `stdout`
wrapper may already translate newlines. Use the raw byte writer and leave the
translation to `cfg.crlf`, otherwise every line ends with two carriage returns.

```c
static int cdc_getc(void *ctx) {
    (void)ctx;
    return getchar_timeout_us(0);   /* negative when idle */
}

static void cdc_putc(void *ctx, char c) {
    (void)ctx;
    (void)putchar_raw(c);           /* not putchar(): no double \r */
}
```

Output written before the host opens the port is discarded, so re-send the
banner when the link comes up rather than only at boot.

[`examples/pico2_usb_cdc`](../examples/pico2_usb_cdc) is a complete worked
example on a Raspberry Pi Pico 2 (Cortex-M33).

## Multiple interfaces

Because there are no globals, a second instance is just a second `cli_t`:

```c
static cli_t uart_cli;
static cli_t usb_cli;
/* same command table, different io_ctx */
```

Both may share the `static const` command table. The `cfg.user` pointer can
differ per instance if the commands need to know which link they came from.

## Terminal settings

* `crlf = true` (the default) suits a terminal emulator over a serial link.
  Turn it off when the transport already translates, or when the peer is a
  script.
* `echo = false` disables all echo and escape output, for a dumb terminal or a
  machine-to-machine protocol. Line editing still works internally; the user
  just sees nothing until the command output appears.
* Arrow keys and `Home`/`End` require the terminal to send ANSI/VT sequences.
  PuTTY, minicom, screen, and the Windows Terminal all do by default.
* The CLI assumes a terminal at least `CLI_MAX_LINE_LEN` columns wide when
  repainting; see [line editing](line-editing.md#screen-updates).

## RTOS use

`cli_t` is **not** internally synchronised. Either confine one instance to one
task, or wrap `cli_poll()` and any out-of-band `cli_write()` in the same mutex.
The common mistake is a logging task writing to the same instance a CLI task is
polling: the output interleaves mid-escape-sequence and the display corrupts.

## Freestanding builds

The library includes `<string.h>`, `<stdarg.h>`, `<stdbool.h>`, `<stddef.h>`
and `<stdint.h>`, plus `<stdio.h>` only when `CLI_ENABLE_PRINTF` is 1. Setting
`CLI_ENABLE_PRINTF=0` removes the only dependency on a hosted-style libc; the
remaining string functions are `strlen`, `strcmp`, `strncpy`, `strcat`,
`memcpy`, `memmove` and `memset`, all available in newlib-nano and in
`-ffreestanding` builds.

See [configuration](configuration.md) for the full list of size switches.
