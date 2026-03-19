#include "tui.h"
#include "fuzzy.h"
#include "main.h"
#include "theme.h"

#if defined(__x86_64__)
#  define PREFETCH(p) __asm__ volatile("prefetcht0 %0"::"m"(*(const char*)(p)))
#else
#  define PREFETCH(p) ((void)0)
#endif

#include <curses.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#define QUERY_HEIGHT 3
#define STATUS_HEIGHT 1

#define COL_PID_W 7
#define COL_NAME_W 20
#define COL_USER_W 12
#define COL_STATE_W 6
#define COL_RAM_W 10

#define PAIR_NORMAL 1
#define PAIR_HIGHLIGHT 2
#define PAIR_SELECTED 3
#define PAIR_QUERY 4
#define PAIR_HEADER 5
#define PAIR_STATUS 6
#define PAIR_DIM 7
#define PAIR_TITLE 8
#define PAIR_POPUP 9
#define PAIR_DIM_POPUP 16
// root pairs bake the correct bg so switching mid-row doesn't clobber row bg
#define PAIR_ROOT_NORMAL 10
#define PAIR_ROOT_SELECTED 11
#define PAIR_ROOT_HIGHLIGHT 25
// per-column text pairs - bg always inherits normal_bg
#define PAIR_PID 12
#define PAIR_USER 13
#define PAIR_STATE 14
#define PAIR_RAM 15
#define PAIR_PID_SELECTED 17
#define PAIR_PID_HIGHLIGHT 18
#define PAIR_USER_SELECTED 19
#define PAIR_USER_HIGHLIGHT 20
#define PAIR_STATE_SELECTED 21
#define PAIR_STATE_HIGHLIGHT 22
#define PAIR_RAM_SELECTED 23
#define PAIR_RAM_HIGHLIGHT 24

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
static void tui_render_action_prompt(tui_state_t *s);
static void tui_render_theme_picker(tui_state_t *s);
static void tui_handle_input(tui_state_t *s, int ch);
static void tui_query_insert(tui_state_t *s, char c);
static void tui_query_backspace(tui_state_t *s);
static void tui_cursor_move(tui_state_t *s, int delta);
static void tui_scroll_to_cursor(tui_state_t *s);
static int tui_list_height(void);
static void fmt_ram(char *buf, size_t len, long ram_mb);
static int cmp_rows_ram(const void *a, const void *b);
static int cmp_rows_score(const void *a, const void *b);
static void tui_mark_all_dirty(tui_state_t *s);
static bool tui_is_selected(const tui_state_t *s, pid_t pid);
static void tui_selection_toggle(tui_state_t *s, pid_t pid);

static bool tui_is_selected(const tui_state_t *s, pid_t pid) {
    for (int i = 0; i < s->selected_count; i++)
        if (s->selected_pids_set[i] == pid) return true;
    return false;
}

static void tui_selection_toggle(tui_state_t *s, pid_t pid) {
    for (int i = 0; i < s->selected_count; i++) {
        if (s->selected_pids_set[i] == pid) {
            // remove by swapping with last
            s->selected_pids_set[i] = s->selected_pids_set[--s->selected_count];
            return;
        }
    }
    if (s->selected_count < TUI_MAX_SELECT)
        s->selected_pids_set[s->selected_count++] = pid;
}

static void tui_init_colors(const sw_theme_t *t) {
    start_color();
    use_default_colors();
    theme_init_custom_colors();
    init_pair(PAIR_NORMAL, t->normal_text, t->normal_bg);
    init_pair(PAIR_HIGHLIGHT, t->highlight_text, t->highlight_bg);
    init_pair(PAIR_SELECTED, t->selected_text, t->selected_bg);
    init_pair(PAIR_QUERY, t->query_text, t->query_bg);
    init_pair(PAIR_HEADER, t->header_text, t->header_bg);
    init_pair(PAIR_STATUS, t->status_text, t->status_bg);
    init_pair(PAIR_DIM, t->dim_text, t->dim_bg);
    init_pair(PAIR_TITLE, t->title_text, t->title_bg);
    init_pair(PAIR_POPUP, t->popup_text, t->popup_bg);
    short dp_text = t->dim_popup_text ? t->dim_popup_text : t->dim_text;
    short dp_bg   = t->dim_popup_bg   ? t->dim_popup_bg   : t->dim_bg;
    init_pair(PAIR_DIM_POPUP, dp_text, dp_bg);

    init_pair(PAIR_ROOT_NORMAL,    t->root_text,           t->root_bg);
    init_pair(PAIR_ROOT_SELECTED,  t->root_selection_text, t->root_selection_bg);
    init_pair(PAIR_ROOT_HIGHLIGHT, t->root_highlight_text, t->root_highlight_bg);

    init_pair(PAIR_PID,   t->pid_text,   t->normal_bg);
    init_pair(PAIR_USER,  t->user_text,  t->normal_bg);
    init_pair(PAIR_STATE, t->state_text, t->normal_bg);
    init_pair(PAIR_RAM,   t->ram_text,   t->normal_bg);

    init_pair(PAIR_PID_SELECTED,    t->pid_selected_text,    t->selected_bg);
    init_pair(PAIR_PID_HIGHLIGHT,   t->pid_highlight_text,   t->highlight_bg);
    init_pair(PAIR_USER_SELECTED,   t->user_selected_text,   t->selected_bg);
    init_pair(PAIR_USER_HIGHLIGHT,  t->user_highlight_text,  t->highlight_bg);
    init_pair(PAIR_STATE_SELECTED,  t->state_selected_text,  t->selected_bg);
    init_pair(PAIR_STATE_HIGHLIGHT, t->state_highlight_text, t->highlight_bg);
    init_pair(PAIR_RAM_SELECTED,    t->ram_selected_text,    t->selected_bg);
    init_pair(PAIR_RAM_HIGHLIGHT,   t->ram_highlight_text,   t->highlight_bg);
}

static void tui_apply_theme(tui_state_t *s, const char *name) {
    sw_theme_t theme;
    theme_load(name, &theme);
    tui_init_colors(&theme);
    strncpy(s->active_theme, name ? name : "default", sizeof(s->active_theme) - 1);

    // re-stamp backgrounds on all windows with the new color pairs
    if (s->win_query) wbkgd(s->win_query, COLOR_PAIR(PAIR_NORMAL));
    if (s->win_list) wbkgd(s->win_list, COLOR_PAIR(PAIR_NORMAL));
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
    s->win_query = newwin(QUERY_HEIGHT, COLS, 0, 0);
    s->win_list = newwin(list_h, COLS, QUERY_HEIGHT, 0);
    s->win_status = newwin(STATUS_HEIGHT, COLS, LINES - STATUS_HEIGHT, 0);
    keypad(s->win_list, TRUE);

    wbkgd(s->win_query, COLOR_PAIR(PAIR_NORMAL));
    wbkgd(s->win_list, COLOR_PAIR(PAIR_NORMAL));
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
    if (s->win_query) { delwin(s->win_query); s->win_query = NULL; }
    if (s->win_list) { delwin(s->win_list); s->win_list = NULL; }
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

        int name_score = fuzzy_score(s->query, p->name, FUZZY_CTX_NAME);
        int cmd_score = fuzzy_score(s->query, p->cmdline, FUZZY_CTX_CMDLINE);

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
    mvwaddstr(w, 0, 0, "Swordfish fuzzy process finder (sfpf)");
    wattroff(w, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

    mvwaddstr(w, 1, 0, "  > ");
    wattron(w, COLOR_PAIR(PAIR_QUERY));
    waddstr(w, s->query);
    wattroff(w, COLOR_PAIR(PAIR_QUERY));

    wattron(w, COLOR_PAIR(PAIR_DIM) | A_DIM);
    mvwaddstr(w, 2, 0, "  Tab: select   Enter: confirm   Ctrl-T: themes   Esc/q: cancel");
    wattroff(w, COLOR_PAIR(PAIR_DIM) | A_DIM);

    wnoutrefresh(w);
}

static void tui_render_list(tui_state_t *s) {
    WINDOW *w = s->win_list;
    int height = tui_list_height();
    werase(w);

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
        wattron(w, COLOR_PAIR(PAIR_DIM) | A_DIM);
        mvwaddstr(w, height / 2, (COLS - 16) / 2, "no matches found");
        wattroff(w, COLOR_PAIR(PAIR_DIM) | A_DIM);
        wnoutrefresh(w);
        return;
    }

    for (int i = 0; i < height && (s->scroll + i) < s->row_count; ++i) {
        int idx = s->scroll + i;
        const tui_row_t *row = &s->rows[idx];
        const process_info_t *p = row->proc;

        bool is_cursor = (idx == s->cursor);
        bool is_selected = tui_is_selected(s, p->pid);
        bool is_root = (strcmp(p->owner, "root") == 0);
        int row_y = i + 1;
        int bold = is_cursor ? A_BOLD : 0;

        int pair_base = is_cursor ? PAIR_HIGHLIGHT : (is_selected ? PAIR_SELECTED : PAIR_NORMAL);
        if (is_cursor && is_root) pair_base = PAIR_ROOT_HIGHLIGHT;

        // sel marker - always base pair
        wattron(w, COLOR_PAIR(pair_base) | bold);
        mvwaddstr(w, row_y, 0, is_selected ? "  " : " ");

        // pid number
        char pid_buf[COL_PID_W + 2];
        snprintf(pid_buf, sizeof(pid_buf), "%-*d ", COL_PID_W, p->pid);
        int pid_pair = (is_cursor && is_root) ? PAIR_ROOT_HIGHLIGHT : (is_cursor ? PAIR_PID_HIGHLIGHT : (is_selected ? PAIR_PID_SELECTED : PAIR_PID));
        wattron(w, COLOR_PAIR(pid_pair) | bold);
        waddnstr(w, pid_buf, COL_PID_W + 1);

        // process name
        char name_buf[COL_NAME_W + 2];
        snprintf(name_buf, sizeof(name_buf), "%-*.*s ", COL_NAME_W, COL_NAME_W, p->name);
        int name_pair;
        if (is_cursor && is_root) name_pair = PAIR_ROOT_HIGHLIGHT;
        else if (is_cursor) name_pair = PAIR_HIGHLIGHT;
        else if (is_root && is_selected) name_pair = PAIR_ROOT_SELECTED;
        else if (is_root) name_pair = PAIR_ROOT_NORMAL;
        else name_pair = pair_base;
        wattron(w, COLOR_PAIR(name_pair) | bold);
        waddnstr(w, name_buf, COL_NAME_W + 1);

        // user
        char user_buf[COL_USER_W + 2];
        snprintf(user_buf, sizeof(user_buf), "%-*.*s ", COL_USER_W, COL_USER_W, p->owner);
        int user_pair = (is_cursor && is_root) ? PAIR_ROOT_HIGHLIGHT : (is_cursor ? PAIR_USER_HIGHLIGHT : (is_selected ? PAIR_USER_SELECTED : PAIR_USER));
        wattron(w, COLOR_PAIR(user_pair) | bold);
        waddnstr(w, user_buf, COL_USER_W + 1);

        // state
        char state_buf[COL_STATE_W + 2];
        snprintf(state_buf, sizeof(state_buf), "%-*c ", COL_STATE_W, p->status.state);
        int state_pair = (is_cursor && is_root) ? PAIR_ROOT_HIGHLIGHT : (is_cursor ? PAIR_STATE_HIGHLIGHT : (is_selected ? PAIR_STATE_SELECTED : PAIR_STATE));
        wattron(w, COLOR_PAIR(state_pair) | bold);
        waddnstr(w, state_buf, COL_STATE_W + 1);

        // ram
        char ram_buf[16];
        fmt_ram(ram_buf, sizeof(ram_buf), p->ram);
        char ram_col[COL_RAM_W + 1];
        snprintf(ram_col, sizeof(ram_col), "%-*.*s", COL_RAM_W, COL_RAM_W, ram_buf);
        int ram_pair = (is_cursor && is_root) ? PAIR_ROOT_HIGHLIGHT : (is_cursor ? PAIR_RAM_HIGHLIGHT : (is_selected ? PAIR_RAM_SELECTED : PAIR_RAM));
        wattron(w, COLOR_PAIR(ram_pair) | bold);
        waddnstr(w, ram_col, COL_RAM_W);

        // restore base pair for wclrtoeol so remainder of row has correct bg
        wattron(w, COLOR_PAIR(pair_base));
        wclrtoeol(w);
        wattroff(w, COLOR_PAIR(pair_base) | bold);
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

    int sel_count = s->selected_count;

    wattron(w, COLOR_PAIR(PAIR_STATUS) | A_BOLD);
    mvwprintw(w, 0, 0, "  %d/%d processes    %d selected",
              s->row_count, s->proc_count, sel_count);
    wclrtoeol(w);
    wattroff(w, COLOR_PAIR(PAIR_STATUS) | A_BOLD);

    wnoutrefresh(w);
}

static void tui_render_confirm(tui_state_t *s) {
    int sel_count = s->selected_count;
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

    wattron(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);
    mvwaddstr(popup, 3, 2, "[y] Confirm    [n / Esc] Cancel");
    wattroff(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);

    wnoutrefresh(popup);
    doupdate();

    keypad(popup, TRUE);
    set_escdelay(25);
    while (true) {
        int ch = wgetch(popup);
        if (ch == 'y' || ch == 'Y') {
            s->confirmed = true;
            break;
        } else if (ch == 'n' || ch == 'N' || ch == 27) {
            s->confirming = false;
            break;
        }
        // anything else including Enter is a no-op
    }

    delwin(popup);

    // force immediate redraw so the popup doesn't ghost until next keypress
    clearok(curscr, TRUE);
    tui_mark_all_dirty(s);
    tui_render_query(s);
    tui_render_list(s);
    tui_render_status(s);
    doupdate();
}

static void tui_render_action_prompt(tui_state_t *s) {
    const int w_width = 44;
    const int w_height = 7;
    int w_y = (LINES - w_height) / 2;
    int w_x = (COLS - w_width) / 2;

    WINDOW *popup = newwin(w_height, w_width, w_y, w_x);
    wbkgd(popup, COLOR_PAIR(PAIR_POPUP));
    keypad(popup, TRUE);

    int n = s->selected_count > 0 ? s->selected_count : (s->row_count > 0 ? 1 : 0);

    while (s->prompting_action) {
        werase(popup);
        wattron(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);
        box(popup, 0, 0);
        wattroff(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);

        wattron(popup, COLOR_PAIR(PAIR_POPUP));
        mvwprintw(popup, 1, 2, "Action for %d process%s:", n, n == 1 ? "" : "es");
        wattroff(popup, COLOR_PAIR(PAIR_POPUP));

        wattron(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);
        mvwaddstr(popup, 2, 2, "k=TERM  k9=KILL  kHUP  kUSR1 ...");
        wattroff(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);

        mvwaddstr(popup, 4, 2, "> ");
        wattron(popup, COLOR_PAIR(PAIR_QUERY));
        waddnstr(popup, s->action_buf, s->action_len);
        wattroff(popup, COLOR_PAIR(PAIR_QUERY));

        wattron(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);
        mvwaddstr(popup, 5, 2, "Enter: confirm   Esc: cancel");
        wattroff(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);

        wnoutrefresh(popup);
        doupdate();

        int ch = wgetch(popup);
        switch (ch) {
        case '\n':
        case KEY_ENTER: {
            if (s->action_len == 0 || s->action_buf[0] != 'k') {
                s->action_len = 0;
                s->action_buf[0] = '\0';
                break;
            }
            const char *sig_str = s->action_buf + 1;
            int sig;
            if (*sig_str == '\0') {
                sig = SIGTERM;
            } else {
                sig = get_signal(sig_str);
                if (sig < 0) {
                    s->action_len = 0;
                    s->action_buf[0] = '\0';
                    break;
                }
            }
            s->pending_sig = sig;
            s->prompting_action = false;
            s->confirming = true;
            break;
        }
        case 27: // Esc
            s->action_len = 0;
            s->action_buf[0] = '\0';
            s->prompting_action = false;
            break;
        case KEY_BACKSPACE:
        case 127:
        case '\b':
            if (s->action_len > 0)
                s->action_buf[--s->action_len] = '\0';
            break;
        case KEY_UP:
        case KEY_DOWN:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_PPAGE:
        case KEY_NPAGE:
        case KEY_HOME:
        case KEY_END:
            break;
        default:
            if (ch >= 32 && ch < 127 && s->action_len < (int)sizeof(s->action_buf) - 1) {
                s->action_buf[s->action_len++] = (char)ch;
                s->action_buf[s->action_len] = '\0';
            }
            break;
        }
    }

    delwin(popup);

    clearok(curscr, TRUE);
    tui_mark_all_dirty(s);
    tui_render_query(s);
    tui_render_list(s);
    tui_render_status(s);
    doupdate();

    if (s->confirming)
        tui_render_confirm(s);
}

static void tui_render_theme_picker(tui_state_t *s) {
    int count = theme_count();

    // start cursor on the currently active theme
    for (int i = 0; i < count; i++) {
        if (strcmp(theme_name_at(i), s->active_theme) == 0) {
            s->theme_picker_cursor = i;
            break;
        }
    }

    // remember what was active so Esc can restore it
    char prev_theme[64];
    strncpy(prev_theme, s->active_theme, sizeof(prev_theme) - 1);

    const int w_width = 36;
    const int w_height = count + 5;
    int w_y = (LINES - w_height) / 2;
    int w_x = (COLS - w_width) / 2;

    WINDOW *popup = newwin(w_height, w_width, w_y, w_x);
    wbkgd(popup, COLOR_PAIR(PAIR_POPUP));
    keypad(popup, TRUE);

    while (s->picking_theme) {
        werase(popup);
        wattron(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);
        box(popup, 0, 0);
        mvwaddstr(popup, 0, (w_width - 8) / 2, " Themes ");
        wattroff(popup, COLOR_PAIR(PAIR_POPUP) | A_BOLD);

        for (int i = 0; i < count; i++) {
            const char *name = theme_name_at(i);
            bool is_cur = (i == s->theme_picker_cursor);

            if (is_cur) wattron(popup, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            else wattron(popup, COLOR_PAIR(PAIR_POPUP));

            mvwprintw(popup, i + 1, 2, " %-*s", w_width - 5, name);

            if (is_cur) wattroff(popup, COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            else wattroff(popup, COLOR_PAIR(PAIR_POPUP));
        }

        wattron(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);
        mvwaddstr(popup, count + 2, 2, "Enter: apply   Esc: cancel");
        mvwaddstr(popup, count + 3, 2, "* theme picker is in testing");
        wattroff(popup, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);

        wnoutrefresh(popup);
        doupdate();

        int ch = wgetch(popup);
        switch (ch) {
        case KEY_UP:
            s->theme_picker_cursor = (s->theme_picker_cursor == 0)
                ? count - 1
                : s->theme_picker_cursor - 1;
            tui_apply_theme(s, theme_name_at(s->theme_picker_cursor));
            break;
        case KEY_DOWN:
            s->theme_picker_cursor = (s->theme_picker_cursor == count - 1)
                ? 0
                : s->theme_picker_cursor + 1;
            tui_apply_theme(s, theme_name_at(s->theme_picker_cursor));
            break;
        case '\n':
        case KEY_ENTER:
            s->picking_theme = false;
            break;
        case 27: // Esc
        case 'q':
            tui_apply_theme(s, prev_theme);
            s->picking_theme = false;
            break;
        case KEY_RESIZE:
            tui_apply_theme(s, prev_theme);
            s->picking_theme = false;
            tui_resize(s);
            break;
        }
    }

    delwin(popup);

    clearok(curscr, TRUE);
    tui_mark_all_dirty(s);
    tui_render_query(s);
    tui_render_list(s);
    tui_render_status(s);
    doupdate();
}

static void tui_mark_all_dirty(tui_state_t *s) {
    s->dirty_query = true;
    s->dirty_list = true;
    s->dirty_status = true;
}

static void tui_render(tui_state_t *s) {
    if (s->dirty_query) { tui_render_query(s); s->dirty_query = false; }
    if (s->dirty_list) { tui_render_list(s); s->dirty_list = false; }
    if (s->dirty_status) { tui_render_status(s); s->dirty_status = false; }
    doupdate();

    if (s->confirming)
        tui_render_confirm(s);

    if (s->prompting_action)
        tui_render_action_prompt(s);

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
        if (s->row_count > 0) {
            if (s->kill_after_select)
                s->confirming = true;
            else
                s->prompting_action = true;
        }
        break;

    case 27: // Esc
    case 17: // Ctrl-Q
        s->cancelled = true;
        break;

    case 20: // Ctrl-T
        s->picking_theme = true;
        break;

    case KEY_UP: tui_cursor_move(s, -1); break;
    case KEY_DOWN: tui_cursor_move(s, +1); break;
    case KEY_PPAGE: tui_cursor_move(s, -tui_list_height()); break;
    case KEY_NPAGE: tui_cursor_move(s, +tui_list_height()); break;
    case KEY_HOME: s->cursor = 0; tui_scroll_to_cursor(s); s->dirty_list = true; break;
    case KEY_END: s->cursor = s->row_count > 0 ? s->row_count - 1 : 0; tui_scroll_to_cursor(s); s->dirty_list = true; break;

    case '\t':
        if (s->row_count > 0) {
            tui_selection_toggle(s, s->rows[s->cursor].proc->pid);
            s->dirty_list = true;
            s->dirty_status = true;
        }
        break;

    case 4: tui_cursor_move(s, +tui_list_height() / 2); break; // Ctrl-D
    case 21: tui_cursor_move(s, -tui_list_height() / 2); break; // Ctrl-U

    case 1: // Ctrl-A
        bool all_selected = true;
        for (int i = 0; i < s->row_count; ++i)
            if (!tui_is_selected(s, s->rows[i].proc->pid)) { all_selected = false; break; }

        if (all_selected)
            s->selected_count = 0;
        else
            for (int i = 0; i < s->row_count; ++i)
                if (!tui_is_selected(s, s->rows[i].proc->pid))
                    tui_selection_toggle(s, s->rows[i].proc->pid);

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
    s.kill_after_select = args->kill_after_select;
    s.pending_sig = args->sig;
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

    while (!s.cancelled) {
        tui_render(&s);
        if (s.cancelled) break;

        if (s.confirmed) {
            result.sig = s.pending_sig;
            if (s.selected_count > 0) {
                result.count = s.selected_count;
                for (int i = 0; i < s.selected_count; i++)
                    result.selected_pids[i] = s.selected_pids_set[i];
            } else if (s.row_count > 0) {
                result.count = 1;
                result.selected_pids[0] = s.rows[s.cursor].proc->pid;
            }

            // send signals
            int sent = 0, failed = 0;
            for (int i = 0; i < result.count; i++) {
                if (kill(result.selected_pids[i], result.sig) == 0)
                    sent++;
                else
                    failed++;
            }

            // result popup
            const int pw = 44;
            const int ph = 5;
            WINDOW *rpop = newwin(ph, pw, (LINES - ph) / 2, (COLS - pw) / 2);
            wbkgd(rpop, COLOR_PAIR(PAIR_POPUP));
            wattron(rpop, COLOR_PAIR(PAIR_POPUP) | A_BOLD);
            box(rpop, 0, 0);
            wattroff(rpop, COLOR_PAIR(PAIR_POPUP) | A_BOLD);
            wattron(rpop, COLOR_PAIR(PAIR_POPUP));
            if (failed == 0)
                mvwprintw(rpop, 1, 2, "Sent signal %d to %d process%s",
                          result.sig, sent, sent == 1 ? "" : "es");
            else
                mvwprintw(rpop, 1, 2, "Sent %d, failed %d (permission?)", sent, failed);
            wattroff(rpop, COLOR_PAIR(PAIR_POPUP));
            wattron(rpop, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);
            mvwaddstr(rpop, 3, 2, "Press any key to exit");
            wattroff(rpop, COLOR_PAIR(PAIR_DIM_POPUP) | A_DIM);
            wnoutrefresh(rpop);
            doupdate();
            keypad(rpop, TRUE);
            wgetch(rpop);
            delwin(rpop);
            break;
        }

        int ch = wgetch(s.win_list);
        tui_handle_input(&s, ch);
    }

    tui_cleanup_windows(&s);
    endwin();

    return result;
}
