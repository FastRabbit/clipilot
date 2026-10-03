/**
 * @file cli_internal.h
 * @brief Declarations shared between the clipilot translation units.
 *
 * Not part of the public API.
 */
#pragma once

#include "cli.h"

/** Escape decoder states. */
enum {
    CLI_ESC_NONE = 0, /**< normal input */
    CLI_ESC_GOT = 1,  /**< ESC seen */
    CLI_ESC_SEQ = 2   /**< ESC [ or ESC O seen, collecting */
};

/* ---- cli.c ------------------------------------------------------------- */

/** Prints the prompt followed by the current line content. */
void cli_draw_prompt(cli_t *cli);

/** Handles a completed line: tokenize, dispatch, push to history, reprompt. */
void cli_submit_line(cli_t *cli);

/** Looks up a name in a command table. Returns NULL when absent. */
const cli_cmd_t *cli_lookup(const cli_cmd_t *table, size_t count, const char *name);

/** Looks up a top level command, application table first, then builtins. */
const cli_cmd_t *cli_lookup_top(const cli_t *cli, const char *name);

/** Writes the standard "error: " prefix. */
void cli_error_prefix(cli_t *cli);

/** Human readable name of an argument type. */
const char *cli_type_name(cli_arg_type_t type);

/* ---- cli_parse.c ------------------------------------------------------- */

/**
 * @brief Splits cli->line into cli->tokens using cli->parse_buf as storage.
 * Supports single/double quotes and backslash escapes.
 * @return CLI_OK, CLI_ERR_EMPTY, CLI_ERR_PARSE or CLI_ERR_OVERFLOW
 */
int cli_tokenize(cli_t *cli);

/**
 * @brief Validates arity and converts tokens according to @p cmd's spec.
 * Emits a diagnostic on failure.
 * @param first  index of the first argument token in cli->tokens
 * @param path   command path used in diagnostics, e.g. "led on"
 */
int cli_build_args(cli_t *cli, const cli_cmd_t *cmd, size_t first, const char *path);

/** @brief Converts one token. Returns true on success. */
bool cli_convert_token(const cli_arg_spec_t *spec, const char *tok, cli_arg_t *out);

/* ---- cli_edit.c -------------------------------------------------------- */

/** Feeds one byte to the line editor / escape decoder. */
void cli_edit_feed(cli_t *cli, char c);

/** Clears the edit buffer without touching the terminal. */
void cli_edit_reset(cli_t *cli);

/** Repaints the prompt and the current line, then positions the cursor. */
void cli_edit_refresh(cli_t *cli);

/** Inserts text at the cursor. Returns the number of characters inserted. */
size_t cli_edit_insert(cli_t *cli, const char *text, size_t len);

/** Replaces the whole line and repaints. */
void cli_edit_set_line(cli_t *cli, const char *text);

#if CLI_HISTORY_DEPTH > 0u
/** Appends a line to the history ring, skipping blanks and duplicates. */
void cli_hist_push(cli_t *cli, const char *line);
/** Returns the n-th newest entry (0 = newest) or NULL. */
const char *cli_hist_get(const cli_t *cli, size_t index);
/** Number of stored history entries. */
size_t cli_hist_count(const cli_t *cli);
#endif

/* ---- cli_complete.c ---------------------------------------------------- */

/** Handles a TAB keypress. */
void cli_complete(cli_t *cli);

/* ---- cli_builtin.c ----------------------------------------------------- */

#if CLI_ENABLE_BUILTINS
/** Returns the built-in command table. */
const cli_cmd_t *cli_builtin_table(size_t *count);
#endif
