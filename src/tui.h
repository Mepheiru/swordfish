#pragma once

#include <stdbool.h>
#include <sys/types.h>

#include "args.h"
#include "process.h"

#define TUI_MAX_QUERY  256
#define TUI_MAX_SELECT 1024

typedef struct {
    pid_t selected_pids[TUI_MAX_SELECT];
    int   count;
} tui_result_t;

typedef struct {
    const process_info_t *proc;
    int score;
} tui_row_t;

typedef struct {
    void *win_query;
    void *win_list;
    void *win_status;

    const process_info_t *procs;
    int proc_count;

    tui_row_t rows[TUI_MAX_SELECT];
    int row_count;

    bool selected[TUI_MAX_SELECT];
    int cursor;
    int scroll;

    char query[TUI_MAX_QUERY];
    int query_len;

    bool confirmed;
    bool cancelled;
    bool confirming;
} tui_state_t;

tui_result_t tui_run(const swordfish_args_t *args,
                     const process_info_t  *procs,
                     int                    count);
