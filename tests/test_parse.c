/**
 * @file test_parse.c
 * @brief Tokenizer and typed argument conversion tests.
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

static void t_quoted_strings(void) {
    CHECK(run("echo \"hello world\"") == CLI_OK);
    CHECK(g_log.args.count == 1u);
    CHECK_STREQ(cli_arg_str(&g_log.args, 0u, ""), "hello world");

    CHECK(run("echo 'a b' c") == CLI_OK);
    CHECK(g_log.args.count == 2u);
    CHECK_STREQ(cli_arg_str(&g_log.args, 0u, ""), "a b");
    CHECK_STREQ(cli_arg_str(&g_log.args, 1u, ""), "c");
}

static void t_escapes(void) {
    CHECK(run("echo a\\ b") == CLI_OK);
    CHECK(g_log.args.count == 1u);
    CHECK_STREQ(cli_arg_str(&g_log.args, 0u, ""), "a b");

    CHECK(run("echo \"say \\\"hi\\\"\"") == CLI_OK);
    CHECK_STREQ(cli_arg_str(&g_log.args, 0u, ""), "say \"hi\"");

    CHECK(run("echo 'raw\\n'") == CLI_OK);
    CHECK_STREQ(cli_arg_str(&g_log.args, 0u, ""), "raw\\n");
}

static void t_unterminated_quote(void) {
    CHECK(run("echo \"oops") == CLI_ERR_PARSE);
    CHECK_HAS(fx_plain(&fx), "unterminated quote");
    CHECK(g_log.calls == 0);
}

static void t_whitespace_only(void) {
    CHECK(run("   ") == CLI_OK);
    CHECK(g_log.calls == 0);
    CHECK_STREQ(fx_plain(&fx), "");
}

static void t_uint_formats(void) {
    CHECK(run("led blink 1000") == CLI_OK);
    CHECK(cli_arg_uint(&g_log.args, 0u, 0u) == 1000u);

    CHECK(run("led blink 0x20") == CLI_OK);
    CHECK(cli_arg_uint(&g_log.args, 0u, 0u) == 0x20u);

    CHECK(run("led blink 0b1010") == CLI_OK);
    CHECK(cli_arg_uint(&g_log.args, 0u, 0u) == 10u);

    CHECK(run("led blink 1_000") == CLI_OK);
    CHECK(cli_arg_uint(&g_log.args, 0u, 0u) == 1000u);

    CHECK(run("led blink 4294967296") == CLI_ERR_ARG_TYPE);
    CHECK(run("led blink -1") == CLI_ERR_ARG_TYPE);
    CHECK(run("led blink 12ab") == CLI_ERR_ARG_TYPE);
}

static void t_int_formats(void) {
    CHECK(run("led blink 10 -5") == CLI_OK);
    CHECK(cli_arg_int(&g_log.args, 1u, 0) == -5);

    CHECK(run("led blink 10 +5") == CLI_OK);
    CHECK(cli_arg_int(&g_log.args, 1u, 0) == 5);

    CHECK(run("led blink 10 -2147483648") == CLI_OK);
    CHECK(cli_arg_int(&g_log.args, 1u, 0) == INT32_MIN);

    CHECK(run("led blink 10 2147483648") == CLI_ERR_ARG_TYPE);
}

static void t_hex_arg(void) {
    CHECK(run("i2c read 0x48 4") == CLI_OK);
    CHECK(cli_arg_uint(&g_log.args, 0u, 0u) == 0x48u);
    CHECK(cli_arg_uint(&g_log.args, 1u, 0u) == 4u);

    CHECK(run("i2c read 48 4") == CLI_OK);
    CHECK(cli_arg_uint(&g_log.args, 0u, 0u) == 0x48u);

    CHECK(run("i2c read zz 4") == CLI_ERR_ARG_TYPE);
    CHECK_HAS(fx_plain(&fx), "not a valid hex value");
}

static void t_bool_arg(void) {
    CHECK(run("flag on") == CLI_OK);
    CHECK(cli_arg_bool(&g_log.args, 0u, false) == true);

    CHECK(run("flag FALSE") == CLI_OK);
    CHECK(cli_arg_bool(&g_log.args, 0u, true) == false);

    CHECK(run("flag 1") == CLI_OK);
    CHECK(cli_arg_bool(&g_log.args, 0u, false) == true);

    CHECK(run("flag maybe") == CLI_ERR_ARG_TYPE);
    CHECK_HAS(fx_plain(&fx), "not a valid boolean");
}

static void t_enum_arg(void) {
    CHECK(run("mode slow") == CLI_OK);
    CHECK(cli_arg_enum(&g_log.args, 0u, -1) == 2);

    CHECK(run("mode FAST") == CLI_OK);
    CHECK(cli_arg_enum(&g_log.args, 0u, -1) == 0);

    CHECK(run("mode turbo") == CLI_ERR_ARG_TYPE);
    CHECK_HAS(fx_plain(&fx), "expected one of: fast faster slow");
}

#if CLI_ENABLE_FLOAT
static void t_float_arg(void) {
    float v;

    CHECK(run("calc 3.5") == CLI_OK);
    v = cli_arg_float(&g_log.args, 0u, 0.0f);
    CHECK((v > 3.49f) && (v < 3.51f));

    CHECK(run("calc -0.25") == CLI_OK);
    v = cli_arg_float(&g_log.args, 0u, 0.0f);
    CHECK((v > -0.26f) && (v < -0.24f));

    CHECK(run("calc 1e3") == CLI_OK);
    v = cli_arg_float(&g_log.args, 0u, 0.0f);
    CHECK((v > 999.0f) && (v < 1001.0f));

    CHECK(run("calc 2.5e-2") == CLI_OK);
    v = cli_arg_float(&g_log.args, 0u, 0.0f);
    CHECK((v > 0.024f) && (v < 0.026f));

    CHECK(run("calc pi") == CLI_ERR_ARG_TYPE);
    CHECK(run("calc 1.2.3") == CLI_ERR_ARG_TYPE);
}
#endif

static void t_variadic(void) {
    CHECK(run("echo one two three") == CLI_OK);
    CHECK(g_log.args.count == 3u);
    CHECK_STREQ(cli_arg_str(&g_log.args, 2u, ""), "three");
    CHECK_HAS(fx_plain(&fx), "one two three");
}

static void t_arity(void) {
    CHECK(run("led blink") == CLI_ERR_ARG_COUNT);
    CHECK_HAS(fx_plain(&fx), "missing argument");
    CHECK_HAS(fx_plain(&fx), "usage: led blink <period:unsigned integer>");

    CHECK(run("led blink 1 2 3") == CLI_ERR_ARG_COUNT);
    CHECK_HAS(fx_plain(&fx), "too many arguments");

    CHECK(run("led on extra") == CLI_ERR_ARG_COUNT);
    CHECK_HAS(fx_plain(&fx), "'led on' takes no arguments");

    CHECK(run("led blink 5") == CLI_OK);
    CHECK(g_log.args.count == 1u);
    CHECK(cli_arg_present(&g_log.args, 1u) == false);
}

void suite_parse(void) {
    printf("suite: parse\n");
    RUN(t_quoted_strings);
    RUN(t_escapes);
    RUN(t_unterminated_quote);
    RUN(t_whitespace_only);
    RUN(t_uint_formats);
    RUN(t_int_formats);
    RUN(t_hex_arg);
    RUN(t_bool_arg);
    RUN(t_enum_arg);
#if CLI_ENABLE_FLOAT
    RUN(t_float_arg);
#endif
    RUN(t_variadic);
    RUN(t_arity);
}
