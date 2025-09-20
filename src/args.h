#pragma once
#include <signal.h>
#include <stdbool.h>

// Sorting modes for --sort
typedef enum { SWSORT_NONE = 0, SWSORT_CPU, SWSORT_RAM, SWSORT_AGE } swordfish_sort_mode_t;

typedef struct {
    const char *sig_str;
    int sig;
    bool do_kill;
    bool select_mode;
    bool exact_match;
    bool print_pids_only;
    bool auto_confirm;
    bool do_verbose;
    const char *user;
    int pattern_start_idx;
    swordfish_sort_mode_t sort_mode;
    const char *sort_key;
    // Add exclude patterns support
    const char **exclude_patterns;
    int exclude_count;
    // New fields for -t and -r
    bool top_only;
    int retry_time;
} swordfish_args_t;

typedef struct {
    const char *flag;
    const char *desc;
} swordfish_flag_desc_t;

typedef struct {
    const char *usage;
    const char *desc;
} swordfish_usage_example_t;

typedef struct {
    const char *name;
    int sig;
} swordfish_signal_t;

extern const swordfish_flag_desc_t swordfish_flags[];
extern const swordfish_usage_example_t swordfish_examples[];
extern const swordfish_signal_t signals[];
extern const size_t swordfish_flags_count;
extern const size_t swordfish_examples_count;
extern const size_t signals_count;

void usage(const char *prog);
void help(const char *prog);
int parse_args(int argc, char **argv, swordfish_args_t *args);
