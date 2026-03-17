#pragma once

#include <stdio.h>

#define MAX_MATCHES 1024

#define COLOR_RESET "\033[0m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_WARN "\033[1;93m"

#define EXIT_FOUND 0
#define EXIT_NO_MATCH 1
#define EXIT_PERMISSION 2
#define EXIT_PARTIAL 3

/* INCREMENT ON RELEASE BUILD*/
#define SWORDFISH_VERSION "1.2.4"
/* INCREMENT ON RELEASE BUILD*/

#define WARN(msg, ...) \
    fprintf(stderr, COLOR_WARN "WARNING:" COLOR_RESET " " msg "\n", ##__VA_ARGS__)

#define ERROR(msg, ...) \
    fprintf(stderr, COLOR_WARN "ERROR:" COLOR_RESET " " msg "\n", ##__VA_ARGS__)

#define INFO(msg, ...) \
    printf(COLOR_YELLOW "INFO:" COLOR_RESET " " msg "\n", ##__VA_ARGS__)

void generate_fish_completions(const char *prog, const char *file_path);
void generate_bash_completions(const char *prog, const char *file_path);
void generate_zsh_completions(const char *prog, const char *file_path);
