#pragma once
#include <sys/types.h>
#include <stddef.h>

void safe_strncpy(char *dst, const char *src, size_t size);
void run_hook(const char *hook, pid_t pid, const char *name);
