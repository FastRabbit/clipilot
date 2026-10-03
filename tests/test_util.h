/**
 * @file test_util.h
 * @brief Minimal assert harness and a fake terminal for the clipilot tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "cli.h"

#define FX_IN_MAX  512u
#define FX_OUT_MAX 8192u

/** Loopback terminal: scripted input bytes in, captured output bytes out. */
typedef struct {
    cli_t cli;
    char in[FX_IN_MAX];
    size_t in_len;
    size_t in_pos;
    char out[FX_OUT_MAX];
    size_t out_len;
    int last_rc;
} fixture_t;

extern int test_checks;
extern int test_failures;

void test_fail(const char *file, int line, const char *expr, const char *detail);

#define CHECK(cond)                                                            \
    do {                                                                       \
        test_checks++;                                                         \
        if (!(cond)) {                                                         \
            test_fail(__FILE__, __LINE__, #cond, NULL);                        \
        }                                                                      \
    } while (0)

#define CHECK_STREQ(actual, expected)                                          \
    do {                                                                       \
        test_checks++;                                                         \
        if (!test_streq((actual), (expected))) {                               \
            test_fail(__FILE__, __LINE__, #actual " == " #expected, (actual)); \
        }                                                                      \
    } while (0)

#define CHECK_HAS(haystack, needle)                                            \
    do {                                                                       \
        test_checks++;                                                         \
        if (!test_contains((haystack), (needle))) {                            \
            test_fail(__FILE__, __LINE__, "output contains " #needle,          \
                      (haystack));                                             \
        }                                                                      \
    } while (0)

#define CHECK_HASNT(haystack, needle)                                          \
    do {                                                                       \
        test_checks++;                                                         \
        if (test_contains((haystack), (needle))) {                             \
            test_fail(__FILE__, __LINE__, "output lacks " #needle,             \
                      (haystack));                                             \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        test_begin(#fn);                                                       \
        fn();                                                                  \
    } while (0)

bool test_streq(const char *a, const char *b);
bool test_contains(const char *haystack, const char *needle);
void test_begin(const char *name);

/** Initialises a fixture and its CLI instance. */
void fx_init(fixture_t *fx, const cli_cmd_t *cmds, size_t count, bool echo);

/** Queues input bytes and pumps them through cli_poll(). */
void fx_type(fixture_t *fx, const char *bytes);

/** Queues @p len input bytes and pumps them through cli_poll(). */
void fx_type_n(fixture_t *fx, const char *bytes, size_t len);

/** Discards captured output. */
void fx_clear(fixture_t *fx);

/** Captured output as a NUL terminated string. */
const char *fx_out(fixture_t *fx);

/** Captured output with ANSI escape sequences and CR removed. */
const char *fx_plain(fixture_t *fx);

/* Test suites. */
void suite_parse(void);
void suite_dispatch(void);
void suite_edit(void);
void suite_complete(void);
