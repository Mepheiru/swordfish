// Global definitions and consts
#pragma once
#define MAX_MATCHES 1024

// Colors
#define COLOR_RESET "\033[0m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_WARN "\033[1;93m"

// Message macros
#define WARN(msg, ...) \
    fprintf(stderr, COLOR_WARN "WARNING:" COLOR_RESET " " msg "\n", ##__VA_ARGS__)

#define ERROR(msg, ...) \
    fprintf(stderr, COLOR_WARN "ERROR:" COLOR_RESET " " msg "\n", ##__VA_ARGS__)

#define INFO(msg, ...) \
    fprintf(stdout, COLOR_YELLOW "INFO:" COLOR_RESET " " msg "\n", ##__VA_ARGS__)

void safe_strcpy(char *dst, const char *src, size_t size);