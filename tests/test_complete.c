/**
 * @file test_complete.c
 * @brief TAB completion tests.
 */
#include <string.h>

#include "test_cmds.h"
#include "test_util.h"

#define TAB "\t"

static fixture_t fx;

static void setup(void) {
    size_t count = 0u;
    const cli_cmd_t *cmds = test_commands(&count);

    fx_init(&fx, cmds, count, true);
    test_log_reset();
}

static void t_unique_command(void) {
    setup();
    fx_type(&fx, "le" TAB);
    CHECK_STREQ(fx.cli.line, "led ");
    CHECK(fx.cli.cursor == 4u);
}

static void t_ambiguous_command_lists(void) {
    setup();
    fx_type(&fx, "s");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_STREQ(fx.cli.line, "s");
    CHECK_HAS(fx_plain(&fx), "sys");
    CHECK_HAS(fx_plain(&fx), "set");
    CHECK_HAS(fx_plain(&fx), "> s");
}

static void t_common_prefix_inserted(void) {
    setup();
    fx_type(&fx, "mode fa" TAB);
    CHECK_STREQ(fx.cli.line, "mode fast"); /* fast, faster -> "fast" */

    setup();
    fx_type(&fx, "f");
    fx_clear(&fx);
    fx_type(&fx, TAB); /* flag, fail share only "f" -> list instead */
    CHECK_STREQ(fx.cli.line, "f");
    CHECK_HAS(fx_plain(&fx), "flag");
    CHECK_HAS(fx_plain(&fx), "fail");
}

static void t_unique_subcommand(void) {
    setup();
    fx_type(&fx, "led b" TAB);
    CHECK_STREQ(fx.cli.line, "led blink ");
}

static void t_ambiguous_subcommand_lists(void) {
    setup();
    fx_type(&fx, "led o");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_STREQ(fx.cli.line, "led o");
    CHECK_HAS(fx_plain(&fx), "on");
    CHECK_HAS(fx_plain(&fx), "off");
}

static void t_empty_prefix_lists_subcommands(void) {
    setup();
    fx_type(&fx, "led ");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_STREQ(fx.cli.line, "led ");
    CHECK_HAS(fx_plain(&fx), "blink");
}

static void t_enum_argument_completion(void) {
    setup();
    fx_type(&fx, "set s" TAB);
    CHECK_STREQ(fx.cli.line, "set speed ");

    setup();
    fx_type(&fx, "set ");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_HAS(fx_plain(&fx), "speed");
    CHECK_HAS(fx_plain(&fx), "name");
}

static void t_no_candidates(void) {
    setup();
    fx_type(&fx, "zz");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_STREQ(fx.cli.line, "zz");
    CHECK_HAS(fx_out(&fx), "\a");

    setup();
    fx_type(&fx, "echo ab");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_STREQ(fx.cli.line, "echo ab"); /* plain string arg, nothing to offer */
}

static void t_builtins_are_completed(void) {
    setup();
    fx_type(&fx, "hi" TAB);
    CHECK_STREQ(fx.cli.line, "history ");

    setup();
    fx_type(&fx, "he" TAB);
    CHECK_STREQ(fx.cli.line, "help ");
}

static void t_completion_after_unknown_command(void) {
    setup();
    fx_type(&fx, "bogus s");
    fx_clear(&fx);
    fx_type(&fx, TAB);
    CHECK_STREQ(fx.cli.line, "bogus s");
}

static void t_completed_command_runs(void) {
    setup();
    fx_type(&fx, "led o" TAB);
    fx_type(&fx, "n" TAB);
    CHECK_STREQ(fx.cli.line, "led on ");
    fx_type(&fx, "\r");
    CHECK_STREQ(g_log.name, "led on");
    CHECK(g_log.calls == 1);
}

void suite_complete(void) {
    printf("suite: complete\n");
    RUN(t_unique_command);
    RUN(t_ambiguous_command_lists);
    RUN(t_common_prefix_inserted);
    RUN(t_unique_subcommand);
    RUN(t_ambiguous_subcommand_lists);
    RUN(t_empty_prefix_lists_subcommands);
    RUN(t_enum_argument_completion);
    RUN(t_no_candidates);
    RUN(t_builtins_are_completed);
    RUN(t_completion_after_unknown_command);
    RUN(t_completed_command_runs);
}
