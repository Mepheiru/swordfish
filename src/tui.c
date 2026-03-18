#include "tui.h"
#include "fuzzy.h"
#include "main.h"

#include <curses.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define QUERY_HEIGHT  3
#define STATUS_HEIGHT 1

#define COL_PID_W   7
#define COL_NAME_W  20
#define COL_USER_W  12
#define COL_STATE_W 6
#define COL_RAM_W   8

#define PAIR_NORMAL    1
#define PAIR_HIGHLIGHT 2
#define PAIR_SELECTED  3
#define PAIR_QUERY     4
#define PAIR_HEADER    5
#define PAIR_STATUS    6
#define PAIR_ROOT      7
#define PAIR_DIM       8

static void tui_init_colors(void);
static void tui_init_windows(tui_state_t *s);
static void tui_resize(tui_state_t *s);
static void tui_cleanup_windows(tui_state_t *s);
static void tui_rebuild_rows(tui_state_t *s);
static void tui_render(tui_state_t *s);
static void tui_render_query(tui_state_t *s);
static void tui_render_list(tui_state_t *s);
static void tui_render_status(tui_state_t *s);
static void tui_render_confirm(tui_state_t *s);
static void tui_handle_input(tui_state_t *s, int ch);
static void tui_query_insert(tui_state_t *s, char c);
static void tui_query_backspace(tui_state_t *s);
static void tui_cursor_move(tui_state_t *s, int delta);
static void tui_scroll_to_cursor(tui_state_t *s);
static int  tui_list_height(void);

static void tui_init_colors(void) {
    start_color();
    use_default_colors();
    init_pair(PAIR_NORMAL,    COLOR_WHITE,  -1);
    init_pair(PAIR_HIGHLIGHT, COLOR_BLACK,  COLOR_CYAN);
    init_pair(PAIR_SELECTED,  COLOR_GREEN,  -1);
    init_pair(PAIR_QUERY,     COLOR_CYAN,   -1);
    init_pair(PAIR_HEADER,    COLOR_BLACK,  COLOR_WHITE);
    init_pair(PAIR_STATUS,    COLOR_BLACK,  COLOR_WHITE);
    init_pair(PAIR_ROOT,      COLOR_YELLOW, -1);
    init_pair(PAIR_DIM,       COLOR_WHITE,  -1);
}

static int tui_list_height(void) {
    return LINES - QUERY_HEIGHT - STATUS_HEIGHT - 1;
}

static void tui_init_windows(tui_state_t *s) {
    int list_h = LINES - QUERY_HEIGHT - STATUS_HEIGHT;
    s->win_query  = newwin(QUERY_HEIGHT,  COLS, 0, 0);
    s->win_list   = newwin(list_h,        COLS, QUERY_HEIGHT, 0);
    s->win_status = newwin(STATUS_HEIGHT, COLS, LINES - STATUS_HEIGHT, 0);
    keypad(s->win_list, TRUE);
}

static void tui_resize(tui_state_t *s) {
    tui_cleanup_windows(s);
    resizeterm(0, 0);
    tui_init_windows(s);
    tui_scroll_to_cursor(s);
}

static void tui_cleanup_windows(tui_state_t *s) {
    if (s->win_query) { delwin(s->win_query); s->win_query = NULL; }
    if (s->win_list) { delwin(s->win_list); s->win_list = NULL; }
    if (s->win_status) { delwin(s->win_status); s->win_status = NULL; }
}

static void tui_rebuild_rows(tui_state_t *s) {
    s->row_count = 0;

    for (int i = 0; i < s->proc_count && s->row_count < TUI_MAX_SELECT; ++i) {
        const process_info_t *p = &s->procs[i];

        if (s->query_len == 0) {
            s->rows[s->row_count++] = (tui_row_t){ .proc = p, .score = 0 };
            continue;
        }

        int name_score = fuzzy_score(s->query, p->name,    FUZZY_CTX_NAME);
        int cmd_score  = fuzzy_score(s->query, p->cmdline, FUZZY_CTX_CMDLINE);

        /* take best score — weighting is handled inside fuzzy_score */
        int score = -1;
        if (name_score >= 0)     score = name_score;
        else if (cmd_score >= 0) score = cmd_score;

        if (score >= 0)
            s->rows[s->row_count++] = (tui_row_t){ .proc = p, .score = score };
    }

    /* sort by score descending — best matches float to top */
    for (int i = 0; i < s->row_count - 1; ++i)
        for (int j = i + 1; j < s->row_count; ++j)
            if (s->rows[j].score > s->rows[i].score) {
                tui_row_t tmp = s->rows[i];
                s->rows[i] = s->rows[j];
                s->rows[j] = tmp;
            }

    if (s->cursor >= s->row_count)
        s->cursor = s->row_count > 0 ? s->row_count - 1 : 0;

    tui_scroll_to_cursor(s);
}

static void tui_render_query(tui_state_t *s) {
    WINDOW *w = s->win_query;
    werase(w);

    wattron(w, COLOR_PAIR(PAIR_QUERY) | A_BOLD);
    mvwprintw(w, 0, 0, "Swordfish fuzzy process finder");
    wattroff(w, COLOR_PAIR(PAIR_QUERY) | A_BOLD);

    mvwprintw(w, 1, 0, "  > ");
    wattron(w, COLOR_PAIR(PAIR_QUERY));
    waddstr(w, s->query);
    wattroff(w, COLOR_PAIR(PAIR_QUERY));

    wattron(w, A_DIM);
    mvwprintw(w, 2, 0, "  Tab: select   Enter: confirm   Esc/q: cancel");
    wattroff(w, A_DIM);

    wnoutrefresh(w);
}

static void tui_render_list(tui_state_t *s) {
    WINDOW *w = s->win_list;
    int height = tui_list_height();
    werase(w);

    wattron(w, COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvwprintw(w, 0, 0, " %-*s %-*s %-*s %-*s %s",
              COL_PID_W, "PID",
              COL_NAME_W, "NAME",
              COL_USER_W, "USER",
              COL_STATE_W, "STATE", "RAM");
    int used = 1 + COL_PID_W + 1 + COL_NAME_W + 1 + COL_USER_W + 1 + COL_STATE_W + 1 + COL_RAM_W;
    for (int x = used; x < COLS; ++x) waddch(w, ' ');
    wattroff(w, COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (s->row_count == 0) {
        wattron(w, A_DIM);
        mvwprintw(w, height / 2, (COLS - 16) / 2, "no matches found");
        wattroff(w, A_DIM);
        wnoutrefresh(w);
        return;
    }

    for (int i = 0; i < height && (s->scroll + i) < s->row_count; ++i) {
        int idx = s->scroll + i;
        const tui_row_t *row = &s->rows[idx];
        const process_info_t *p = row->proc;

        bool is_cursor = (idx == s->cursor);
        bool is_selected = s->selected[idx];
        bool is_root = (strcmp(p->owner, "root") == 0);
        int  row_y = i + 1;

        if (is_cursor) wattron(w, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        else if (is_selected) wattron(w, COLOR_PAIR(PAIR_SELECTED));
        else wattron(w, COLOR_PAIR(PAIR_NORMAL));

        mvwprintw(w, row_y, 0, "%s", is_selected ? "  " : " ");
        wprintw(w, "%-*d ", COL_PID_W, p->pid);

        if (is_root && !is_cursor) {
            wattroff(w, COLOR_PAIR(PAIR_NORMAL));
            wattron(w, COLOR_PAIR(PAIR_ROOT));
        }
        wprintw(w, "%-*.*s ", COL_NAME_W, COL_NAME_W, p->name);
        if (is_root && !is_cursor) {
            wattroff(w, COLOR_PAIR(PAIR_ROOT));
            wattron(w, is_selected ? COLOR_PAIR(PAIR_SELECTED) : COLOR_PAIR(PAIR_NORMAL));
        }

        wprintw(w, "%-*.*s ", COL_USER_W, COL_USER_W, p->owner);
        wprintw(w, "%-*c ", COL_STATE_W, p->status.state);
        if (p->ram > 0) wprintw(w, "%ldMB", p->ram);
        else wprintw(w, "  -  ");

        wclrtoeol(w);

        if (is_cursor) wattroff(w, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        else if (is_selected) wattroff(w, COLOR_PAIR(PAIR_SELECTED));
        else wattroff(w, COLOR_PAIR(PAIR_NORMAL));
    }

    if (s->row_count > height) {
        int bar_h = height * height / s->row_count;
        if (bar_h < 1) bar_h = 1;
        int bar_top = s->scroll * height / s->row_count;
        for (int y = 1; y <= height; ++y) {
            bool in_bar = (y - 1 >= bar_top && y - 1 < bar_top + bar_h);
            mvwaddch(w, y, COLS - 1, in_bar ? ACS_BLOCK : ACS_VLINE);
        }
    }

    wnoutrefresh(w);
}

static void tui_render_status(tui_state_t *s) {
    WINDOW *w = s->win_status;
    werase(w);

    int sel_count = 0;
    for (int i = 0; i < s->row_count; ++i)
        if (s->selected[i]) sel_count++;

    wattron(w, COLOR_PAIR(PAIR_STATUS) | A_BOLD);
    mvwprintw(w, 0, 0, "  %d/%d processes  |  %d selected", s->row_count, s->proc_count, sel_count);
    wclrtoeol(w);
    wattroff(w, COLOR_PAIR(PAIR_STATUS) | A_BOLD);

    wnoutrefresh(w);
}

static void tui_render_confirm(tui_state_t *s) {
    int sel_count = 0;
    for (int i = 0; i < s->row_count; ++i)
        if (s->selected[i]) sel_count++;
    int n = sel_count > 0 ? sel_count : (s->row_count > 0 ? 1 : 0);

    const int w_width  = 44;
    const int w_height = 5;
    int w_y = (LINES - w_height) / 2;
    int w_x = (COLS - w_width) / 2;

    WINDOW *popup = newwin(w_height, w_width, w_y, w_x);

    wattron(popup, COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    box(popup, 0, 0);

    mvwprintw(popup, 1, 2, "Send signal to %d process%s?", n, n == 1 ? "" : "es");
    mvwprintw(popup, 3, 2, "[y] Confirm    [n / Esc] Cancel");

    wattroff(popup, COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    wnoutrefresh(popup);
    doupdate();

    /* block for a single keypress here — no need to re-enter the main loop */
    keypad(popup, TRUE);
    int ch = wgetch(popup);
    if (ch == 'y' || ch == 'Y')
        s->confirmed  = true;
    else
        s->confirming = false;

    delwin(popup);

    /* force a full redraw to clear the popup */
    clearok(curscr, TRUE);
}

static void tui_render(tui_state_t *s) {
    tui_render_query(s);
    tui_render_list(s);
    tui_render_status(s);
    doupdate();

    if (s->confirming)
        tui_render_confirm(s);
}

static void tui_query_insert(tui_state_t *s, char c) {
    if (s->query_len >= TUI_MAX_QUERY - 1)
        return;
    s->query[s->query_len++] = c;
    s->query[s->query_len] = '\0';
    tui_rebuild_rows(s);
}

static void tui_query_backspace(tui_state_t *s) {
    if (s->query_len == 0)
        return;
    s->query[--s->query_len] = '\0';
    tui_rebuild_rows(s);
}

static void tui_cursor_move(tui_state_t *s, int delta) {
    s->cursor += delta;
    if (s->cursor < 0) s->cursor = 0;
    if (s->cursor >= s->row_count) s->cursor = s->row_count > 0 ? s->row_count - 1 : 0;
    tui_scroll_to_cursor(s);
}

static void tui_scroll_to_cursor(tui_state_t *s) {
    int height = tui_list_height();
    if (s->cursor < s->scroll)
        s->scroll = s->cursor;
    if (s->cursor >= s->scroll + height)
        s->scroll = s->cursor - height + 1;
    if (s->scroll < 0)
        s->scroll = 0;
}

static void tui_handle_input(tui_state_t *s, int ch) {
    switch (ch) {
    case '\n':
    case KEY_ENTER:
        if (s->row_count > 0) s->confirming = true;
        break;

    case 27: // Esc
    case 'q':
        s->cancelled = true;
        break;

    case KEY_UP: tui_cursor_move(s, -1); break;
    case KEY_DOWN: tui_cursor_move(s, +1); break;
    case KEY_PPAGE: tui_cursor_move(s, -tui_list_height()); break;
    case KEY_NPAGE: tui_cursor_move(s, +tui_list_height()); break;
    case KEY_HOME: s->cursor = 0; tui_scroll_to_cursor(s); break;
    case KEY_END: s->cursor = s->row_count > 0 ? s->row_count - 1 : 0; tui_scroll_to_cursor(s); break;

    case '\t':
        if (s->row_count > 0)
            s->selected[s->cursor] = !s->selected[s->cursor];
        break;

    case 1: // Ctrl-A
        for (int i = 0; i < s->row_count; ++i)
            s->selected[i] = true;
        break;

    case KEY_RESIZE:
        tui_resize(s);
        break;

    case KEY_BACKSPACE:
    case 127:
    case '\b':
        tui_query_backspace(s);
        break;

    default:
        if (ch >= 32 && ch < 127)
            tui_query_insert(s, (char)ch);
        break;
    }
}

tui_result_t tui_run(const swordfish_args_t *args, const process_info_t *procs, int count) {
    tui_result_t result = {0};

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    if (has_colors())
        tui_init_colors();

    tui_state_t s = {0};
    s.procs = procs;
    s.proc_count = count;

    tui_init_windows(&s);
    tui_rebuild_rows(&s);

    (void)args;

    while (!s.confirmed && !s.cancelled) {
        tui_render(&s);
        int ch = wgetch(s.win_list);
        tui_handle_input(&s, ch);
    }

    if (s.confirmed) {
        int sel_count = 0;
        for (int i = 0; i < s.row_count; ++i)
            if (s.selected[i])
                result.selected_pids[sel_count++] = s.rows[i].proc->pid;

        if (sel_count == 0 && s.row_count > 0)
            result.selected_pids[result.count++] = s.rows[s.cursor].proc->pid;
        else
            result.count = sel_count;
    }

    tui_cleanup_windows(&s);
    endwin();

    return result;
}
