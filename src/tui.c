#include "tui.h"
#include "fuzzy.h"
#include "main.h"
#include "theme.h"

// must be a macro — inline asm operand constraints require an addressable lvalue,
// which a function parameter doesn't guarantee at every call site
#if defined(__x86_64__)
#  define PREFETCH(p) __asm__ volatile("prefetcht0 %0"::"m"(*(const char*)(p)))
#else
#  define PREFETCH(p) ((void)0)
#endif

#include <curses.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#define QUERY_HEIGHT  3
#define STATUS_HEIGHT 1

#define COL_PID_W   7
#define COL_NAME_W  20
#define COL_USER_W  12
#define COL_STATE_W 6
#define COL_RAM_W   10

#define PAIR_NORMAL       1
#define PAIR_HIGHLIGHT    2
#define PAIR_SELECTED     3
#define PAIR_QUERY        4
#define PAIR_HEADER       5
#define PAIR_STATUS       6
#define PAIR_DIM          7
#define PAIR_TITLE        8
#define PAIR_POPUP        9
// root pairs bake the correct bg in so they work on any row state
#define PAIR_ROOT_NORMAL   10
#define PAIR_ROOT_SELECTED 11

static void tui_apply_theme(tui_state_t *s, const char *name);
static void tui_init_colors(const sw_theme_t *t);
static void tui_init_windows(tui_state_t *s);
static void tui_resize(tui_state_t *s);
static void tui_cleanup_windows(tui_state_t *s);
static void tui_rebuild_rows(tui_state_t *s);
static void tui_render(tui_state_t *s);
static void tui_render_query(tui_state_t *s);
static void tui_render_list(tui_state_t *s);
static void tui_render_status(tui_state_t *s);
static void tui_render_confirm(tui_state_t *s);
static void tui_render_theme_picker(tui_state_t *s);
static void tui_handle_input(tui_state_t *s, int ch);
static void tui_query_insert(tui_state_t *s, char c);
static void tui_query_backspace(tui_state_t *s);
static void tui_cursor_move(tui_state_t *s, int delta);
static void tui_scroll_to_cursor(tui_state_t *s);
static int  tui_list_height(void);
static void fmt_ram(char *buf, size_t len, long ram_mb);
static int  cmp_rows_ram(const void *a, const void *b);
static int  cmp_rows_score(const void *a, const void *b);
static void tui_mark_all_dirty(tui_state_t *s);

static void tui_init_colors(const sw_theme_t *t) {
    start_color();
    use_default_colors();
    theme_init_custom_colors();
    init_pair(PAIR_NORMAL,        t->normal_fg,    t->normal_bg);
    init_pair(PAIR_HIGHLIGHT,     t->highlight_fg, t->highlight_bg);
    init_pair(PAIR_SELECTED,      t->selected_fg,  t->selected_bg);
    init_pair(PAIR_QUERY,         t->query_fg,     t->query_bg);
    init_pair(PAIR_HEADER,        t->header_fg,    t->header_bg);
    init_pair(PAIR_STATUS,        t->status_fg,    t->status_bg);
    init_pair(PAIR_DIM,           t->dim_fg,       t->dim_bg);
    init_pair(PAIR_TITLE,         t->title_fg,     t->title_bg);
    init_pair(PAIR_POPUP,         t->popup_fg,     t->popup_bg);
    // root fg baked against each possible row background so switching pairs
    // mid-row doesn't clobber the row's background color
    init_pair(PAIR_ROOT_NORMAL,   t->root_fg,      t->normal_bg);
    init_pair(PAIR_ROOT_SELECTED, t->root_fg,      t->selected_bg);
}

static void tui_apply_theme(tui_state_t *s, const char *name) {
    sw_theme_t theme;
    theme_load(name, &theme);
    tui_init_colors(&theme);
    strncpy(s->active_theme, name ? name : "default", sizeof(s->active_theme) - 1);

    // re-stamp backgrounds on all windows with the new color pairs
    if (s->win_query)  wbkgd(s->win_query,  COLOR_PAIR(PAIR_NORMAL));
    if (s->win_list)   wbkgd(s->win_list,   COLOR_PAIR(PAIR_NORMAL));
    if (s->win_status) wbkgd(s->win_status, COLOR_PAIR(PAIR_STATUS));

    tui_mark_all_dirty(s);
}

static void fmt_ram(char *buf, size_t len, long ram_mb) {
    if (ram_mb <= 0)
        snprintf(buf, len, "-");
    else if (ram_mb >= 1024)
        snprintf(buf, len, "%.1f GiB", ram_mb / 1024.0);
    else
        snprintf(buf, len, "%.1f MiB", (double)ram_mb);
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

    wbkgd(s->win_query,  COLOR_PAIR(PAIR_NORMAL));
    wbkgd(s->win_list,   COLOR_PAIR(PAIR_NORMAL));
    wbkgd(s->win_status, COLOR_PAIR(PAIR_STATUS));
}

static void tui_resize(tui_state_t *s) {
    tui_cleanup_windows(s);
    resizeterm(0, 0);
    tui_init_windows(s);
    tui_scroll_to_cursor(s);
    tui_mark_all_dirty(s);
}

static void tui_cleanup_windows(tui_state_t *s) {
    if (s->win_query)  { delwin(s->win_query);  s->win_query  = NULL; }
    if (s->win_list)   { delwin(s->win_list);   s->win_list   = NULL; }
    if (s->win_status) { delwin(s->win_status); s->win_status = NULL; }
}

static int cmp_rows_ram(const void *a, const void *b) {
    const tui_row_t *ra = a, *rb = b;
    return (rb->proc->ram > ra->proc->ram) - (rb->proc->ram < ra->proc->ram);
}

static int cmp_rows_score(const void *a, const void *b) {
    const tui_row_t *ra = a, *rb = b;
    return rb->score - ra->score;
}

__attribute__((hot))
static void tui_rebuild_rows(tui_state_t *s) {
    s->row_count = 0;

    for (int i = 0; i < s->proc_count && s->row_count < TUI_MAX_SELECT; ++i) {
        if (i + 2 < s->proc_count)
            PREFETCH(&s->procs[i + 2]);

        const process_info_t *p = &s->procs[i];

        if (s->query_len == 0) {
            s->rows[s->row_count++] = (tui_row_t){ .proc = p, .score = 0 };
            continue;
        }

        int name_score = fuzzy_score(s->query, p->name,    FUZZY_CTX_NAME);
        int cmd_score  = fuzzy_score(s->query, p->cmdline, FUZZY_CTX_CMDLINE);

        int score = -1;
        if (name_score >= 0) score = name_score;
        else if (cmd_score >= 0) score = cmd_score;

        if (score >= 0) {
            score = fuzzy_apply_proc_bonus(score, p);
            s->rows[s->row_count++] = (tui_row_t){ .proc = p, .score = score };
        }
    }

    if (s->query_len == 0)
        qsort(s->rows, s->row_count, sizeof(tui_row_t), cmp_rows_ram);
    else
        qsort(s->rows, s->row_count, sizeof(tui_row_t), cmp_rows_score);

    if (s->cursor >= s->row_count)
        s->cursor = s->row_count > 0 ? s->row_count - 1 : 0;

    tui_scroll_to_cursor(s);
    s->dirty_list   = true;
    s->dirty_status = true;
}

static void tui_render_query(tui_state_t *s) {
    WINDOW *w = s->win_query;
    werase(w);

    wattron(w, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
    mvwaddstr(w, 0, 0, "Swordfish fuzzy process finder");
    wattroff(w, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

    mvwaddstr(w, 1, 0, "  > ");
    wattron(w, COLOR_PAIR(PAIR_QUERY));
    waddstr(w, s->query);
    wattroff(w, COLOR_PAIR(PAIR_QUERY));

    wattron(w, A_DIM);
    mvwaddstr(w, 2, 0, "  Tab: select   Enter: confirm   Ctrl-T: themes   Esc/q: cancel");
    wattroff(w, A_DIM);

    wnoutrefresh(w);
}

static void tui_render_list(tui_state_t *s) {
    WINDOW *w = s->win_list;
    int height = tui_list_height();
    werase(w);

    // header is constant — format once and reuse
    static char header_buf[256] = {0};
    if (!header_buf[0])
        snprintf(header_buf, sizeof(header_buf), " %-*s %-*s %-*s %-*s %s",
                 COL_PID_W,   "PID",
                 COL_NAME_W,  "NAME",
                 COL_USER_W,  "USER",
                 COL_STATE_W, "STATE", "RAM");

    wattron(w, COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvwaddstr(w, 0, 0, header_buf);
    int used = 1 + COL_PID_W + 1 + COL_NAME_W + 1 + COL_USER_W + 1 + COL_STATE_W + 1 + COL_RAM_W;
    for (int x = used; x < COLS; ++x) waddch(w, ' ');
    wattroff(w, COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (s->row_count == 0) {
        wattron(w, A_DIM);
        mvwaddstr(w, height / 2, (COLS - 16) / 2, "no matches found");
        wattroff(w, A_DIM);
        wnoutrefresh(w);
        return;
    }

    for (int i = 0; i < height && (s->scroll + i) < s->row_count; ++i) {
        int idx = s->scroll + i;
        const tui_row_t *row = &s->rows[idx];
        const process_info_t *p = row->proc;

        bool is_cursor   = (idx == s->cursor);
        bool is_selected = s->selected[idx];
        bool is_root     = (strcmp(p->owner, "root") == 0);
        int  row_y       = i + 1;

        if (is_cursor)        wattron(w, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        else if (is_selected) wattron(w, COLOR_PAIR(PAIR_SELECTED));
        else                  wattron(w, COLOR_PAIR(PAIR_NORMAL));

        // sel marker + pid in one shot — skips the printf pipeline inside ncurses
        char prefix[32];
        int prefix_len = snprintf(prefix, sizeof(prefix), "%s%-*d ",
                                  is_selected ? "  " : " ", COL_PID_W, p->pid);
        mvwaddnstr(w, row_y, 0, prefix, prefix_len);

        // root name uses a pair that has root_fg baked against the correct bg
        // so switching pairs mid-row never clobbers the row background
        char name_buf[COL_NAME_W + 2];
        snprintf(name_buf, sizeof(name_buf), "%-*.*s ", COL_NAME_W, COL_NAME_W, p->name);
        if (is_root && !is_cursor && !is_selected)
            wattron(w, COLOR_PAIR(PAIR_ROOT_NORMAL));
        waddnstr(w, name_buf, COL_NAME_W + 1);
        if (is_root && !is_cursor && !is_selected)
            wattron(w, COLOR_PAIR(PAIR_NORMAL));

        // user + state + ram in one shot
        char ram_buf[16];
        fmt_ram(ram_buf, sizeof(ram_buf), p->ram);
        char suffix[64];
        int suffix_len = snprintf(suffix, sizeof(suffix), "%-*.*s %-*c %-*.*s",
                                  COL_USER_W,  COL_USER_W,  p->owner,
                                  COL_STATE_W, p->status.state,
                                  COL_RAM_W,   COL_RAM_W,   ram_buf);
        waddnstr(w, suffix, suffix_len);

        wclrtoeol(w);

        if (is_cursor)        wattroff(w, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        else if (is_selected) wattroff(w, COLOR_PAIR(PAIR_SELECTED));
        else                  wattroff(w, COLOR_PAIR(PAIR_NORMAL));
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
    mvwprintw(w, 0, 0, "  %d/%d processes  |  %d selected  |  theme: %s",
              s->row_count, s->proc_count, sel_count, s->active_theme);
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
    int w_x = (COLS  - w_width)  / 2;

    WINDOW *popup = newwin(w_height, w_width, w_y, w_x);
    wbkgd(popup, COLOR_PAIR(PAIR_POPUP));

    wattron(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);
    box(popup, 0, 0);
    wattroff(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);

    wattron(popup, COLOR_PAIR(PAIR_POPUP));
    mvwprintw(popup, 1, 2, "Send signal to %d process%s?", n, n == 1 ? "" : "es");
    wattroff(popup, COLOR_PAIR(PAIR_POPUP));

    wattron(popup, COLOR_PAIR(PAIR_POPUP) | A_DIM);
    mvwaddstr(popup, 3, 2, "[y] Confirm    [n / Esc] Cancel");
    wattroff(popup, COLOR_PAIR(PAIR_POPUP) | A_DIM);

    wnoutrefresh(popup);
    doupdate();

    keypad(popup, TRUE);
    set_escdelay(25);
    int ch = wgetch(popup);
    if (ch == 'y' || ch == 'Y')
        s->confirmed  = true;
    else
        s->confirming = false;

    delwin(popup);

    /* force immediate redraw so the popup doesn't ghost until next keypress */
    clearok(curscr, TRUE);
    tui_mark_all_dirty(s);
    tui_render_query(s);
    tui_render_list(s);
    tui_render_status(s);
    doupdate();
}

static void tui_render_theme_picker(tui_state_t *s) {
    int count = theme_count();

    const int w_width  = 36;
    const int w_height = count + 6;
    int w_y = (LINES - w_height) / 2;
    int w_x = (COLS  - w_width)  / 2;

    WINDOW *popup = newwin(w_height, w_width, w_y, w_x);
    wbkgd(popup, COLOR_PAIR(PAIR_POPUP));
    keypad(popup, TRUE);

    while (s->picking_theme) {
        werase(popup);
        wattron(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);
        box(popup, 0, 0);
        mvwaddstr(popup, 0, (w_width - 7) / 2, " Theme ");
        wattroff(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);

        for (int i = 0; i < count; i++) {
            const char *name = theme_name_at(i);
            bool is_active = (strcmp(name, s->active_theme) == 0);
            bool is_cur    = (i == s->theme_picker_cursor);

            if (is_cur) wattron(popup, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            else        wattron(popup, COLOR_PAIR(PAIR_POPUP));

            mvwprintw(popup, i + 1, 2, " %-*s%s",
                      w_width - 7, name, is_active ? " *" : "  ");

            if (is_cur) wattroff(popup, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            else        wattroff(popup, COLOR_PAIR(PAIR_POPUP));
        }

        wattron(popup, COLOR_PAIR(PAIR_POPUP) | A_DIM);
        mvwaddstr(popup, count + 2, 2, "Enter: apply   Esc: close");
        // theme picker is experimental — warn the user
        mvwaddstr(popup, count + 3, 2, "* theme picker is in testing");
        wattroff(popup, COLOR_PAIR(PAIR_POPUP) | A_DIM);

        wnoutrefresh(popup);
        doupdate();

        int ch = wgetch(popup);
        switch (ch) {
        case KEY_UP:
            if (s->theme_picker_cursor > 0) s->theme_picker_cursor--;
            break;
        case KEY_DOWN:
            if (s->theme_picker_cursor < count - 1) s->theme_picker_cursor++;
            break;
        case '\n':
        case KEY_ENTER: {
            const char *chosen = theme_name_at(s->theme_picker_cursor);
            if (chosen) tui_apply_theme(s, chosen);
            s->picking_theme = false;
            break;
        }
        case 27: // Esc
        case 'q':
            s->picking_theme = false;
            break;
        case KEY_RESIZE:
            s->picking_theme = false;
            tui_resize(s);
            break;
        }
    }

    delwin(popup);

    /* force full redraw after popup closes */
    clearok(curscr, TRUE);
    tui_mark_all_dirty(s);
    tui_render_query(s);
    tui_render_list(s);
    tui_render_status(s);
    doupdate();
}

static void tui_mark_all_dirty(tui_state_t *s) {
    s->dirty_query  = true;
    s->dirty_list   = true;
    s->dirty_status = true;
}

static void tui_render(tui_state_t *s) {
    if (s->dirty_query)  { tui_render_query(s);  s->dirty_query  = false; }
    if (s->dirty_list)   { tui_render_list(s);   s->dirty_list   = false; }
    if (s->dirty_status) { tui_render_status(s); s->dirty_status = false; }
    doupdate();

    if (s->confirming)
        tui_render_confirm(s);

    if (s->picking_theme)
        tui_render_theme_picker(s);
}

static void tui_query_insert(tui_state_t *s, char c) {
    if (s->query_len >= TUI_MAX_QUERY - 1)
        return;
    s->query[s->query_len++] = c;
    s->query[s->query_len] = '\0';
    s->dirty_query = true;
    tui_rebuild_rows(s);
}

static void tui_query_backspace(tui_state_t *s) {
    if (s->query_len == 0)
        return;
    s->query[--s->query_len] = '\0';
    s->dirty_query = true;
    tui_rebuild_rows(s);
}

static void tui_cursor_move(tui_state_t *s, int delta) {
    s->cursor += delta;
    if (s->cursor < 0) s->cursor = 0;
    if (s->cursor >= s->row_count) s->cursor = s->row_count > 0 ? s->row_count - 1 : 0;
    tui_scroll_to_cursor(s);
    s->dirty_list = true;
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

    case 20: // Ctrl-T
        s->picking_theme = true;
        break;

    case KEY_UP:    tui_cursor_move(s, -1); break;
    case KEY_DOWN:  tui_cursor_move(s, +1); break;
    case KEY_PPAGE: tui_cursor_move(s, -tui_list_height()); break;
    case KEY_NPAGE: tui_cursor_move(s, +tui_list_height()); break;
    case KEY_HOME:  s->cursor = 0; tui_scroll_to_cursor(s); s->dirty_list = true; break;
    case KEY_END:   s->cursor = s->row_count > 0 ? s->row_count - 1 : 0; tui_scroll_to_cursor(s); s->dirty_list = true; break;

    case '\t':
        if (s->row_count > 0) {
            s->selected[s->cursor] = !s->selected[s->cursor];
            s->dirty_list   = true;
            s->dirty_status = true;
        }
        break;

    case 4:  tui_cursor_move(s, +tui_list_height() / 2); break; // Ctrl-D
    case 21: tui_cursor_move(s, -tui_list_height() / 2); break; // Ctrl-U

    case 1: // Ctrl-A
        for (int i = 0; i < s->row_count; ++i)
            s->selected[i] = true;
        s->dirty_list   = true;
        s->dirty_status = true;
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

    tui_state_t s = {0};
    s.procs = procs;
    s.proc_count = count;
    strncpy(s.active_theme, args->theme ? args->theme : "default",
            sizeof(s.active_theme) - 1);

    if (has_colors()) {
        sw_theme_t theme;
        theme_load(s.active_theme, &theme);
        tui_init_colors(&theme);
    }

    tui_init_windows(&s);
    tui_rebuild_rows(&s);
    tui_mark_all_dirty(&s);

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
