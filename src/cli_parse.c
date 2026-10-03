/**
 * @file cli_parse.c
 * @brief Line tokenizer and typed argument conversion.
 */
#include "cli_internal.h"

/* ------------------------------------------------------------------ */
/* Tokenizer                                                           */
/* ------------------------------------------------------------------ */

static bool is_space(char c) {
    return (c == ' ') || (c == '\t');
}

static char unescape(char c) {
    switch (c) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    default:  return c;
    }
}

int cli_tokenize(cli_t *cli) {
    const char *src = cli->line;
    char *dst = cli->parse_buf;
    char *const end = cli->parse_buf + sizeof(cli->parse_buf);

    cli->token_count = 0u;

    while (*src != '\0') {
        char quote = '\0';

        while (is_space(*src)) {
            src++;
        }
        if (*src == '\0') {
            break;
        }
        if (cli->token_count >= CLI_MAX_TOKENS) {
            return CLI_ERR_OVERFLOW;
        }
        cli->tokens[cli->token_count] = dst;
        cli->token_count++;

        while (*src != '\0') {
            char c = *src;

            if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                    src++;
                    continue;
                }
            } else {
                if (is_space(c)) {
                    break;
                }
                if ((c == '"') || (c == '\'')) {
                    quote = c;
                    src++;
                    continue;
                }
            }
            if ((c == '\\') && (quote != '\'') && (src[1] != '\0')) {
                src++;
                c = unescape(*src);
            }
            if (dst >= (end - 1)) {
                return CLI_ERR_OVERFLOW;
            }
            *dst = c;
            dst++;
            src++;
        }
        if (quote != '\0') {
            return CLI_ERR_PARSE;
        }
        if (dst >= end) {
            return CLI_ERR_OVERFLOW;
        }
        *dst = '\0';
        dst++;
    }

    return (cli->token_count == 0u) ? CLI_ERR_EMPTY : CLI_OK;
}

/* ------------------------------------------------------------------ */
/* Scalar converters                                                   */
/* ------------------------------------------------------------------ */

static unsigned digit_val(char c) {
    if ((c >= '0') && (c <= '9')) {
        return (unsigned)(c - '0');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (unsigned)(c - 'a') + 10u;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (unsigned)(c - 'A') + 10u;
    }
    return 99u;
}

static bool parse_digits(const char *s, unsigned base, uint32_t *out) {
    uint32_t acc = 0u;
    bool any = false;

    for (; *s != '\0'; s++) {
        unsigned d;

        if (*s == '_') {
            continue; /* allow 1_000 style grouping */
        }
        d = digit_val(*s);
        if (d >= base) {
            return false;
        }
        if (acc > ((0xFFFFFFFFu - d) / base)) {
            return false; /* overflow */
        }
        acc = (acc * base) + d;
        any = true;
    }
    if (!any) {
        return false;
    }
    *out = acc;
    return true;
}

/** Consumes an optional 0x/0b/0o prefix and reports the resulting base. */
static const char *strip_base_prefix(const char *s, unsigned *base) {
    *base = 10u;
    if (s[0] == '0') {
        switch (s[1]) {
        case 'x': case 'X': *base = 16u; return s + 2;
        case 'b': case 'B': *base = 2u;  return s + 2;
        case 'o': case 'O': *base = 8u;  return s + 2;
        default: break;
        }
    }
    return s;
}

static bool parse_u32(const char *s, uint32_t *out) {
    unsigned base;

    if (*s == '+') {
        s++;
    }
    s = strip_base_prefix(s, &base);
    return parse_digits(s, base, out);
}

static bool parse_i32(const char *s, int32_t *out) {
    bool neg = false;
    unsigned base;
    uint32_t mag;

    if ((*s == '-') || (*s == '+')) {
        neg = (*s == '-');
        s++;
    }
    s = strip_base_prefix(s, &base);
    if (!parse_digits(s, base, &mag)) {
        return false;
    }
    if (neg) {
        if (mag > 2147483648u) {
            return false;
        }
        *out = (mag == 2147483648u) ? INT32_MIN : -(int32_t)mag;
    } else {
        if (mag > 2147483647u) {
            return false;
        }
        *out = (int32_t)mag;
    }
    return true;
}

static bool parse_hex(const char *s, uint32_t *out) {
    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        s += 2;
    }
    return parse_digits(s, 16u, out);
}

static bool str_ieq(const char *a, const char *b) {
    for (;; a++, b++) {
        char ca = *a;
        char cb = *b;

        if ((ca >= 'A') && (ca <= 'Z')) {
            ca = (char)(ca - 'A' + 'a');
        }
        if ((cb >= 'A') && (cb <= 'Z')) {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
    }
}

static bool parse_bool(const char *s, bool *out) {
    static const char *const yes[] = { "1", "on", "true", "yes", "y", "enable" };
    static const char *const no[] = { "0", "off", "false", "no", "n", "disable" };
    size_t i;

    for (i = 0u; i < (sizeof(yes) / sizeof(yes[0])); i++) {
        if (str_ieq(s, yes[i])) {
            *out = true;
            return true;
        }
    }
    for (i = 0u; i < (sizeof(no) / sizeof(no[0])); i++) {
        if (str_ieq(s, no[i])) {
            *out = false;
            return true;
        }
    }
    return false;
}

#if CLI_ENABLE_FLOAT
static bool parse_float(const char *s, float *out) {
    bool neg = false;
    bool any = false;
    float mant = 0.0f;

    if ((*s == '-') || (*s == '+')) {
        neg = (*s == '-');
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        mant = (mant * 10.0f) + (float)(*s - '0');
        any = true;
        s++;
    }
    if (*s == '.') {
        float scale = 0.1f;

        s++;
        while ((*s >= '0') && (*s <= '9')) {
            mant += (float)(*s - '0') * scale;
            scale *= 0.1f;
            any = true;
            s++;
        }
    }
    if (!any) {
        return false;
    }
    if ((*s == 'e') || (*s == 'E')) {
        bool eneg = false;
        int exp = 0;
        int i;

        s++;
        if ((*s == '-') || (*s == '+')) {
            eneg = (*s == '-');
            s++;
        }
        if ((*s < '0') || (*s > '9')) {
            return false;
        }
        while ((*s >= '0') && (*s <= '9')) {
            exp = (exp * 10) + (*s - '0');
            if (exp > 38) {
                return false;
            }
            s++;
        }
        for (i = 0; i < exp; i++) {
            mant = eneg ? (mant / 10.0f) : (mant * 10.0f);
        }
    }
    if (*s != '\0') {
        return false;
    }
    *out = neg ? -mant : mant;
    return true;
}
#endif /* CLI_ENABLE_FLOAT */

static int enum_index(const char *const *values, const char *tok) {
    int i;

    if (values == NULL) {
        return -1;
    }
    for (i = 0; values[i] != NULL; i++) {
        if (str_ieq(values[i], tok)) {
            return i;
        }
    }
    return -1;
}

bool cli_convert_token(const cli_arg_spec_t *spec, const char *tok, cli_arg_t *out) {
    out->type = spec->type;
    out->raw = tok;

    switch (spec->type) {
    case CLI_ARG_INT:
        return parse_i32(tok, &out->val.i);
    case CLI_ARG_UINT:
        return parse_u32(tok, &out->val.u);
    case CLI_ARG_HEX:
        return parse_hex(tok, &out->val.u);
    case CLI_ARG_BOOL:
        return parse_bool(tok, &out->val.b);
    case CLI_ARG_STR:
        out->val.s = tok;
        return true;
    case CLI_ARG_ENUM: {
        int idx = enum_index(spec->enum_values, tok);

        if (idx < 0) {
            return false;
        }
        out->val.e = idx;
        return true;
    }
#if CLI_ENABLE_FLOAT
    case CLI_ARG_FLOAT:
        return parse_float(tok, &out->val.f);
#endif
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Arity validation and argument vector construction                   */
/* ------------------------------------------------------------------ */

int cli_build_args(cli_t *cli, const cli_cmd_t *cmd, size_t first, const char *path) {
    const size_t given = cli->token_count - first;
    const size_t spec_count = (cmd->args != NULL) ? cmd->arg_count : 0u;
    bool variadic = (spec_count > 0u) && cmd->args[spec_count - 1u].variadic;
    size_t required = 0u;
    size_t maximum;
    size_t i;

    cli->args.count = 0u;

    for (i = 0u; i < spec_count; i++) {
        if (!cmd->args[i].optional) {
            required++;
        }
    }
    maximum = variadic ? (size_t)CLI_MAX_ARGS : spec_count;

    if (given > (size_t)CLI_MAX_ARGS) {
        cli_error_prefix(cli);
        cli_write(cli, "too many arguments\n");
        return CLI_ERR_OVERFLOW;
    }
    if ((given < required) || (given > maximum)) {
        cli_error_prefix(cli);
        if (spec_count == 0u) {
            cli_write(cli, "'");
            cli_write(cli, path);
            cli_write(cli, "' takes no arguments\n");
        } else {
            cli_write(cli, (given < required) ? "missing argument" : "too many arguments");
            cli_write(cli, "\n");
        }
        cli_print_usage(cli, cmd, path);
        return CLI_ERR_ARG_COUNT;
    }

    for (i = 0u; i < given; i++) {
        const cli_arg_spec_t *spec = &cmd->args[(i < spec_count) ? i : (spec_count - 1u)];

        if (!cli_convert_token(spec, cli->tokens[first + i], &cli->args.v[i])) {
            cli_error_prefix(cli);
            cli_write(cli, "argument ");
            cli_write_uint(cli, (uint32_t)(i + 1u), 10u);
            if (spec->name != NULL) {
                cli_write(cli, " <");
                cli_write(cli, spec->name);
                cli_write(cli, ">");
            }
            cli_write(cli, ": '");
            cli_write(cli, cli->tokens[first + i]);
            cli_write(cli, "' is not a valid ");
            cli_write(cli, cli_type_name(spec->type));
            if ((spec->type == CLI_ARG_ENUM) && (spec->enum_values != NULL)) {
                size_t k;

                cli_write(cli, " (expected one of:");
                for (k = 0u; spec->enum_values[k] != NULL; k++) {
                    cli_write(cli, " ");
                    cli_write(cli, spec->enum_values[k]);
                }
                cli_write(cli, ")");
            }
            cli_write(cli, "\n");
            return CLI_ERR_ARG_TYPE;
        }
        cli->args.count = i + 1u;
    }

    return CLI_OK;
}
