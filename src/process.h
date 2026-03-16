#pragma once

#include <regex.h>
#include <stdbool.h>
#include <sys/types.h>
#include "args.h"

typedef struct {
    uid_t uid;
    char state;
    char threads[322];
} proc_status_t;

typedef struct {
    char **patterns;
    bool *pattern_is_pid;
    int pattern_count;
} pattern_list_t;

typedef struct {
    pid_t pid;
    char name[256];
    char owner[64];
    char cmdline[256];
    double cpu;
    long ram;
    long start_time;
    proc_status_t status;
} process_info_t;

typedef enum { PAT_EXACT, PAT_REGEX, PAT_SUBSTR, PAT_SKIP } pattern_type_t;

typedef struct {
    char pattern[256];
    int type;
    regex_t regex;
} compiled_pattern_t;

#define MAX_PATTERNS 64

int scan_processes(const swordfish_args_t *args, pattern_list_t *plist);
void drop_privileges(void);
bool is_all_digits(const char *s);
bool is_zombie_process(pid_t pid);
bool is_interactive(void);
