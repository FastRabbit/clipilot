/**
 * @file cli_builtin.c
 * @brief Help/usage rendering and the built-in commands.
 */
#include <string.h>

#include "cli_internal.h"

/* ------------------------------------------------------------------ */
/* Usage rendering                                                     */
/* ------------------------------------------------------------------ */

static void write_arg_spec(cli_t *cli, const cli_arg_spec_t *spec) {
    const bool opt = spec->optional;

    cli_write(cli, opt ? "[" : "<");
    cli_write(cli, (spec->name != NULL) ? spec->name : "arg");
    if ((spec->type == CLI_ARG_ENUM) && (spec->enum_values != NULL)) {
        size_t i;

        cli_write(cli, ":");
        for (i = 0u; spec->enum_values[i] != NULL; i++) {
            if (i != 0u) {
                cli_write(cli, "|");
            }
            cli_write(cli, spec->enum_values[i]);
        }
    } else if (spec->type != CLI_ARG_STR) {
        cli_write(cli, ":");
        cli_write(cli, cli_type_name(spec->type));
    } else {
        /* plain strings need no type annotation */
    }
    if (spec->variadic) {
        cli_write(cli, "...");
    }
    cli_write(cli, opt ? "]" : ">");
}

void cli_print_usage(cli_t *cli, const cli_cmd_t *cmd, const char *path) {
    size_t i;

    cli_write(cli, "usage: ");
    cli_write(cli, (path != NULL) ? path : cmd->name);

    if ((cmd->subcmds != NULL) && (cmd->subcmd_count > 0u)) {
        cli_write(cli, (cmd->handler != NULL) ? " [<subcommand>]" : " <subcommand>");
    }
    for (i = 0u; i < cmd->arg_count; i++) {
        cli_write(cli, " ");
        write_arg_spec(cli, &cmd->args[i]);
    }
    cli_write(cli, "\n");

    for (i = 0u; i < cmd->arg_count; i++) {
        if (cmd->args[i].help != NULL) {
            cli_write(cli, "  ");
            write_arg_spec(cli, &cmd->args[i]);
            cli_write(cli, "  ");
            cli_write(cli, cmd->args[i].help);
            cli_write(cli, "\n");
        }
    }
}

static size_t widest_name(const cli_cmd_t *table, size_t count, size_t width) {
    size_t i;

    for (i = 0u; i < count; i++) {
        size_t len = (table[i].name != NULL) ? strlen(table[i].name) : 0u;

        if (len > width) {
            width = len;
        }
    }
    return width;
}

static void print_table(cli_t *cli, const cli_cmd_t *table, size_t count, size_t width) {
    size_t i;

    for (i = 0u; i < count; i++) {
        size_t len = strlen(table[i].name);
        size_t pad;

        cli_write(cli, "  ");
        cli_write(cli, table[i].name);
        for (pad = len; pad < (width + 2u); pad++) {
            cli_putc(cli, ' ');
        }
        cli_write(cli, (table[i].help != NULL) ? table[i].help : "");
        cli_write(cli, "\n");
    }
}

void cli_print_help(cli_t *cli) {
    size_t width = 0u;
#if CLI_ENABLE_BUILTINS
    size_t bcount = 0u;
    const cli_cmd_t *btable = cli_builtin_table(&bcount);
#endif

    width = widest_name(cli->cfg.commands, cli->cfg.command_count, width);
#if CLI_ENABLE_BUILTINS
    width = widest_name(btable, bcount, width);
#endif

    cli_write(cli, "commands:\n");
    print_table(cli, cli->cfg.commands, cli->cfg.command_count, width);
#if CLI_ENABLE_BUILTINS
    print_table(cli, btable, bcount, width);
#endif
}

/* ------------------------------------------------------------------ */
/* Built-in commands                                                   */
/* ------------------------------------------------------------------ */

#if CLI_ENABLE_BUILTINS

static int builtin_help(cli_t *cli, const cli_args_t *args, void *user) {
    char path[64];
    const cli_cmd_t *node;
    size_t i;

    (void)user;

    if (args->count == 0u) {
        cli_print_help(cli);
        cli_write(cli, "\ntype 'help <command>' for details\n");
        return 0;
    }

    node = cli_lookup_top(cli, args->v[0].raw);
    if (node == NULL) {
        cli_error_prefix(cli);
        cli_write(cli, "unknown command '");
        cli_write(cli, args->v[0].raw);
        cli_write(cli, "'\n");
        return 0;
    }
    strncpy(path, node->name, sizeof(path) - 1u);
    path[sizeof(path) - 1u] = '\0';

    for (i = 1u; i < args->count; i++) {
        const cli_cmd_t *sub = cli_lookup(node->subcmds, node->subcmd_count,
                                          args->v[i].raw);

        if (sub == NULL) {
            cli_error_prefix(cli);
            cli_write(cli, "unknown subcommand '");
            cli_write(cli, args->v[i].raw);
            cli_write(cli, "' for '");
            cli_write(cli, path);
            cli_write(cli, "'\n");
            return 0;
        }
        node = sub;
        if ((strlen(path) + 1u + strlen(node->name)) < sizeof(path)) {
            strcat(path, " ");
            strcat(path, node->name);
        }
    }

    if (node->help != NULL) {
        cli_write(cli, node->help);
        cli_write(cli, "\n");
    }
    cli_print_usage(cli, node, path);
    if ((node->subcmds != NULL) && (node->subcmd_count > 0u)) {
        size_t width = widest_name(node->subcmds, node->subcmd_count, 0u);

        cli_write(cli, "subcommands:\n");
        print_table(cli, node->subcmds, node->subcmd_count, width);
    }
    return 0;
}

#if CLI_HISTORY_DEPTH > 0u
static int builtin_history(cli_t *cli, const cli_args_t *args, void *user) {
    size_t count = cli_hist_count(cli);
    size_t i;

    (void)args;
    (void)user;

    for (i = count; i > 0u; i--) {
        const char *line = cli_hist_get(cli, i - 1u);

        cli_write(cli, "  ");
        cli_write_uint(cli, (uint32_t)(count - i + 1u), 10u);
        cli_write(cli, "  ");
        cli_write(cli, (line != NULL) ? line : "");
        cli_write(cli, "\n");
    }
    return 0;
}
#endif

static const cli_arg_spec_t help_args[] = {
    {
        .name = "command",
        .type = CLI_ARG_STR,
        .optional = true,
        .variadic = true,
        .enum_values = NULL,
        .help = "command path to describe"
    }
};

static const cli_cmd_t builtins[] = {
    {
        .name = "help",
        .help = "list commands or describe one",
        .subcmds = NULL,
        .subcmd_count = 0u,
        .args = help_args,
        .arg_count = sizeof(help_args) / sizeof(help_args[0]),
        .handler = builtin_help
    },
#if CLI_HISTORY_DEPTH > 0u
    {
        .name = "history",
        .help = "show recently entered lines",
        .subcmds = NULL,
        .subcmd_count = 0u,
        .args = NULL,
        .arg_count = 0u,
        .handler = builtin_history
    },
#endif
};

const cli_cmd_t *cli_builtin_table(size_t *count) {
    *count = sizeof(builtins) / sizeof(builtins[0]);
    return builtins;
}

#endif /* CLI_ENABLE_BUILTINS */
