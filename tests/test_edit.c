/**
 * @file test_edit.c
 * @brief Line editing, control keys and history navigation tests.
 */
#include <string.h>

#include "test_cmds.h"
#include "test_util.h"

#define KEY_UP    "\x1b[A"
#define KEY_DOWN  "\x1b[B"
#define KEY_RIGHT "\x1b[C"
#define KEY_LEFT  "\x1b[D"
#define KEY_HOME  "\x1b[H"
#define KEY_END   "\x1b[F"
#define KEY_DEL   "\x1b[3~"

static fixture_t fx;

static void setup(void) {
    size_t count = 0u;
    const cli_cmd_t *cmds = test_commands(&count);

    fx_init(&fx, cmds, count, true);
    test_log_reset();
}

static void t_plain_typing(void) {
    setup();
    fx_type(&fx, "hello");
    CHECK_STREQ(fx.cli.line, "hello");
    CHECK(fx.cli.cursor == 5u);
    CHECK_HAS(fx_out(&fx), "hello");
}

static void t_backspace(void) {
    setup();
    fx_type(&fx, "hel\x7f\x7f");
    CHECK_STREQ(fx.cli.line, "h");
    CHECK(fx.cli.cursor == 1u);

    fx_type(&fx, "\x08");
    CHECK_STREQ(fx.cli.line, "");
    fx_type(&fx, "\x7f");
    CHECK_STREQ(fx.cli.line, "");
    CHECK(fx.cli.cursor == 0u);
}

static void t_cursor_insert(void) {
    setup();
    fx_type(&fx, "abc");
    fx_type(&fx, KEY_LEFT KEY_LEFT);
    CHECK(fx.cli.cursor == 1u);
    fx_type(&fx, "X");
    CHECK_STREQ(fx.cli.line, "aXbc");
    CHECK(fx.cli.cursor == 2u);
    fx_type(&fx, KEY_RIGHT);
    CHECK(fx.cli.cursor == 3u);
}

static void t_delete_key(void) {
    setup();
    fx_type(&fx, "abcd" KEY_HOME KEY_DEL);
    CHECK_STREQ(fx.cli.line, "bcd");
    CHECK(fx.cli.cursor == 0u);
}

static void t_home_end(void) {
    setup();
    fx_type(&fx, "abcd" KEY_HOME);
    CHECK(fx.cli.cursor == 0u);
    fx_type(&fx, KEY_END);
    CHECK(fx.cli.cursor == 4u);

    fx_type(&fx, "\x01"); /* Ctrl-A */
    CHECK(fx.cli.cursor == 0u);
    fx_type(&fx, "\x05"); /* Ctrl-E */
    CHECK(fx.cli.cursor == 4u);
    fx_type(&fx, "\x02"); /* Ctrl-B */
    CHECK(fx.cli.cursor == 3u);
    fx_type(&fx, "\x06"); /* Ctrl-F */
    CHECK(fx.cli.cursor == 4u);
}

static void t_kill_keys(void) {
    setup();
    fx_type(&fx, "abcdef" KEY_LEFT KEY_LEFT "\x15"); /* Ctrl-U */
    CHECK_STREQ(fx.cli.line, "ef");
    CHECK(fx.cli.cursor == 0u);

    setup();
    fx_type(&fx, "abcdef" KEY_HOME KEY_RIGHT KEY_RIGHT "\x0b"); /* Ctrl-K */
    CHECK_STREQ(fx.cli.line, "ab");
    CHECK(fx.cli.cursor == 2u);

    setup();
    fx_type(&fx, "one two\x17"); /* Ctrl-W */
    CHECK_STREQ(fx.cli.line, "one ");

    fx_type(&fx, "\x17");
    CHECK_STREQ(fx.cli.line, "");
}

static void t_ctrl_c(void) {
    setup();
    fx_type(&fx, "abc");
    fx_clear(&fx);
    fx_type(&fx, "\x03");
    CHECK_STREQ(fx.cli.line, "");
    CHECK(fx.cli.cursor == 0u);
    CHECK_HAS(fx_plain(&fx), "^C");
}

static void t_ctrl_d_deletes(void) {
    setup();
    fx_type(&fx, "abc" KEY_HOME "\x04");
    CHECK_STREQ(fx.cli.line, "bc");
}

static void t_unknown_escape_ignored(void) {
    setup();
    fx_type(&fx, "ab\x1b[5~cd");
    CHECK_STREQ(fx.cli.line, "abcd");

    fx_type(&fx, "\x1bZ");
    CHECK_STREQ(fx.cli.line, "abcd");
}

static void t_history_navigation(void) {
    setup();
    fx_type(&fx, "led on\r");
    fx_type(&fx, "led off\r");

    fx_type(&fx, KEY_UP);
    CHECK_STREQ(fx.cli.line, "led off");
    CHECK(fx.cli.cursor == 7u);

    fx_type(&fx, KEY_UP);
    CHECK_STREQ(fx.cli.line, "led on");

    fx_type(&fx, KEY_UP); /* no more entries */
    CHECK_STREQ(fx.cli.line, "led on");

    fx_type(&fx, KEY_DOWN);
    CHECK_STREQ(fx.cli.line, "led off");

    fx_type(&fx, KEY_DOWN);
    CHECK_STREQ(fx.cli.line, "");

    fx_type(&fx, KEY_DOWN);
    CHECK_STREQ(fx.cli.line, "");
}

static void t_history_stash(void) {
    setup();
    fx_type(&fx, "led on\r");
    fx_type(&fx, "partial");
    fx_type(&fx, KEY_UP);
    CHECK_STREQ(fx.cli.line, "led on");
    fx_type(&fx, KEY_DOWN);
    CHECK_STREQ(fx.cli.line, "partial");
}

static void t_history_skips_duplicates(void) {
    setup();
    fx_type(&fx, "led on\r");
    fx_type(&fx, "led on\r");
    fx_type(&fx, "\r");
    fx_type(&fx, KEY_UP);
    CHECK_STREQ(fx.cli.line, "led on");
    fx_type(&fx, KEY_UP);
    CHECK_STREQ(fx.cli.line, "led on");
}

static void t_history_ctrl_pn(void) {
    setup();
    fx_type(&fx, "led on\r");
    fx_type(&fx, "\x10"); /* Ctrl-P */
    CHECK_STREQ(fx.cli.line, "led on");
    fx_type(&fx, "\x0e"); /* Ctrl-N */
    CHECK_STREQ(fx.cli.line, "");
}

static void t_line_overflow(void) {
    char big[CLI_MAX_LINE_LEN + 8u];
    size_t i;

    setup();
    for (i = 0u; i < (sizeof(big) - 1u); i++) {
        big[i] = 'x';
    }
    big[sizeof(big) - 1u] = '\0';

    fx_type(&fx, big);
    CHECK(fx.cli.len == (size_t)CLI_MAX_LINE_LEN);
    fx_clear(&fx);
    fx_type(&fx, "\r");
    CHECK_HAS(fx_plain(&fx), "line too long");
    CHECK(fx.cli.len == 0u);
}

static void t_prompt_redrawn(void) {
    setup();
    fx_clear(&fx);
    fx_type(&fx, "led on\r");
    CHECK_HAS(fx_plain(&fx), "> ");
    CHECK(fx.cli.len == 0u);
}

static void t_set_prompt(void) {
    setup();
    cli_set_prompt(&fx.cli, "stm32# ");
    fx_clear(&fx);
    fx_type(&fx, "a");
    CHECK_HAS(fx_out(&fx), "a");
    fx_clear(&fx);
    fx_type(&fx, "\x03");
    CHECK_HAS(fx_plain(&fx), "stm32# ");
}

void suite_edit(void) {
    printf("suite: edit\n");
    RUN(t_plain_typing);
    RUN(t_backspace);
    RUN(t_cursor_insert);
    RUN(t_delete_key);
    RUN(t_home_end);
    RUN(t_kill_keys);
    RUN(t_ctrl_c);
    RUN(t_ctrl_d_deletes);
    RUN(t_unknown_escape_ignored);
    RUN(t_history_navigation);
    RUN(t_history_stash);
    RUN(t_history_skips_duplicates);
    RUN(t_history_ctrl_pn);
    RUN(t_line_overflow);
    RUN(t_prompt_redrawn);
    RUN(t_set_prompt);
}
