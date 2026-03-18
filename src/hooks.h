#pragma once
#include <sys/types.h>
#include <stddef.h>

void safe_strncpy(char *dst, const char *src, size_t size);
void run_hook(const char *hook, pid_t pid, const char *name);

void generate_fish_completions(const char *prog, const char *file_path);
void generate_bash_completions(const char *prog, const char *file_path);
void generate_zsh_completions(const char *prog, const char *file_path);
