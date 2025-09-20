#pragma once
#include "args.h"
#include <stdbool.h>
#include <sys/types.h>

#define MAX_MATCHES 1024

// Process entry struct for process matching and info
typedef struct {
    pid_t pid;
    char name[256];
    char owner[64];
} proc_entry_t;

int scan_processes(const swordfish_args_t *args, char **patterns, int pattern_count);
void drop_privileges(void);
bool is_all_digits(const char *s);
bool is_zombie_process(pid_t pid);