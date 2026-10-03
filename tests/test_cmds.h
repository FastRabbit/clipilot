/**
 * @file test_cmds.h
 * @brief Shared command table used by the clipilot test suites.
 */
#pragma once

#include "cli.h"

/** Records the most recent handler invocation. */
typedef struct {
    const char *name;
    cli_args_t args;
    int calls;
} call_log_t;

extern call_log_t g_log;

/** Returns the test command table. */
const cli_cmd_t *test_commands(size_t *count);

/** Resets the invocation log. */
void test_log_reset(void);
