/**
 * @file main.c
 * @brief clipilot on a Raspberry Pi Pico 2 (RP2350, Cortex-M33) over USB CDC.
 *
 * Two LEDs are driven from the console:
 *   LED1 -> GPIO15
 *   LED2 -> GPIO14
 *
 * Connect with any serial terminal on the USB CDC port (the baud rate is
 * irrelevant for CDC) and try:
 *   help
 *   led on led1
 *   led blink led2 100 20
 *   led status
 *   gpio set 16 on
 *   sys info
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "cli.h"

/* ------------------------------------------------------------------ */
/* Board definition                                                    */
/* ------------------------------------------------------------------ */

#define LED1_PIN 15u
#define LED2_PIN 14u

#define LED_COUNT 2u

/** Blink bookkeeping for one LED. Driven from the main loop, not a handler. */
typedef struct {
    uint32_t pin;
    bool on;
    uint32_t period_ms;  /**< 0 when not blinking */
    int32_t remaining;   /**< toggles left, negative = forever */
    uint32_t next_ms;
} led_t;

typedef struct {
    led_t led[LED_COUNT];
    bool usb_was_connected;
} board_t;

static const char *const led_names[] = { "led1", "led2", "all", NULL };

/* ------------------------------------------------------------------ */
/* LED helpers                                                         */
/* ------------------------------------------------------------------ */

static void led_apply(led_t *led, bool on) {
    led->on = on;
    gpio_put(led->pin, on);
}

static void led_stop_blink(led_t *led) {
    led->period_ms = 0u;
    led->remaining = 0;
}

/**
 * @brief Advances the software blink for one LED.
 *
 * Called from the main loop so that a blinking LED never blocks the console.
 */
static void led_tick(led_t *led, uint32_t now_ms) {
    if (led->period_ms == 0u) {
        return;
    }
    if ((int32_t)(now_ms - led->next_ms) < 0) {
        return;
    }
    led_apply(led, !led->on);
    led->next_ms = now_ms + led->period_ms;

    if (led->remaining > 0) {
        led->remaining--;
        if (led->remaining == 0) {
            led_stop_blink(led);
            led_apply(led, false);
        }
    }
}

static void led_fn_on(led_t *led) {
    led_stop_blink(led);
    led_apply(led, true);
}

static void led_fn_off(led_t *led) {
    led_stop_blink(led);
    led_apply(led, false);
}

static void led_fn_toggle(led_t *led) {
    led_stop_blink(led);
    led_apply(led, !led->on);
}

/** Runs @p fn over the LEDs selected by an enum argument (0, 1 or 2 = all). */
static void led_for_each(board_t *board, int which, void (*fn)(led_t *)) {
    uint32_t i;

    for (i = 0u; i < LED_COUNT; i++) {
        if ((which == 2) || ((uint32_t)which == i)) {
            fn(&board->led[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Console I/O over USB CDC                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Non-blocking character source.
 *
 * A zero timeout makes this return PICO_ERROR_TIMEOUT (a negative value) when
 * the host has sent nothing, which is exactly what cli_poll() expects.
 */
static int cdc_getc(void *ctx) {
    (void)ctx;
    return getchar_timeout_us(0);
}

/**
 * @brief Character sink.
 *
 * putchar_raw() bypasses the SDK's own newline translation. Plain putchar()
 * would expand '\n' as well and, combined with cli_config_t::crlf, produce a
 * doubled carriage return.
 */
static void cdc_putc(void *ctx, char c) {
    (void)ctx;
    (void)putchar_raw(c);
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static int cmd_led_on(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *b = (board_t *)user;
    int which = cli_arg_enum(args, 0u, 2);

    led_for_each(b, which, led_fn_on);
    cli_printf(cli, "%s on\n", led_names[which]);
    return 0;
}

static int cmd_led_off(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *b = (board_t *)user;
    int which = cli_arg_enum(args, 0u, 2);

    led_for_each(b, which, led_fn_off);
    cli_printf(cli, "%s off\n", led_names[which]);
    return 0;
}

static int cmd_led_toggle(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *b = (board_t *)user;
    int which = cli_arg_enum(args, 0u, 2);

    led_for_each(b, which, led_fn_toggle);
    cli_printf(cli, "%s toggled\n", led_names[which]);
    return 0;
}

/**
 * Starts a blink and returns immediately; the toggling happens in the main
 * loop. A handler that slept here would freeze the console for the duration.
 */
static int cmd_led_blink(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *b = (board_t *)user;
    int which = cli_arg_enum(args, 0u, 2);
    uint32_t period = cli_arg_uint(args, 1u, 250u);
    int32_t count = cli_arg_int(args, 2u, -1);
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t i;

    if (period == 0u) {
        cli_write(cli, "period must be greater than zero\n");
        return 1;
    }

    for (i = 0u; i < LED_COUNT; i++) {
        led_t *led = &b->led[i];

        if ((which != 2) && ((uint32_t)which != i)) {
            continue;
        }
        led->period_ms = period;
        led->remaining = (count > 0) ? (count * 2) : -1;
        led->next_ms = now + period;
        led_apply(led, true);
    }

    if (count > 0) {
        cli_printf(cli, "%s blinking %ld times every %lu ms\n",
                   led_names[which], (long)count, (unsigned long)period);
    } else {
        cli_printf(cli, "%s blinking every %lu ms\n",
                   led_names[which], (unsigned long)period);
    }
    return 0;
}

static int cmd_led_status(cli_t *cli, const cli_args_t *args, void *user) {
    board_t *b = (board_t *)user;
    uint32_t i;

    (void)args;
    for (i = 0u; i < LED_COUNT; i++) {
        const led_t *led = &b->led[i];

        cli_printf(cli, "%s  GPIO%-2lu  %-3s", led_names[i],
                   (unsigned long)led->pin, led->on ? "on" : "off");
        if (led->period_ms != 0u) {
            cli_printf(cli, "  blinking %lu ms", (unsigned long)led->period_ms);
        }
        cli_write(cli, "\n");
    }
    return 0;
}

static int cmd_gpio_get(cli_t *cli, const cli_args_t *args, void *user) {
    uint32_t pin = cli_arg_uint(args, 0u, 0u);

    (void)user;
    if (pin > 47u) {
        cli_write(cli, "pin out of range (0-47)\n");
        return 1;
    }
    cli_printf(cli, "GPIO%lu = %d\n", (unsigned long)pin, gpio_get(pin) ? 1 : 0);
    return 0;
}

static int cmd_gpio_set(cli_t *cli, const cli_args_t *args, void *user) {
    uint32_t pin = cli_arg_uint(args, 0u, 0u);
    bool level = cli_arg_bool(args, 1u, false);

    (void)user;
    if (pin > 47u) {
        cli_write(cli, "pin out of range (0-47)\n");
        return 1;
    }
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, level);
    cli_printf(cli, "GPIO%lu = %d\n", (unsigned long)pin, level ? 1 : 0);
    return 0;
}

static int cmd_sys_info(cli_t *cli, const cli_args_t *args, void *user) {
    (void)args;
    (void)user;
    cli_write(cli, "board   : Raspberry Pi Pico 2 (RP2350, Cortex-M33)\n");
    cli_write(cli, "console : USB CDC\n");
    cli_printf(cli, "uptime  : %lu ms\n",
               (unsigned long)to_ms_since_boot(get_absolute_time()));
    cli_printf(cli, "cli_t   : %u bytes\n", (unsigned)sizeof(cli_t));
    return 0;
}

static int cmd_sys_reboot(cli_t *cli, const cli_args_t *args, void *user) {
    bool to_bootsel = (cli_arg_enum(args, 0u, -1) == 0);

    (void)user;
    if (to_bootsel) {
        cli_write(cli, "rebooting into BOOTSEL\n");
        sleep_ms(50);                 /* let the reply reach the host */
        reset_usb_boot(0u, 0u);
    }
    cli_write(cli, "rebooting\n");
    sleep_ms(50);
    watchdog_reboot(0u, 0u, 0u);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Command table                                                       */
/* ------------------------------------------------------------------ */

static const cli_arg_spec_t which_args[] = {
    { "led", CLI_ARG_ENUM, true, false, led_names, "which LED (default all)" },
};

static const cli_arg_spec_t blink_args[] = {
    { "led",    CLI_ARG_ENUM, true, false, led_names, "which LED (default all)" },
    { "period", CLI_ARG_UINT, true, false, NULL, "half period in ms (default 250)" },
    { "count",  CLI_ARG_INT,  true, false, NULL, "blinks, omit for forever" },
};

static const cli_arg_spec_t gpio_get_args[] = {
    { "pin", CLI_ARG_UINT, false, false, NULL, "GPIO number" },
};

static const cli_arg_spec_t gpio_set_args[] = {
    { "pin",   CLI_ARG_UINT, false, false, NULL, "GPIO number" },
    { "level", CLI_ARG_BOOL, false, false, NULL, "on/off, 1/0, yes/no" },
};

static const char *const reboot_targets[] = { "bootsel", NULL };

static const cli_arg_spec_t reboot_args[] = {
    { "target", CLI_ARG_ENUM, true, false, reboot_targets,
      "reboot into the USB bootloader" },
};

static const cli_cmd_t led_subs[] = {
    { "on",     "switch on",  NULL, 0, which_args, 1, cmd_led_on },
    { "off",    "switch off", NULL, 0, which_args, 1, cmd_led_off },
    { "toggle", "invert",     NULL, 0, which_args, 1, cmd_led_toggle },
    { "blink",  "blink in the background", NULL, 0, blink_args, 3, cmd_led_blink },
    { "status", "show LED state", NULL, 0, NULL, 0, cmd_led_status },
};

static const cli_cmd_t gpio_subs[] = {
    { "get", "read a pin",  NULL, 0, gpio_get_args, 1, cmd_gpio_get },
    { "set", "drive a pin", NULL, 0, gpio_set_args, 2, cmd_gpio_set },
};

static const cli_cmd_t sys_subs[] = {
    { "info",   "board and console information", NULL, 0, NULL, 0, cmd_sys_info },
    { "reboot", "restart the MCU", NULL, 0, reboot_args, 1, cmd_sys_reboot },
};

static const cli_cmd_t commands[] = {
    { "led",  "LED control",     led_subs,  5, NULL, 0, NULL },
    { "gpio", "raw GPIO access", gpio_subs, 2, NULL, 0, NULL },
    { "sys",  "system commands", sys_subs,  2, NULL, 0, NULL },
};

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static board_t board;
static cli_t cli;

static void board_init(void) {
    uint32_t i;

    board.led[0].pin = LED1_PIN;
    board.led[1].pin = LED2_PIN;

    for (i = 0u; i < LED_COUNT; i++) {
        gpio_init(board.led[i].pin);
        gpio_set_dir(board.led[i].pin, GPIO_OUT);
        gpio_put(board.led[i].pin, false);
    }
}

static void console_init(void) {
    cli_config_t cfg = cli_config_default();

    cfg.commands      = commands;
    cfg.command_count = sizeof(commands) / sizeof(commands[0]);
    cfg.getc_fn       = cdc_getc;
    cfg.putc_fn       = cdc_putc;
    cfg.user          = &board;
    cfg.prompt        = "pico2> ";
    cfg.echo          = true;
    cfg.crlf          = true;

    cli_init(&cli, &cfg);
}

/** Writes a banner before the CLI exists, so it lands above the first prompt. */
static void cdc_banner(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            (void)putchar_raw('\r');
        }
        (void)putchar_raw(*s);
        s++;
    }
}

int main(void) {
    stdio_init_all();
    board_init();

    /*
     * Anything written before the host opens the CDC port is discarded, so
     * the console is (re-)initialised on each connect. That also gives a
     * terminal attached long after boot a banner and a prompt.
     */
    for (;;) {
        bool connected = stdio_usb_connected();
        uint32_t now;
        uint32_t i;

        if (connected && !board.usb_was_connected) {
            /* The banner goes first: cli_init() prints the prompt. */
            cdc_banner("\nclipilot on Pico 2 - type 'help'\n");
            console_init();
        }
        board.usb_was_connected = connected;

        if (connected) {
            cli_poll(&cli);
        }

        now = to_ms_since_boot(get_absolute_time());
        for (i = 0u; i < LED_COUNT; i++) {
            led_tick(&board.led[i], now);
        }
    }
}
