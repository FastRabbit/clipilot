/**
 * @file test_dispatch.c
 * @brief Command lookup, subcommand handling and builtin tests.
 */
#include <string.h>

#include "test_cmds.h"
#include "test_util.h"

static fixture_t fx;

static int run(const char *line) {
    size_t count = 0u;
    const cli_cmd_t *cmds = test_commands(&count);

    fx_init(&fx, cmds, count, false);
    test_log_reset();
    fx.last_rc = cli_exec(&fx.cli, line);
    return fx.last_rc;
}

static void t_unknown_command(void) {
    CHECK(run("nope") == CLI_ERR_UNKNOWN_CMD);
    CHECK_HAS(fx_plain(&fx), "unknown command 'nope'");
    CHECK_HAS(fx_plain(&fx), "type 'help'");
    CHECK(g_log.calls == 0);
}

static void t_subcommand_required(void) {
    CHECK(run("led") == CLI_ERR_UNKNOWN_SUB);
    CHECK_HAS(fx_plain(&fx), "'led' requires a subcommand");
    CHECK_HAS(fx_plain(&fx), "subcommands: on off blink");
}

static void t_unknown_subcommand(void) {
    CHECK(run("led sparkle") == CLI_ERR_UNKNOWN_SUB);
    CHECK_HAS(fx_plain(&fx), "unknown subcommand 'sparkle' for 'led'");
}

static void t_subcommand_dispatch(void) {
    CHECK(run("led on") == CLI_OK);
    CHECK_STREQ(g_log.name, "led on");
    CHECK(g_log.args.count == 0u);

    CHECK(run("sys stats") == CLI_OK);
    CHECK_STREQ(g_log.name, "sys stats");
}

static void t_handler_failure_reported(void) {
    CHECK(run("fail") == -7);
    CHECK_HAS(fx_plain(&fx), "'fail' returned -7");
}

static void t_case_sensitive(void) {
    CHECK(run("LED on") == CLI_ERR_UNKNOWN_CMD);
}

static void t_help_overview(void) {
    CHECK(run("help") == CLI_OK);
    CHECK_HAS(fx_plain(&fx), "commands:");
    CHECK_HAS(fx_plain(&fx), "led");
    CHECK_HAS(fx_plain(&fx), "LED control");
    CHECK_HAS(fx_plain(&fx), "history");
    CHECK_HAS(fx_plain(&fx), "help <command>");
}

static void t_help_for_command(void) {
    CHECK(run("help led blink") == CLI_OK);
    CHECK_HAS(fx_plain(&fx), "blink the LED");
    CHECK_HAS(fx_plain(&fx), "usage: led blink <period:unsigned integer> [count:integer]");
    CHECK_HAS(fx_plain(&fx), "blink period in ms");

    CHECK(run("help led") == CLI_OK);
    CHECK_HAS(fx_plain(&fx), "usage: led <subcommand>");
    CHECK_HAS(fx_plain(&fx), "switch the LED on");

    CHECK(run("help nope") == CLI_OK);
    CHECK_HAS(fx_plain(&fx), "unknown command 'nope'");
}

static void t_history_builtin(void) {
    size_t count = 0u;
    const cli_cmd_t *cmds = test_commands(&count);

    fx_init(&fx, cmds, count, false);
    fx_type(&fx, "led on\r");
    fx_type(&fx, "led off\r");
    fx_type(&fx, "led on\r");
    fx_clear(&fx);
    fx_type(&fx, "history\r");

    CHECK_HAS(fx_plain(&fx), "1  led on");
    CHECK_HAS(fx_plain(&fx), "2  led off");
    CHECK_HAS(fx_plain(&fx), "3  led on");
}

static void t_line_endings(void) {
    size_t count = 0u;
    const cli_cmd_t *cmds = test_commands(&count);

    fx_init(&fx, cmds, count, false);
    test_log_reset();
    fx_type(&fx, "led on\r\n");
    CHECK(g_log.calls == 1);

    fx_type(&fx, "led off\n");
    CHECK(g_log.calls == 2);
    CHECK_STREQ(g_log.name, "led off");
}

void suite_dispatch(void) {
    printf("suite: dispatch\n");
    RUN(t_unknown_command);
    RUN(t_subcommand_required);
    RUN(t_unknown_subcommand);
    RUN(t_subcommand_dispatch);
    RUN(t_handler_failure_reported);
    RUN(t_case_sensitive);
    RUN(t_help_overview);
    RUN(t_help_for_command);
    RUN(t_history_builtin);
    RUN(t_line_endings);
}
