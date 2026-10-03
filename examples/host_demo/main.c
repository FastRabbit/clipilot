/**
 * @file main.c
 * @brief Host demo for clipilot: raw-mode console driving the CLI.
 *
 * Build and run, then try:
 *   help
 *   led blink 250 4
 *   i2c read 0x48 8
 *   set mode turbo
 *   echo hello "quoted world"
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Terminal plumbing                                                   */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

static DWORD saved_out_mode;
static HANDLE out_handle;

static void term_raw(void) {
    out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(out_handle, &saved_out_mode)) {
        (void)SetConsoleMode(out_handle,
                             saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

static void term_restore(void) {
    if (out_handle != NULL) {
        (void)SetConsoleMode(out_handle, saved_out_mode);
    }
}

/** Translates the _getch() extended key encoding into ANSI escape sequences. */
static int term_getc(void *ctx) {
    static const char *pending;
    int c;

    (void)ctx;

    if ((pending != NULL) && (*pending != '\0')) {
        c = (unsigned char)*pending;
        pending++;
        return c;
    }
    if (!_kbhit()) {
        return -1;
    }
    c = _getch();
    if ((c == 0) || (c == 0xE0)) {
        int ext = _getch();

        switch (ext) {
        case 'H': pending = "[A"; break;
        case 'P': pending = "[B"; break;
        case 'M': pending = "[C"; break;
        case 'K': pending = "[D"; break;
        case 'G': pending = "[H"; break;
        case 'O': pending = "[F"; break;
        case 'S': pending = "[3~"; break;
        default:  return -1;
        }
        return 0x1B;
    }
    return c;
}

static bool term_eof(void) {
    return false;
}

#else /* POSIX */

static struct termios saved_termios;
static bool termios_saved;
static bool stdin_eof;

static void term_raw(void) {
    struct termios raw;
    int flags;

    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return;
    }
    termios_saved = true;
    raw = saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

static void term_restore(void) {
    if (termios_saved) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    }
}

static int term_getc(void *ctx) {
    unsigned char c;
    ssize_t n;

    (void)ctx;
    n = read(STDIN_FILENO, &c, 1u);
    if (n == 0) {
        stdin_eof = true; /* piped input exhausted */
        return -1;
    }
    if (n != 1) {
        return -1;
    }
    return (int)c;
}

static bool term_eof(void) {
    return stdin_eof;
}

#endif

static void term_putc(void *ctx, char c) {
    (void)ctx;
    (void)fputc(c, stdout);
    (void)fflush(stdout);
}

/** Yields the CPU between polls so the demo does not spin at 100%. */
static void term_idle(void) {
#ifdef _WIN32
    Sleep(2);
#else
    (void)usleep(2000);
#endif
}

/* ------------------------------------------------------------------ */
/* Demo application state and commands                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    bool running;
    bool led;
    uint32_t blink_ms;
    char name[24];
} app_t;

static int cmd_led_on(cli_t *cli, const cli_args_t *args, void *user) {
    app_t *app = (app_t *)user;

    (void)args;
    app->led = true;
    cli_write(cli, "LED on\n");
    return 0;
}

static int cmd_led_off(cli_t *cli, const cli_args_t *args, void *user) {
    app_t *app = (app_t *)user;

    (void)args;
    app->led = false;
    cli_write(cli, "LED off\n");
    return 0;
}

static int cmd_led_blink(cli_t *cli, const cli_args_t *args, void *user) {
    app_t *app = (app_t *)user;
    uint32_t period = cli_arg_uint(args, 0u, 500u);
    int32_t count = cli_arg_int(args, 1u, 1);

    app->blink_ms = period;
    cli_printf(cli, "blinking %d times every %u ms\n", (int)count, (unsigned)period);
    return 0;
}

static int cmd_led_status(cli_t *cli, const cli_args_t *args, void *user) {
    app_t *app = (app_t *)user;

    (void)args;
    cli_write(cli, "LED is ");
    cli_write(cli, app->led ? "on" : "off");
    cli_write(cli, "\n");
    return 0;
}

static int cmd_i2c_read(cli_t *cli, const cli_args_t *args, void *user) {
    uint32_t addr = cli_arg_uint(args, 0u, 0u);
    uint32_t len = cli_arg_uint(args, 1u, 0u);
    uint32_t i;

    (void)user;
    cli_printf(cli, "read %u byte(s) from 0x%02X:", (unsigned)len, (unsigned)addr);
    for (i = 0u; i < len; i++) {
        cli_printf(cli, " %02X", (unsigned)((addr + i) & 0xFFu));
    }
    cli_write(cli, "\n");
    return 0;
}

static int cmd_i2c_write(cli_t *cli, const cli_args_t *args, void *user) {
    uint32_t addr = cli_arg_uint(args, 0u, 0u);
    size_t i;

    (void)user;
    cli_printf(cli, "write to 0x%02X:", (unsigned)addr);
    for (i = 1u; i < args->count; i++) {
        cli_printf(cli, " %02X", (unsigned)cli_arg_uint(args, i, 0u));
    }
    cli_write(cli, "\n");
    return 0;
}

static int cmd_echo(cli_t *cli, const cli_args_t *args, void *user) {
    size_t i;

    (void)user;
    for (i = 0u; i < args->count; i++) {
        if (i != 0u) {
            cli_write(cli, " ");
        }
        cli_write(cli, cli_arg_str(args, i, ""));
    }
    cli_write(cli, "\n");
    return 0;
}

static int cmd_set(cli_t *cli, const cli_args_t *args, void *user) {
    app_t *app = (app_t *)user;
    int key = cli_arg_enum(args, 0u, -1);
    const char *value = cli_arg_str(args, 1u, "");

    switch (key) {
    case 0: /* name */
        strncpy(app->name, value, sizeof(app->name) - 1u);
        app->name[sizeof(app->name) - 1u] = '\0';
        cli_printf(cli, "name = %s\n", app->name);
        break;
    case 1: /* prompt */
        cli_set_prompt(cli, value);
        break;
    default:
        cli_write(cli, "unhandled key\n");
        return 1;
    }
    return 0;
}

static int cmd_quit(cli_t *cli, const cli_args_t *args, void *user) {
    app_t *app = (app_t *)user;

    (void)args;
    cli_write(cli, "bye\n");
    app->running = false;
    return 0;
}

static const cli_arg_spec_t blink_args[] = {
    { "period", CLI_ARG_UINT, false, false, NULL, "period in milliseconds" },
    { "count",  CLI_ARG_INT,  true,  false, NULL, "number of blinks (default 1)" },
};

static const cli_arg_spec_t i2c_read_args[] = {
    { "addr", CLI_ARG_HEX,  false, false, NULL, "device address, e.g. 0x48" },
    { "len",  CLI_ARG_UINT, false, false, NULL, "number of bytes to read" },
};

static const cli_arg_spec_t i2c_write_args[] = {
    { "addr", CLI_ARG_HEX, false, false, NULL, "device address" },
    { "byte", CLI_ARG_HEX, false, true,  NULL, "one or more bytes" },
};

static const cli_arg_spec_t echo_args[] = {
    { "text", CLI_ARG_STR, false, true, NULL, "words to print" },
};

static const char *const set_keys[] = { "name", "prompt", NULL };

static const cli_arg_spec_t set_args[] = {
    { "key",   CLI_ARG_ENUM, false, false, set_keys, "setting to change" },
    { "value", CLI_ARG_STR,  false, false, NULL,     "new value" },
};

static const cli_cmd_t led_subs[] = {
    { "on",     "switch the LED on",   NULL, 0u, NULL, 0u, cmd_led_on },
    { "off",    "switch the LED off",  NULL, 0u, NULL, 0u, cmd_led_off },
    { "blink",  "blink the LED",       NULL, 0u, blink_args, 2u, cmd_led_blink },
    { "status", "show the LED state",  NULL, 0u, NULL, 0u, cmd_led_status },
};

static const cli_cmd_t i2c_subs[] = {
    { "read",  "read bytes from a device", NULL, 0u, i2c_read_args, 2u, cmd_i2c_read },
    { "write", "write bytes to a device",  NULL, 0u, i2c_write_args, 2u, cmd_i2c_write },
};

static const cli_cmd_t commands[] = {
    { "led",  "LED control",      led_subs, 4u, NULL, 0u, NULL },
    { "i2c",  "I2C bus access",   i2c_subs, 2u, NULL, 0u, NULL },
    { "echo", "print arguments",  NULL, 0u, echo_args, 1u, cmd_echo },
    { "set",  "change a setting", NULL, 0u, set_args, 2u, cmd_set },
    { "quit", "leave the demo",   NULL, 0u, NULL, 0u, cmd_quit },
};

int main(void) {
    static cli_t cli;
    app_t app;
    cli_config_t cfg = cli_config_default();

    memset(&app, 0, sizeof(app));
    app.running = true;
    strncpy(app.name, "demo", sizeof(app.name) - 1u);

    term_raw();

    cfg.commands = commands;
    cfg.command_count = sizeof(commands) / sizeof(commands[0]);
    cfg.getc_fn = term_getc;
    cfg.putc_fn = term_putc;
    cfg.user = &app;
    cfg.prompt = "clipilot> ";

    printf("clipilot host demo - type 'help', TAB completes, Ctrl-C clears\r\n");
    fflush(stdout);
    cli_init(&cli, &cfg);

    while (app.running && !term_eof()) {
        cli_poll(&cli);
        term_idle();
    }

    term_restore();
    return 0;
}
