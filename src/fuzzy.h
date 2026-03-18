#pragma once

#include "process.h"

typedef enum {
    FUZZY_CTX_NAME,
    FUZZY_CTX_CMDLINE,
} fuzzy_ctx_t;

int fuzzy_score(const char *pattern, const char *str, fuzzy_ctx_t ctx);
int fuzzy_apply_proc_bonus(int score, const process_info_t *proc);
