#include "main.h"
#include "process.h"
#include "args.h"
#include "hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

void safe_strcpy(char *dst, const char *src, size_t size) {
    if (!dst || !src || size == 0) return;
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

void run_hook(const char *hook, pid_t pid, const char *name) {
    if (!hook || hook[0] == '\0')
        return;               // silently skip when no hook supplied

    // NOTE: system() is simple but has injection risks — see notes below
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s %d %s", hook, (int)pid, name);
    if (system(cmd) != 0) {
        fprintf(stderr, "Hook '%s' failed for PID %d (%s)\n", hook, (int)pid, name);
    }
}