#pragma once

#include <stdio.h>

extern int tests_passed;
extern int tests_failed;

#define CHECK(cond, label) do { \
    if (cond) { \
        tests_passed++; \
        printf("  \033[1;32mPASS\033[0m  %s\n", label); \
    } else { \
        tests_failed++; \
        printf("  \033[1;31mFAIL\033[0m  %s  \033[2m(%s:%d)\033[0m\n", \
               label, __FILE__, __LINE__); \
    } \
} while (0)

#define SUITE(name) printf("\n\033[1m%s\033[0m\n", name)
