#pragma once
#include "args.h"
#include <stdbool.h>
#include <sys/types.h>

#define MAX_MATCHES 1024

typedef struct {
    char **patterns;
    bool *pattern_is_pid;
    int pattern_count;
} pattern_list_t;

typedef struct {
    pid_t pid;
    char name[256];
    char owner[64];
} proc_entry_t;

typedef struct {
    uid_t uid;
    char state;
    char threads[32];
} proc_status_t;

typedef struct {
    pid_t pid;
    char name[256];
    char owner[64];
    char cmdline[256];
    proc_status_t status;
} process_info_t;

int scan_processes(const swordfish_args_t *args, pattern_list_t *plist);
void drop_privileges(void);
bool is_all_digits(const char *s);
bool is_zombie_process(pid_t pid);