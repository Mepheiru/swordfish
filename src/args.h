#pragma once
#include <stdint.h>

/* Sorting modes for --sort */
typedef enum : uint8_t { SWSORT_NONE = 0, SWSORT_CPU, SWSORT_RAM, SWSORT_AGE } swordfish_sort_mode_t;

/* Command-line arguments structure definitions */
typedef struct {
    const char *sig_str;
    const char *sort_key;
    const char *user;
    const char **exclude_patterns;
    char pre_hook[256];
    char post_hook[256];
    unsigned short int sig;
    short int exclude_count;
    short int pattern_start_idx;
    short int verbose_level;
    int retry_time;
    unsigned int do_term       : 1;
    unsigned int do_kill       : 1;
    unsigned int do_sig        : 1;
    unsigned int select_mode   : 1;
    unsigned int exact_match   : 1;
    unsigned int print_pids_only : 1;
    unsigned int auto_confirm  : 1;
    unsigned int run_static    : 1;
    unsigned int top_only      : 1;
    unsigned int hide_root     : 1;
    swordfish_sort_mode_t sort_mode;
    const char *help_topic;
} swordfish_args_t;

int parse_args(int *argc, char **argv, swordfish_args_t *args);
