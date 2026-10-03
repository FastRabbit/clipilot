/**
 * @file test_cmds.c
 * @brief Command tree exercised by the test suites.
 */
#include <string.h>

#include "test_cmds.h"

call_log_t g_log;

void test_log_reset(void) {
    memset(&g_log, 0, sizeof(g_log));
}

static int record(const char *name, const cli_args_t *args) {
    g_log.name = name;
    g_log.args = *args;
    g_log.calls++;
    return 0;
}

static int h_led_on(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("led on", args);
}

static int h_led_off(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("led off", args);
}

static int h_led_blink(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("led blink", args);
}

static int h_i2c_read(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("i2c read", args);
}

static int h_echo(cli_t *cli, const cli_args_t *args, void *user) {
    size_t i;

    (void)user;
    for (i = 0u; i < args->count; i++) {
        if (i != 0u) {
            cli_write(cli, " ");
        }
        cli_write(cli, cli_arg_str(args, i, ""));
    }
    cli_write(cli, "\n");
    return record("echo", args);
}

static int h_set(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("set", args);
}

static int h_mode(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("mode", args);
}

static int h_flag(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("flag", args);
}

static int h_calc(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("calc", args);
}

static int h_fail(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    (void)record("fail", args);
    return -7;
}

static int h_stats(cli_t *cli, const cli_args_t *args, void *user) {
    (void)cli;
    (void)user;
    return record("sys stats", args);
}

static const cli_arg_spec_t blink_args[] = {
    { "period", CLI_ARG_UINT, false, false, NULL, "blink period in ms" },
    { "count",  CLI_ARG_INT,  true,  false, NULL, "number of blinks" },
};

static const cli_arg_spec_t read_args[] = {
    { "addr", CLI_ARG_HEX,  false, false, NULL, "7 bit device address" },
    { "len",  CLI_ARG_UINT, false, false, NULL, "number of bytes" },
};

static const cli_arg_spec_t echo_args[] = {
    { "text", CLI_ARG_STR, false, true, NULL, "words to print" },
};

static const char *const key_values[] = { "speed", "mode", "name", NULL };

static const cli_arg_spec_t set_args[] = {
    { "key",   CLI_ARG_ENUM, false, false, key_values, "setting to change" },
    { "value", CLI_ARG_STR,  false, false, NULL,       "new value" },
};

static const char *const mode_values[] = { "fast", "faster", "slow", NULL };

static const cli_arg_spec_t mode_args[] = {
    { "mode", CLI_ARG_ENUM, false, false, mode_values, "operating mode" },
};

static const cli_arg_spec_t flag_args[] = {
    { "state", CLI_ARG_BOOL, false, false, NULL, "on or off" },
};

static const cli_arg_spec_t calc_args[] = {
    { "value", CLI_ARG_FLOAT, false, false, NULL, "a floating point number" },
};

static const cli_cmd_t led_subs[] = {
    { "on",    "switch the LED on",  NULL, 0u, NULL, 0u, h_led_on },
    { "off",   "switch the LED off", NULL, 0u, NULL, 0u, h_led_off },
    { "blink", "blink the LED",      NULL, 0u, blink_args, 2u, h_led_blink },
};

static const cli_cmd_t i2c_subs[] = {
    { "read", "read from a device", NULL, 0u, read_args, 2u, h_i2c_read },
};

static const cli_cmd_t sys_subs[] = {
    { "stats", "print counters", NULL, 0u, NULL, 0u, h_stats },
};

static const cli_cmd_t commands[] = {
    { "led",  "LED control",       led_subs, 3u, NULL, 0u, NULL },
    { "i2c",  "I2C bus access",    i2c_subs, 1u, NULL, 0u, NULL },
    { "sys",  "system info",       sys_subs, 1u, NULL, 0u, NULL },
    { "echo", "print the input",   NULL, 0u, echo_args, 1u, h_echo },
    { "set",  "change a setting",  NULL, 0u, set_args,  2u, h_set },
    { "mode", "select a mode",     NULL, 0u, mode_args, 1u, h_mode },
    { "flag", "toggle a flag",     NULL, 0u, flag_args, 1u, h_flag },
    { "calc", "float playground",  NULL, 0u, calc_args, 1u, h_calc },
    { "fail", "always fails",      NULL, 0u, NULL, 0u, h_fail },
};

const cli_cmd_t *test_commands(size_t *count) {
    *count = sizeof(commands) / sizeof(commands[0]);
    return commands;
}
