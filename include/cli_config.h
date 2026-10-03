/**
 * @file cli_config.h
 * @brief Compile-time configuration for the clipilot embedded CLI.
 *
 * Every value can be overridden by defining it before including this header,
 * or on the compiler command line (e.g. -DCLI_MAX_LINE_LEN=64).
 */
#pragma once

/** Maximum number of characters in one input line (excluding the terminator). */
#ifndef CLI_MAX_LINE_LEN
#define CLI_MAX_LINE_LEN 128u
#endif

/** Number of lines kept in the history ring. Set to 0 to disable history. */
#ifndef CLI_HISTORY_DEPTH
#define CLI_HISTORY_DEPTH 8u
#endif

/** Maximum number of whitespace separated tokens on one line. */
#ifndef CLI_MAX_TOKENS
#define CLI_MAX_TOKENS 16u
#endif

/** Maximum number of arguments handed to a command handler. */
#ifndef CLI_MAX_ARGS
#define CLI_MAX_ARGS 12u
#endif

/** Maximum number of candidates listed by tab completion. */
#ifndef CLI_MAX_COMPLETIONS
#define CLI_MAX_COMPLETIONS 32u
#endif

/** Maximum length of the prompt string (excluding the terminator). */
#ifndef CLI_MAX_PROMPT_LEN
#define CLI_MAX_PROMPT_LEN 16u
#endif

/** Maximum bytes buffered while decoding one escape sequence. */
#ifndef CLI_MAX_ESC_LEN
#define CLI_MAX_ESC_LEN 8u
#endif

/** Maximum bytes consumed by a single cli_poll() call (0 = until starved). */
#ifndef CLI_POLL_BUDGET
#define CLI_POLL_BUDGET 64u
#endif

/** Set to 0 to drop float argument support (avoids soft-float pulls). */
#ifndef CLI_ENABLE_FLOAT
#define CLI_ENABLE_FLOAT 1
#endif

/** Set to 0 to drop cli_printf() and with it the libc vsnprintf dependency. */
#ifndef CLI_ENABLE_PRINTF
#define CLI_ENABLE_PRINTF 1
#endif

/** Scratch buffer size used by cli_printf(). */
#ifndef CLI_PRINTF_BUF_LEN
#define CLI_PRINTF_BUF_LEN 128u
#endif

/** Set to 0 to drop the built-in `help` and `history` commands. */
#ifndef CLI_ENABLE_BUILTINS
#define CLI_ENABLE_BUILTINS 1
#endif

/** Default prompt used when cli_config_t::prompt is NULL. */
#ifndef CLI_DEFAULT_PROMPT
#define CLI_DEFAULT_PROMPT "> "
#endif
