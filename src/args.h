#pragma once
#include <stdint.h>
#include <sys/types.h>

/* Sorting modes */
typedef enum : uint8_t {
    SWSORT_NONE = 0,
    SWSORT_RAM,
    SWSORT_AGE,
} swordfish_sort_mode_t;

/* Operations */
typedef enum : uint8_t {
    SWOP_STATIC = 0,
    SWOP_KILL,
    SWOP_SELECT,
    SWOP_FUZZY,
} swordfish_op_t;

/* Long opt identifiers */
typedef enum {
    LOPT_SORT = 1000,
    LOPT_HELP = 1001,
    LOPT_EXCLUDE = 1002,
    LOPT_PRE_HOOK = 1003,
    LOPT_POST_HOOK = 1004,
    LOPT_COMPLETIONS = 1005,
    LOPT_VERSION = 1006,
    LOPT_MAN = 1007,
    LOPT_USER = 1008,
    LOPT_RETRY = 1009,
    LOPT_TIMEOUT = 1010,
    LOPT_FORMAT = 1011,
    LOPT_PARENT = 1012,
    LOPT_SESSION = 1013,
    LOPT_PIDFILE = 1014,
    LOPT_OUTPUT = 1015,
    LOPT_THEME = 1016,
} swordfish_lopt_t;

/* Main arguments struct */
typedef struct {
    const char *sig_str;
    const char *user;
    const char *format;
    const char *pidfile;
    const char *help_topic;
    const char *theme;
    const char **exclude_patterns;
    int sig;
    int retry_time;
    int timeout;
    int session_id;
    char pre_hook[256];
    char post_hook[256];
    pid_t parent_pid;
    short int exclude_count;
    short int pattern_start_idx;
    short int verbose_level;
    swordfish_op_t operation;
    swordfish_sort_mode_t sort_mode;
    unsigned int exact_match     : 1;
    unsigned int auto_confirm    : 1;
    unsigned int print_pids_only : 1;
    unsigned int top_only        : 1;
    unsigned int hide_root       : 1;
    unsigned int dry_run         : 1;
    unsigned int wait_for_death  : 1;
    unsigned int kill_after_select : 1;
} swordfish_args_t;

int parse_args(int *argc, char **argv, swordfish_args_t *args);
int get_signal(const char *sigstr);
