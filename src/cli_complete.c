/**
 * @file cli_complete.c
 * @brief TAB completion for commands, subcommands and enum arguments.
 */
#include <string.h>

#include "cli_internal.h"

/** Terminal width assumed when listing candidates in columns. */
#define CLI_LIST_WIDTH 72u

typedef struct {
    const char *p;
    size_t len;
} word_t;

static bool name_eq(const char *name, const word_t *w) {
    return (name != NULL) && (strlen(name) == w->len) &&
           (memcmp(name, w->p, w->len) == 0);
}

static const cli_cmd_t *lookup_word(const cli_cmd_t *table, size_t count, const word_t *w) {
    size_t i;

    if (table == NULL) {
        return NULL;
    }
    for (i = 0u; i < count; i++) {
        if (name_eq(table[i].name, w)) {
            return &table[i];
        }
    }
    return NULL;
}

static const cli_cmd_t *lookup_word_top(const cli_t *cli, const word_t *w) {
    const cli_cmd_t *cmd = lookup_word(cli->cfg.commands, cli->cfg.command_count, w);

#if CLI_ENABLE_BUILTINS
    if (cmd == NULL) {
        size_t count = 0u;
        const cli_cmd_t *table = cli_builtin_table(&count);

        cmd = lookup_word(table, count, w);
    }
#endif
    return cmd;
}

/** Splits line[0..limit) into words. Returns the total count of words found. */
static size_t scan_words(const char *line, size_t limit, word_t *out, size_t max) {
    size_t i = 0u;
    size_t n = 0u;

    while (i < limit) {
        size_t start;

        while ((i < limit) && (line[i] == ' ')) {
            i++;
        }
        if (i >= limit) {
            break;
        }
        start = i;
        while ((i < limit) && (line[i] != ' ')) {
            i++;
        }
        if (n < max) {
            out[n].p = &line[start];
            out[n].len = i - start;
        }
        n++;
    }
    return n;
}

static void add_candidate(const char **cands, size_t *n, const char *name,
                          const char *prefix, size_t prefix_len) {
    if ((name == NULL) || (*n >= (size_t)CLI_MAX_COMPLETIONS)) {
        return;
    }
    if (strlen(name) < prefix_len) {
        return;
    }
    if ((prefix_len != 0u) && (memcmp(name, prefix, prefix_len) != 0)) {
        return;
    }
    cands[*n] = name;
    (*n)++;
}

static void add_table(const char **cands, size_t *n, const cli_cmd_t *table,
                      size_t count, const char *prefix, size_t prefix_len) {
    size_t i;

    for (i = 0u; i < count; i++) {
        add_candidate(cands, n, table[i].name, prefix, prefix_len);
    }
}

/** Length of the longest common prefix of all candidates. */
static size_t common_prefix_len(const char **cands, size_t n) {
    size_t len = strlen(cands[0]);
    size_t i;

    for (i = 1u; i < n; i++) {
        size_t k = 0u;

        while ((k < len) && (cands[i][k] != '\0') && (cands[i][k] == cands[0][k])) {
            k++;
        }
        len = k;
    }
    return len;
}

static void list_candidates(cli_t *cli, const char **cands, size_t n) {
    size_t col = 0u;
    size_t i;

    cli_write(cli, "\n");
    for (i = 0u; i < n; i++) {
        size_t w = strlen(cands[i]) + 2u;

        if ((col != 0u) && ((col + w) > (size_t)CLI_LIST_WIDTH)) {
            cli_write(cli, "\n");
            col = 0u;
        }
        cli_write(cli, cands[i]);
        cli_write(cli, "  ");
        col += w;
    }
    cli_write(cli, "\n");
}

/** Collects the candidate set valid at word @p index. */
static size_t collect(cli_t *cli, const word_t *words, size_t index,
                      const char *prefix, size_t prefix_len,
                      const char **cands) {
    size_t n = 0u;
    const cli_cmd_t *node;
    size_t k;

    if (index == 0u) {
        add_table(cands, &n, cli->cfg.commands, cli->cfg.command_count,
                  prefix, prefix_len);
#if CLI_ENABLE_BUILTINS
        {
            size_t count = 0u;
            const cli_cmd_t *table = cli_builtin_table(&count);

            add_table(cands, &n, table, count, prefix, prefix_len);
        }
#endif
        return n;
    }

    node = lookup_word_top(cli, &words[0]);
    k = 1u;
    while ((node != NULL) && (k < index) &&
           (node->subcmds != NULL) && (node->subcmd_count > 0u)) {
        const cli_cmd_t *sub = lookup_word(node->subcmds, node->subcmd_count, &words[k]);

        if (sub == NULL) {
            node = NULL;
            break;
        }
        node = sub;
        k++;
    }
    if (node == NULL) {
        return 0u;
    }

    if ((k == index) && (node->subcmds != NULL) && (node->subcmd_count > 0u)) {
        add_table(cands, &n, node->subcmds, node->subcmd_count, prefix, prefix_len);
        return n;
    }

    if ((node->args != NULL) && (node->arg_count > 0u)) {
        size_t pos = index - k;
        const cli_arg_spec_t *spec = NULL;

        if (pos < node->arg_count) {
            spec = &node->args[pos];
        } else if (node->args[node->arg_count - 1u].variadic) {
            spec = &node->args[node->arg_count - 1u];
        } else {
            spec = NULL;
        }
        if ((spec != NULL) && (spec->type == CLI_ARG_ENUM) &&
            (spec->enum_values != NULL)) {
            size_t i;

            for (i = 0u; spec->enum_values[i] != NULL; i++) {
                add_candidate(cands, &n, spec->enum_values[i], prefix, prefix_len);
            }
        }
    }
    return n;
}

void cli_complete(cli_t *cli) {
    const char *cands[CLI_MAX_COMPLETIONS];
    word_t words[CLI_MAX_TOKENS];
    size_t word_start = cli->cursor;
    size_t prefix_len;
    size_t index;
    size_t n;

    while ((word_start > 0u) && (cli->line[word_start - 1u] != ' ')) {
        word_start--;
    }
    prefix_len = cli->cursor - word_start;
    index = scan_words(cli->line, word_start, words, (size_t)CLI_MAX_TOKENS);

    if (index >= (size_t)CLI_MAX_TOKENS) {
        return;
    }

    n = collect(cli, words, index, &cli->line[word_start], prefix_len, cands);

    if (n == 0u) {
        if (cli->cfg.echo) {
            cli_putc(cli, '\a');
        }
        return;
    }
    if (n == 1u) {
        const char *rest = cands[0] + prefix_len;

        (void)cli_edit_insert(cli, rest, strlen(rest));
        (void)cli_edit_insert(cli, " ", 1u);
        return;
    }

    {
        size_t lcp = common_prefix_len(cands, n);

        if (lcp > prefix_len) {
            (void)cli_edit_insert(cli, cands[0] + prefix_len, lcp - prefix_len);
        } else {
            list_candidates(cli, cands, n);
            cli_edit_refresh(cli);
        }
    }
}
