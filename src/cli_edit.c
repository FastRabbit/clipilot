/**
 * @file cli_edit.c
 * @brief Line editing, escape sequence decoding and command history.
 */
#include <string.h>

#include "cli_internal.h"

#define CLI_CH_CTRL_A 0x01
#define CLI_CH_CTRL_B 0x02
#define CLI_CH_CTRL_C 0x03
#define CLI_CH_CTRL_D 0x04
#define CLI_CH_CTRL_E 0x05
#define CLI_CH_CTRL_F 0x06
#define CLI_CH_BS     0x08
#define CLI_CH_TAB    0x09
#define CLI_CH_LF     0x0A
#define CLI_CH_CTRL_K 0x0B
#define CLI_CH_CTRL_L 0x0C
#define CLI_CH_CR     0x0D
#define CLI_CH_CTRL_N 0x0E
#define CLI_CH_CTRL_P 0x10
#define CLI_CH_CTRL_U 0x15
#define CLI_CH_CTRL_W 0x17
#define CLI_CH_ESC    0x1B
#define CLI_CH_DEL    0x7F

/* ------------------------------------------------------------------ */
/* Buffer and screen primitives                                        */
/* ------------------------------------------------------------------ */

void cli_edit_reset(cli_t *cli) {
    cli->line[0] = '\0';
    cli->len = 0u;
    cli->cursor = 0u;
    cli->overflow = false;
    cli->esc_state = CLI_ESC_NONE;
    cli->esc_len = 0u;
#if CLI_HISTORY_DEPTH > 0u
    cli->hist_pos = -1;
#endif
}

/** Emits CSI <n> <final>, omitting the parameter when it is 1. */
static void csi_move(cli_t *cli, size_t n, char final) {
    if (n == 0u) {
        return;
    }
    cli_write(cli, "\x1b[");
    if (n > 1u) {
        cli_write_uint(cli, (uint32_t)n, 10u);
    }
    cli_putc(cli, final);
}

void cli_edit_refresh(cli_t *cli) {
    if (!cli->cfg.echo) {
        return;
    }
    cli_write(cli, "\r\x1b[K");
    cli_write(cli, cli->prompt);
    cli_write_len(cli, cli->line, cli->len);
    csi_move(cli, cli->len - cli->cursor, 'D');
}

size_t cli_edit_insert(cli_t *cli, const char *text, size_t len) {
    size_t room = (size_t)CLI_MAX_LINE_LEN - cli->len;
    size_t n = (len > room) ? room : len;
    bool at_end = (cli->cursor == cli->len);

    if (n < len) {
        cli->overflow = true;
    }
    if (n == 0u) {
        if (cli->cfg.echo) {
            cli_putc(cli, '\a');
        }
        return 0u;
    }
    if (!at_end) {
        memmove(&cli->line[cli->cursor + n], &cli->line[cli->cursor],
                cli->len - cli->cursor);
    }
    memcpy(&cli->line[cli->cursor], text, n);
    cli->len += n;
    cli->cursor += n;
    cli->line[cli->len] = '\0';

    if (cli->cfg.echo) {
        if (at_end) {
            cli_write_len(cli, text, n);
        } else {
            cli_edit_refresh(cli);
        }
    }
    return n;
}

void cli_edit_set_line(cli_t *cli, const char *text) {
    size_t len = (text != NULL) ? strlen(text) : 0u;

    if (len > (size_t)CLI_MAX_LINE_LEN) {
        len = (size_t)CLI_MAX_LINE_LEN;
    }
    if (len != 0u) {
        memcpy(cli->line, text, len);
    }
    cli->len = len;
    cli->cursor = len;
    cli->line[len] = '\0';
    cli->overflow = false;
    cli_edit_refresh(cli);
}

/** Removes @p count characters ending at the cursor. */
static void delete_before(cli_t *cli, size_t count) {
    bool at_end;

    if ((count == 0u) || (cli->cursor == 0u)) {
        return;
    }
    if (count > cli->cursor) {
        count = cli->cursor;
    }
    at_end = (cli->cursor == cli->len);

    memmove(&cli->line[cli->cursor - count], &cli->line[cli->cursor],
            cli->len - cli->cursor);
    cli->len -= count;
    cli->cursor -= count;
    cli->line[cli->len] = '\0';
    if (cli->len == 0u) {
        cli->overflow = false;
    }

    if (cli->cfg.echo) {
        if (at_end && (count == 1u)) {
            cli_write(cli, "\b \b");
        } else {
            cli_edit_refresh(cli);
        }
    }
}

/** Removes the character under the cursor. */
static void delete_at(cli_t *cli) {
    if (cli->cursor >= cli->len) {
        return;
    }
    memmove(&cli->line[cli->cursor], &cli->line[cli->cursor + 1u],
            cli->len - cli->cursor - 1u);
    cli->len--;
    cli->line[cli->len] = '\0';
    cli_edit_refresh(cli);
}

static void kill_to_end(cli_t *cli) {
    if (cli->cursor >= cli->len) {
        return;
    }
    cli->len = cli->cursor;
    cli->line[cli->len] = '\0';
    if (cli->cfg.echo) {
        cli_write(cli, "\x1b[K");
    }
}

static void kill_prev_word(cli_t *cli) {
    size_t pos = cli->cursor;

    while ((pos > 0u) && (cli->line[pos - 1u] == ' ')) {
        pos--;
    }
    while ((pos > 0u) && (cli->line[pos - 1u] != ' ')) {
        pos--;
    }
    delete_before(cli, cli->cursor - pos);
}

static void move_left(cli_t *cli) {
    if (cli->cursor == 0u) {
        return;
    }
    cli->cursor--;
    if (cli->cfg.echo) {
        csi_move(cli, 1u, 'D');
    }
}

static void move_right(cli_t *cli) {
    if (cli->cursor >= cli->len) {
        return;
    }
    cli->cursor++;
    if (cli->cfg.echo) {
        csi_move(cli, 1u, 'C');
    }
}

static void move_home(cli_t *cli) {
    if (cli->cursor == 0u) {
        return;
    }
    if (cli->cfg.echo) {
        csi_move(cli, cli->cursor, 'D');
    }
    cli->cursor = 0u;
}

static void move_end(cli_t *cli) {
    if (cli->cursor >= cli->len) {
        return;
    }
    if (cli->cfg.echo) {
        csi_move(cli, cli->len - cli->cursor, 'C');
    }
    cli->cursor = cli->len;
}

/* ------------------------------------------------------------------ */
/* History                                                             */
/* ------------------------------------------------------------------ */

#if CLI_HISTORY_DEPTH > 0u

size_t cli_hist_count(const cli_t *cli) {
    return cli->hist_count;
}

const char *cli_hist_get(const cli_t *cli, size_t index) {
    size_t slot;

    if (index >= cli->hist_count) {
        return NULL;
    }
    slot = ((size_t)cli->hist_head + (size_t)CLI_HISTORY_DEPTH - 1u - index)
           % (size_t)CLI_HISTORY_DEPTH;
    return cli->hist[slot];
}

void cli_hist_push(cli_t *cli, const char *line) {
    const char *newest;

    cli->hist_pos = -1;

    if ((line == NULL) || (line[0] == '\0')) {
        return;
    }
    newest = cli_hist_get(cli, 0u);
    if ((newest != NULL) && (strcmp(newest, line) == 0)) {
        return;
    }
    memset(cli->hist[cli->hist_head], 0, sizeof(cli->hist[0]));
    strncpy(cli->hist[cli->hist_head], line, sizeof(cli->hist[0]) - 1u);
    cli->hist_head = (uint8_t)((cli->hist_head + 1u) % CLI_HISTORY_DEPTH);
    if (cli->hist_count < CLI_HISTORY_DEPTH) {
        cli->hist_count++;
    }
}

/** @param older true moves towards older entries (up arrow). */
static void hist_browse(cli_t *cli, bool older) {
    int pos = cli->hist_pos;

    if (cli->hist_count == 0u) {
        return;
    }
    if (older) {
        if ((pos + 1) >= (int)cli->hist_count) {
            return;
        }
        if (pos < 0) {
            memcpy(cli->hist_stash, cli->line, cli->len + 1u);
        }
        pos++;
    } else {
        if (pos < 0) {
            return;
        }
        pos--;
    }
    cli_edit_set_line(cli, (pos < 0) ? cli->hist_stash : cli_hist_get(cli, (size_t)pos));
    cli->hist_pos = (int16_t)pos;
}

#endif /* CLI_HISTORY_DEPTH > 0 */

static void history_prev(cli_t *cli) {
#if CLI_HISTORY_DEPTH > 0u
    hist_browse(cli, true);
#else
    (void)cli;
#endif
}

static void history_next(cli_t *cli) {
#if CLI_HISTORY_DEPTH > 0u
    hist_browse(cli, false);
#else
    (void)cli;
#endif
}

/* ------------------------------------------------------------------ */
/* Escape sequence handling                                            */
/* ------------------------------------------------------------------ */

static void handle_escape(cli_t *cli) {
    char final = cli->esc_buf[cli->esc_len - 1u];

    switch (final) {
    case 'A': history_prev(cli); break;
    case 'B': history_next(cli); break;
    case 'C': move_right(cli); break;
    case 'D': move_left(cli); break;
    case 'H': move_home(cli); break;
    case 'F': move_end(cli); break;
    case '~': {
        unsigned param = 0u;
        uint8_t i;

        for (i = 1u; i < (cli->esc_len - 1u); i++) {
            char c = cli->esc_buf[i];

            if ((c < '0') || (c > '9')) {
                break;
            }
            param = (param * 10u) + (unsigned)(c - '0');
        }
        switch (param) {
        case 1u: case 7u: move_home(cli); break;
        case 4u: case 8u: move_end(cli); break;
        case 3u: delete_at(cli); break;
        default: break;
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Byte dispatcher                                                     */
/* ------------------------------------------------------------------ */

void cli_edit_feed(cli_t *cli, char c) {
    if (cli->esc_state == CLI_ESC_GOT) {
        if ((c == '[') || (c == 'O')) {
            cli->esc_state = CLI_ESC_SEQ;
            cli->esc_len = 0u;
            cli->esc_buf[cli->esc_len] = c;
            cli->esc_len++;
        } else {
            cli->esc_state = CLI_ESC_NONE;
        }
        return;
    }
    if (cli->esc_state == CLI_ESC_SEQ) {
        if (cli->esc_len < (uint8_t)CLI_MAX_ESC_LEN) {
            cli->esc_buf[cli->esc_len] = c;
            cli->esc_len++;
        }
        if ((c >= 0x40) && (c <= 0x7E)) {
            handle_escape(cli);
            cli->esc_state = CLI_ESC_NONE;
        } else if (cli->esc_len >= (uint8_t)CLI_MAX_ESC_LEN) {
            cli->esc_state = CLI_ESC_NONE; /* runaway sequence */
        }
        return;
    }

    if (c == CLI_CH_LF) {
        if (cli->skip_lf) {
            cli->skip_lf = false;
            return;
        }
    } else {
        cli->skip_lf = false;
    }

    switch (c) {
    case CLI_CH_CR:
        cli->skip_lf = true;
        cli_submit_line(cli);
        break;
    case CLI_CH_LF:
        cli_submit_line(cli);
        break;
    case CLI_CH_ESC:
        cli->esc_state = CLI_ESC_GOT;
        break;
    case CLI_CH_TAB:
        cli_complete(cli);
        break;
    case CLI_CH_BS:
    case CLI_CH_DEL:
        delete_before(cli, 1u);
        break;
    case CLI_CH_CTRL_A:
        move_home(cli);
        break;
    case CLI_CH_CTRL_E:
        move_end(cli);
        break;
    case CLI_CH_CTRL_B:
        move_left(cli);
        break;
    case CLI_CH_CTRL_F:
        move_right(cli);
        break;
    case CLI_CH_CTRL_D:
        delete_at(cli);
        break;
    case CLI_CH_CTRL_K:
        kill_to_end(cli);
        break;
    case CLI_CH_CTRL_U:
        delete_before(cli, cli->cursor);
        break;
    case CLI_CH_CTRL_W:
        kill_prev_word(cli);
        break;
    case CLI_CH_CTRL_P:
        history_prev(cli);
        break;
    case CLI_CH_CTRL_N:
        history_next(cli);
        break;
    case CLI_CH_CTRL_L:
        if (cli->cfg.echo) {
            cli_write(cli, "\x1b[2J\x1b[H");
        }
        cli_edit_refresh(cli);
        break;
    case CLI_CH_CTRL_C:
        cli_write(cli, "^C\n");
        cli_edit_reset(cli);
        cli_draw_prompt(cli);
        break;
    default:
        if ((c >= 0x20) && (c < CLI_CH_DEL)) {
            (void)cli_edit_insert(cli, &c, 1u);
        }
        break;
    }
}
