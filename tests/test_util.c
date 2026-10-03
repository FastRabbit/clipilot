/**
 * @file test_util.c
 * @brief Harness implementation: assertions and the loopback terminal.
 */
#include <stdio.h>
#include <string.h>

#include "test_util.h"

int test_checks;
int test_failures;

static const char *current_test = "?";
static char plain_buf[FX_OUT_MAX];

void test_begin(const char *name) {
    current_test = name;
    printf("  - %s\n", name);
}

void test_fail(const char *file, int line, const char *expr, const char *detail) {
    test_failures++;
    printf("    FAIL %s:%d in %s\n      %s\n", file, line, current_test, expr);
    if (detail != NULL) {
        printf("      actual: >>>%s<<<\n", detail);
    }
}

bool test_streq(const char *a, const char *b) {
    if ((a == NULL) || (b == NULL)) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

bool test_contains(const char *haystack, const char *needle) {
    if ((haystack == NULL) || (needle == NULL)) {
        return false;
    }
    return strstr(haystack, needle) != NULL;
}

/* ------------------------------------------------------------------ */

static int fx_getc(void *ctx) {
    fixture_t *fx = (fixture_t *)ctx;

    if (fx->in_pos >= fx->in_len) {
        return -1;
    }
    return (int)(unsigned char)fx->in[fx->in_pos++];
}

static void fx_putc(void *ctx, char c) {
    fixture_t *fx = (fixture_t *)ctx;

    if (fx->out_len < (FX_OUT_MAX - 1u)) {
        fx->out[fx->out_len++] = c;
        fx->out[fx->out_len] = '\0';
    }
}

void fx_init(fixture_t *fx, const cli_cmd_t *cmds, size_t count, bool echo) {
    cli_config_t cfg = cli_config_default();

    memset(fx, 0, sizeof(*fx));
    cfg.commands = cmds;
    cfg.command_count = count;
    cfg.getc_fn = fx_getc;
    cfg.putc_fn = fx_putc;
    cfg.io_ctx = fx;
    cfg.user = fx;
    cfg.echo = echo;
    cfg.crlf = true;
    cli_init(&fx->cli, &cfg);
    fx_clear(fx);
}

void fx_type_n(fixture_t *fx, const char *bytes, size_t len) {
    size_t i;

    if (fx->in_pos >= fx->in_len) {
        fx->in_pos = 0u;
        fx->in_len = 0u;
    }
    for (i = 0u; i < len; i++) {
        if (fx->in_len < FX_IN_MAX) {
            fx->in[fx->in_len++] = bytes[i];
        }
    }
    while (fx->in_pos < fx->in_len) {
        cli_poll(&fx->cli);
    }
}

void fx_type(fixture_t *fx, const char *bytes) {
    fx_type_n(fx, bytes, strlen(bytes));
}

void fx_clear(fixture_t *fx) {
    fx->out_len = 0u;
    fx->out[0] = '\0';
}

const char *fx_out(fixture_t *fx) {
    return fx->out;
}

const char *fx_plain(fixture_t *fx) {
    size_t i = 0u;
    size_t o = 0u;

    while ((i < fx->out_len) && (o < (sizeof(plain_buf) - 1u))) {
        char c = fx->out[i];

        if (c == '\x1b') {
            i++;
            if ((i < fx->out_len) && ((fx->out[i] == '[') || (fx->out[i] == 'O'))) {
                i++;
                while (i < fx->out_len) {
                    char s = fx->out[i];

                    i++;
                    if ((s >= 0x40) && (s <= 0x7E)) {
                        break;
                    }
                }
            }
            continue;
        }
        if (c == '\r') {
            i++;
            continue;
        }
        plain_buf[o++] = c;
        i++;
    }
    plain_buf[o] = '\0';
    return plain_buf;
}
