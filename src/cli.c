/**
 * @file cli.c
 * @brief Instance lifecycle, output helpers and command dispatch.
 */
#include <string.h>

#include "cli_internal.h"

#if CLI_ENABLE_PRINTF
#include <stdio.h>
#endif

/** Longest command path ("cmd sub sub") rendered in diagnostics. */
#define CLI_PATH_LEN 64u

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

void cli_putc(cli_t *cli, char c) {
    if (cli->cfg.putc_fn == NULL) {
        return;
    }
    if ((c == '\n') && cli->cfg.crlf) {
        cli->cfg.putc_fn(cli->cfg.io_ctx, '\r');
    }
    cli->cfg.putc_fn(cli->cfg.io_ctx, c);
}

void cli_write_len(cli_t *cli, const char *s, size_t len) {
    size_t i;

    if (s == NULL) {
        return;
    }
    for (i = 0u; i < len; i++) {
        cli_putc(cli, s[i]);
    }
}

void cli_write(cli_t *cli, const char *s) {
    if (s == NULL) {
        return;
    }
    cli_write_len(cli, s, strlen(s));
}

void cli_write_uint(cli_t *cli, uint32_t value, unsigned base) {
    char buf[33];
    size_t i = sizeof(buf);

    if ((base < 2u) || (base > 16u)) {
        base = 10u;
    }
    do {
        uint32_t digit = value % base;

        i--;
        buf[i] = (char)((digit < 10u) ? ('0' + digit) : ('a' + digit - 10u));
        value /= base;
    } while (value != 0u);

    cli_write_len(cli, &buf[i], sizeof(buf) - i);
}

void cli_write_int(cli_t *cli, int32_t value) {
    uint32_t mag;

    if (value < 0) {
        cli_putc(cli, '-');
        mag = (value == INT32_MIN) ? 2147483648u : (uint32_t)(-value);
    } else {
        mag = (uint32_t)value;
    }
    cli_write_uint(cli, mag, 10u);
}

#if CLI_ENABLE_PRINTF
int cli_vprintf(cli_t *cli, const char *fmt, va_list ap) {
    char buf[CLI_PRINTF_BUF_LEN];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (n < 0) {
        return n;
    }
    if ((size_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    cli_write_len(cli, buf, (size_t)n);
    return n;
}

int cli_printf(cli_t *cli, const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = cli_vprintf(cli, fmt, ap);
    va_end(ap);
    return n;
}
#endif /* CLI_ENABLE_PRINTF */

void cli_error_prefix(cli_t *cli) {
    cli_write(cli, "error: ");
}

const char *cli_type_name(cli_arg_type_t type) {
    switch (type) {
    case CLI_ARG_INT:   return "integer";
    case CLI_ARG_UINT:  return "unsigned integer";
    case CLI_ARG_HEX:   return "hex value";
    case CLI_ARG_FLOAT: return "float";
    case CLI_ARG_STR:   return "string";
    case CLI_ARG_BOOL:  return "boolean";
    case CLI_ARG_ENUM:  return "choice";
    default:            return "value";
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void str_copy(char *dst, size_t size, const char *src) {
    size_t i = 0u;

    if (size == 0u) {
        return;
    }
    if (src != NULL) {
        for (; (i < (size - 1u)) && (src[i] != '\0'); i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

cli_config_t cli_config_default(void) {
    cli_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.prompt = CLI_DEFAULT_PROMPT;
    cfg.echo = true;
    cfg.crlf = true;
    return cfg;
}

void cli_init(cli_t *cli, const cli_config_t *cfg) {
    memset(cli, 0, sizeof(*cli));
    cli->cfg = *cfg;
    str_copy(cli->prompt, sizeof(cli->prompt),
             (cfg->prompt != NULL) ? cfg->prompt : CLI_DEFAULT_PROMPT);
    cli_edit_reset(cli);
#if CLI_HISTORY_DEPTH > 0u
    cli->hist_pos = -1;
#endif
    cli->started = true;
    cli_draw_prompt(cli);
}

void cli_set_prompt(cli_t *cli, const char *prompt) {
    str_copy(cli->prompt, sizeof(cli->prompt),
             (prompt != NULL) ? prompt : CLI_DEFAULT_PROMPT);
    if (!cli->executing) {
        cli_edit_refresh(cli);
    }
}

void cli_feed(cli_t *cli, char c) {
    cli_edit_feed(cli, c);
}

void cli_feed_buf(cli_t *cli, const char *data, size_t len) {
    size_t i;

    for (i = 0u; i < len; i++) {
        cli_edit_feed(cli, data[i]);
    }
}

void cli_poll(cli_t *cli) {
    unsigned budget = CLI_POLL_BUDGET;

    if (cli->cfg.getc_fn == NULL) {
        return;
    }
    for (;;) {
        int c = cli->cfg.getc_fn(cli->cfg.io_ctx);

        if (c < 0) {
            break;
        }
        cli_edit_feed(cli, (char)c);

        if (CLI_POLL_BUDGET != 0u) {
            budget--;
            if (budget == 0u) {
                break;
            }
        }
    }
}

void cli_draw_prompt(cli_t *cli) {
    if (!cli->cfg.echo) {
        return;
    }
    cli_write(cli, cli->prompt);
    cli_write_len(cli, cli->line, cli->len);
}

/* ------------------------------------------------------------------ */
/* Lookup and dispatch                                                 */
/* ------------------------------------------------------------------ */

const cli_cmd_t *cli_lookup(const cli_cmd_t *table, size_t count, const char *name) {
    size_t i;

    if ((table == NULL) || (name == NULL)) {
        return NULL;
    }
    for (i = 0u; i < count; i++) {
        if ((table[i].name != NULL) && (strcmp(table[i].name, name) == 0)) {
            return &table[i];
        }
    }
    return NULL;
}

const cli_cmd_t *cli_lookup_top(const cli_t *cli, const char *name) {
    const cli_cmd_t *cmd = cli_lookup(cli->cfg.commands, cli->cfg.command_count, name);

#if CLI_ENABLE_BUILTINS
    if (cmd == NULL) {
        size_t count = 0u;
        const cli_cmd_t *table = cli_builtin_table(&count);

        cmd = cli_lookup(table, count, name);
    }
#endif
    return cmd;
}

static void path_append(char *path, const char *name) {
    size_t used = strlen(path);

    if (used != 0u) {
        if (used >= (CLI_PATH_LEN - 1u)) {
            return;
        }
        path[used] = ' ';
        used++;
        path[used] = '\0';
    }
    str_copy(&path[used], CLI_PATH_LEN - used, name);
}

static void list_subcommands(cli_t *cli, const cli_cmd_t *cmd) {
    size_t i;

    cli_write(cli, "subcommands:");
    for (i = 0u; i < cmd->subcmd_count; i++) {
        cli_write(cli, " ");
        cli_write(cli, cmd->subcmds[i].name);
    }
    cli_write(cli, "\n");
}

static int cli_dispatch(cli_t *cli) {
    char path[CLI_PATH_LEN];
    const cli_cmd_t *target;
    size_t idx = 1u;
    int rc;

    target = cli_lookup_top(cli, cli->tokens[0]);
    if (target == NULL) {
        cli_error_prefix(cli);
        cli_write(cli, "unknown command '");
        cli_write(cli, cli->tokens[0]);
        cli_write(cli, "'\n");
#if CLI_ENABLE_BUILTINS
        cli_write(cli, "type 'help' for a list of commands\n");
#endif
        return CLI_ERR_UNKNOWN_CMD;
    }

    path[0] = '\0';
    path_append(path, target->name);

    while ((target->subcmds != NULL) && (target->subcmd_count > 0u)) {
        const cli_cmd_t *sub = NULL;

        if (idx < cli->token_count) {
            sub = cli_lookup(target->subcmds, target->subcmd_count, cli->tokens[idx]);
        }
        if (sub == NULL) {
            if (target->handler != NULL) {
                break; /* remaining tokens are arguments of this command */
            }
            cli_error_prefix(cli);
            if (idx < cli->token_count) {
                cli_write(cli, "unknown subcommand '");
                cli_write(cli, cli->tokens[idx]);
                cli_write(cli, "' for '");
                cli_write(cli, path);
                cli_write(cli, "'\n");
            } else {
                cli_write(cli, "'");
                cli_write(cli, path);
                cli_write(cli, "' requires a subcommand\n");
            }
            list_subcommands(cli, target);
            return CLI_ERR_UNKNOWN_SUB;
        }
        target = sub;
        path_append(path, target->name);
        idx++;
    }

    if (target->handler == NULL) {
        cli_error_prefix(cli);
        cli_write(cli, "'");
        cli_write(cli, path);
        cli_write(cli, "' cannot be executed directly\n");
        if ((target->subcmds != NULL) && (target->subcmd_count > 0u)) {
            list_subcommands(cli, target);
        }
        return CLI_ERR_UNKNOWN_SUB;
    }

    rc = cli_build_args(cli, target, idx, path);
    if (rc != CLI_OK) {
        return rc;
    }

    rc = target->handler(cli, &cli->args, cli->cfg.user);
    if (rc != 0) {
        cli_error_prefix(cli);
        cli_write(cli, "'");
        cli_write(cli, path);
        cli_write(cli, "' returned ");
        cli_write_int(cli, (int32_t)rc);
        cli_write(cli, "\n");
    }
    return rc;
}

static int cli_run_line(cli_t *cli) {
    int rc = cli_tokenize(cli);

    switch (rc) {
    case CLI_OK:
        break;
    case CLI_ERR_EMPTY:
        return CLI_OK;
    case CLI_ERR_PARSE:
        cli_error_prefix(cli);
        cli_write(cli, "unterminated quote\n");
        return rc;
    default:
        cli_error_prefix(cli);
        cli_write(cli, "line too complex\n");
        return rc;
    }

    cli->executing = true;
    rc = cli_dispatch(cli);
    cli->executing = false;
    return rc;
}

int cli_exec(cli_t *cli, const char *line) {
    str_copy(cli->line, sizeof(cli->line), line);
    cli->len = strlen(cli->line);
    cli->cursor = cli->len;

    return cli_run_line(cli);
}

void cli_submit_line(cli_t *cli) {
    cli_write(cli, "\n");

    if (cli->overflow) {
        cli_error_prefix(cli);
        cli_write(cli, "line too long\n");
        cli->overflow = false;
        cli_edit_reset(cli);
        cli_draw_prompt(cli);
        return;
    }

#if CLI_HISTORY_DEPTH > 0u
    cli_hist_push(cli, cli->line);
#endif

    (void)cli_run_line(cli);

    cli_edit_reset(cli);
    cli_draw_prompt(cli);
}

/* ------------------------------------------------------------------ */
/* Argument accessors                                                  */
/* ------------------------------------------------------------------ */

static const cli_arg_t *arg_at(const cli_args_t *args, size_t index, cli_arg_type_t type) {
    if ((args == NULL) || (index >= args->count)) {
        return NULL;
    }
    if (args->v[index].type != type) {
        return NULL;
    }
    return &args->v[index];
}

bool cli_arg_present(const cli_args_t *args, size_t index) {
    return (args != NULL) && (index < args->count);
}

int32_t cli_arg_int(const cli_args_t *args, size_t index, int32_t fallback) {
    const cli_arg_t *a = arg_at(args, index, CLI_ARG_INT);

    return (a != NULL) ? a->val.i : fallback;
}

uint32_t cli_arg_uint(const cli_args_t *args, size_t index, uint32_t fallback) {
    const cli_arg_t *a = arg_at(args, index, CLI_ARG_UINT);

    if (a == NULL) {
        a = arg_at(args, index, CLI_ARG_HEX);
    }
    return (a != NULL) ? a->val.u : fallback;
}

const char *cli_arg_str(const cli_args_t *args, size_t index, const char *fallback) {
    if ((args == NULL) || (index >= args->count)) {
        return fallback;
    }
    return args->v[index].raw;
}

bool cli_arg_bool(const cli_args_t *args, size_t index, bool fallback) {
    const cli_arg_t *a = arg_at(args, index, CLI_ARG_BOOL);

    return (a != NULL) ? a->val.b : fallback;
}

int cli_arg_enum(const cli_args_t *args, size_t index, int fallback) {
    const cli_arg_t *a = arg_at(args, index, CLI_ARG_ENUM);

    return (a != NULL) ? a->val.e : fallback;
}

#if CLI_ENABLE_FLOAT
float cli_arg_float(const cli_args_t *args, size_t index, float fallback) {
    const cli_arg_t *a = arg_at(args, index, CLI_ARG_FLOAT);

    return (a != NULL) ? a->val.f : fallback;
}
#endif
