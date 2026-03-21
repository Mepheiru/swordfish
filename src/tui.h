#pragma once

#include <stdbool.h>
#include <sys/types.h>

#include "args.h"
#include "process.h"

#define TUI_MAX_QUERY 256
#define TUI_MAX_SELECT 512

typedef struct {
    pid_t selected_pids[TUI_MAX_SELECT];
    int count;
    int sig;
} tui_result_t;

typedef struct {
    const process_info_t *proc;
    int score;
} tui_row_t;

typedef struct {
    const process_info_t *procs;
    void *win_query;
    void *win_list;
    void *win_status;
    tui_row_t rows[TUI_MAX_SELECT];
    pid_t selected_pids_set[TUI_MAX_SELECT];
    char query[TUI_MAX_QUERY];
    char active_theme[64];
    char action_buf[32];
    int proc_count;
    int theme_picker_cursor;
    int row_count;
    int selected_count;
    int cursor;
    int scroll;
    int query_len;
    int action_len;
    int pending_sig;
    bool confirmed;
    bool cancelled;
    bool confirming;
    bool prompting_action;
    bool kill_after_select;
    bool dirty_query;
    bool dirty_list;
    bool dirty_status;
    bool picking_theme;
} tui_state_t;

tui_result_t tui_run(const swordfish_args_t *args, const process_info_t *procs, int count);
