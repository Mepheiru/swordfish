#pragma once
#include <stddef.h>
#include <sys/types.h>

void safe_strcpy(char *dst, const char *src, size_t size);
void run_hook(const char *hook, pid_t pid, const char *name);
