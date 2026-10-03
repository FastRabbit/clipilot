/**
 * @file cli.h
 * @brief clipilot - a small, allocation-free command line interface for MCUs.
 *
 * The library needs two hooks from the application: a non-blocking character
 * source and a character sink. Everything else (line editing, history, tab
 * completion, tokenizing, typed argument conversion and dispatch) is handled
 * by the core.
 *
 * @code
 * static int led_on(cli_t *cli, const cli_args_t *args, void *user);
 *
 * static const cli_cmd_t led_subs[] = {
 *     { .name = "on",  .help = "turn the LED on",  .handler = led_on },
 * };
 * static const cli_cmd_t commands[] = {
 *     { .name = "led", .help = "LED control",
 *       .subcmds = led_subs, .subcmd_count = 1 },
 * };
 *
 * cli_t cli;
 * cli_config_t cfg = cli_config_default();
 * cfg.commands = commands;
 * cfg.command_count = 1;
 * cfg.getc_fn = uart_getc;
 * cfg.putc_fn = uart_putc;
 * cli_init(&cli, &cfg);
 * for (;;) { cli_poll(&cli); }
 * @endcode
 */
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cli_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes returned by the core. Handlers may return their own values. */
typedef enum {
    CLI_OK = 0,               /**< success */
    CLI_ERR_UNKNOWN_CMD = -1, /**< no such command */
    CLI_ERR_UNKNOWN_SUB = -2, /**< no such subcommand */
    CLI_ERR_ARG_COUNT = -3,   /**< too few or too many arguments */
    CLI_ERR_ARG_TYPE = -4,    /**< an argument failed type conversion */
    CLI_ERR_PARSE = -5,       /**< malformed line (unterminated quote, ...) */
    CLI_ERR_OVERFLOW = -6,    /**< line or token limit exceeded */
    CLI_ERR_EMPTY = -7        /**< line contained no tokens */
} cli_status_t;

/** Argument types understood by the automatic converter. */
typedef enum {
    CLI_ARG_INT,   /**< signed 32 bit; accepts decimal, 0x.., 0b.. */
    CLI_ARG_UINT,  /**< unsigned 32 bit; accepts decimal, 0x.., 0b.. */
    CLI_ARG_HEX,   /**< unsigned 32 bit; hexadecimal, optional 0x prefix */
    CLI_ARG_FLOAT, /**< float; requires CLI_ENABLE_FLOAT */
    CLI_ARG_STR,   /**< raw string, quotes and escapes already resolved */
    CLI_ARG_BOOL,  /**< on/off, true/false, yes/no, 1/0 */
    CLI_ARG_ENUM   /**< one of cli_arg_spec_t::enum_values */
} cli_arg_type_t;

/** Declarative description of one command argument. */
typedef struct {
    const char *name;                 /**< shown in usage output */
    cli_arg_type_t type;              /**< how the token is converted */
    bool optional;                    /**< may be omitted (trailing only) */
    bool variadic;                    /**< last spec soaks up remaining tokens */
    const char *const *enum_values;   /**< NULL terminated, for CLI_ARG_ENUM */
    const char *help;                 /**< one line description, may be NULL */
} cli_arg_spec_t;

/** One converted argument value. */
typedef struct {
    cli_arg_type_t type; /**< type the value was converted as */
    const char *raw;     /**< the unconverted token text */
    union {
        int32_t i;      /**< CLI_ARG_INT */
        uint32_t u;     /**< CLI_ARG_UINT, CLI_ARG_HEX */
#if CLI_ENABLE_FLOAT
        float f;        /**< CLI_ARG_FLOAT */
#endif
        bool b;         /**< CLI_ARG_BOOL */
        const char *s;  /**< CLI_ARG_STR */
        int e;          /**< CLI_ARG_ENUM: index into enum_values */
    } val;
} cli_arg_t;

/** Argument vector handed to a command handler. */
typedef struct {
    size_t count;                 /**< number of arguments actually supplied */
    cli_arg_t v[CLI_MAX_ARGS];    /**< converted values */
} cli_args_t;

typedef struct cli cli_t;

/**
 * @brief Command handler.
 * @param cli   the CLI instance, usable for output
 * @param args  converted arguments, already validated against the spec
 * @param user  cli_config_t::user pointer
 * @return 0 on success; non-zero is reported to the terminal
 */
typedef int (*cli_handler_fn)(cli_t *cli, const cli_args_t *args, void *user);

/** A command, or a subcommand when referenced from cli_cmd_t::subcmds. */
typedef struct cli_cmd {
    const char *name;                /**< command word, must not contain spaces */
    const char *help;                /**< one line description, may be NULL */
    const struct cli_cmd *subcmds;   /**< optional subcommand table */
    size_t subcmd_count;             /**< number of entries in @c subcmds */
    const cli_arg_spec_t *args;      /**< optional argument specification */
    size_t arg_count;                /**< number of entries in @c args */
    cli_handler_fn handler;          /**< invoked after successful validation */
} cli_cmd_t;

/**
 * @brief Non-blocking character source.
 * @return the next byte, or a negative value when no byte is available
 */
typedef int (*cli_getc_fn)(void *io_ctx);

/** @brief Character sink. */
typedef void (*cli_putc_fn)(void *io_ctx, char c);

/** Instance configuration, copied into the instance by cli_init(). */
typedef struct {
    const cli_cmd_t *commands; /**< application command table */
    size_t command_count;      /**< number of entries in @c commands */
    cli_getc_fn getc_fn;       /**< required character source */
    cli_putc_fn putc_fn;       /**< required character sink */
    void *io_ctx;              /**< passed to getc_fn/putc_fn */
    void *user;                /**< passed to command handlers */
    const char *prompt;        /**< NULL selects CLI_DEFAULT_PROMPT */
    bool echo;                 /**< echo input and emit escape sequences */
    bool crlf;                 /**< translate '\\n' output to "\\r\\n" */
} cli_config_t;

/** Internal state. Allocate one per interface; do not touch the fields. */
struct cli {
    cli_config_t cfg;
    char prompt[CLI_MAX_PROMPT_LEN + 1u];

    char line[CLI_MAX_LINE_LEN + 1u];
    size_t len;
    size_t cursor;
    bool overflow;

    uint8_t esc_state;
    uint8_t esc_len;
    char esc_buf[CLI_MAX_ESC_LEN];
    bool skip_lf;

#if CLI_HISTORY_DEPTH > 0u
    char hist[CLI_HISTORY_DEPTH][CLI_MAX_LINE_LEN + 1u];
    char hist_stash[CLI_MAX_LINE_LEN + 1u];
    uint8_t hist_count;
    uint8_t hist_head;
    int16_t hist_pos;
#endif

    char parse_buf[CLI_MAX_LINE_LEN + 1u];
    const char *tokens[CLI_MAX_TOKENS];
    size_t token_count;
    cli_args_t args;

    bool started;
    bool executing;
};

/** @brief Returns a configuration pre-filled with the library defaults. */
cli_config_t cli_config_default(void);

/**
 * @brief Initialises an instance and prints the first prompt.
 * @param cli  instance storage owned by the caller
 * @param cfg  configuration; copied, but the command table must outlive @p cli
 */
void cli_init(cli_t *cli, const cli_config_t *cfg);

/** @brief Drains the character source, processing up to CLI_POLL_BUDGET bytes. */
void cli_poll(cli_t *cli);

/** @brief Feeds a single received byte into the CLI. */
void cli_feed(cli_t *cli, char c);

/** @brief Feeds a block of received bytes into the CLI. */
void cli_feed_buf(cli_t *cli, const char *data, size_t len);

/**
 * @brief Replaces the prompt.
 *
 * When called outside of a command handler the current line is redrawn with
 * the new prompt; when called from a handler the new prompt simply appears
 * with the next input line.
 */
void cli_set_prompt(cli_t *cli, const char *prompt);

/**
 * @brief Executes one line as if it had been typed.
 *
 * Bypasses the line editor and history. Must not be called from within a
 * command handler, because it reuses the instance's parsing buffers.
 * @return a cli_status_t, or the handler's return value
 */
int cli_exec(cli_t *cli, const char *line);

/** @brief Writes a NUL terminated string, honouring the crlf setting. */
void cli_write(cli_t *cli, const char *s);

/** @brief Writes @p len bytes, honouring the crlf setting. */
void cli_write_len(cli_t *cli, const char *s, size_t len);

/** @brief Writes a single character, honouring the crlf setting. */
void cli_putc(cli_t *cli, char c);

/** @brief Writes a signed decimal number. */
void cli_write_int(cli_t *cli, int32_t value);

/** @brief Writes an unsigned number in the given base (2..16). */
void cli_write_uint(cli_t *cli, uint32_t value, unsigned base);

#if CLI_ENABLE_PRINTF
/** @brief printf style output, limited to CLI_PRINTF_BUF_LEN bytes per call. */
int cli_printf(cli_t *cli, const char *fmt, ...);
/** @brief va_list flavour of cli_printf(). */
int cli_vprintf(cli_t *cli, const char *fmt, va_list ap);
#endif

/** @brief Prints the command overview, as the `help` builtin does. */
void cli_print_help(cli_t *cli);

/**
 * @brief Prints the usage line generated from a command's argument spec.
 * @param path full invocation path including the command name (e.g. "led on");
 *             NULL falls back to cmd->name
 */
void cli_print_usage(cli_t *cli, const cli_cmd_t *cmd, const char *path);

/** @name Argument accessors
 * Each accessor returns @p fallback when the index is out of range or the
 * stored value has a different type.
 * @{ */
bool cli_arg_present(const cli_args_t *args, size_t index);
int32_t cli_arg_int(const cli_args_t *args, size_t index, int32_t fallback);
uint32_t cli_arg_uint(const cli_args_t *args, size_t index, uint32_t fallback);
const char *cli_arg_str(const cli_args_t *args, size_t index, const char *fallback);
bool cli_arg_bool(const cli_args_t *args, size_t index, bool fallback);
int cli_arg_enum(const cli_args_t *args, size_t index, int fallback);
#if CLI_ENABLE_FLOAT
float cli_arg_float(const cli_args_t *args, size_t index, float fallback);
#endif
/** @} */

#ifdef __cplusplus
}
#endif
