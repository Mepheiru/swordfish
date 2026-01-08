#pragma once
#include <signal.h>
#include <stdbool.h>

typedef struct {
    const char *flag;
    const char *desc;
} swordfish_flag_desc_t;

typedef struct {
    const char *usage;
    const char *desc;
} swordfish_usage_example_t;

typedef struct {
    const char *name;
    const char *desc;
} swordfish_completion_guide_t;

typedef struct {
    const char *name;
    int sig;
} swordfish_signal_t;

typedef struct {
    const char *short_flag;
    const char *long_flag;
    const char *arg;
    const char *desc;
    bool common;
} swordfish_option_t;

typedef struct {
    const char *name;        // internal name (for --help <name>)
    const char *title;       // human readable
    const char *description; // shown in help/man
} swordfish_help_category_info_t;

typedef struct {
    const char *category;
    const char *short_flag;
    const char *long_flag;
} swordfish_option_map_t;


extern const char *short_opts;

extern const swordfish_option_t swordfish_options[];
extern const size_t swordfish_options_count;
extern const swordfish_usage_example_t swordfish_usage[];
extern const swordfish_signal_t signals[];
extern const size_t swordfish_usage_count;
extern const size_t signals_count;

void usage(const char *prog);
void help(const char *prog, const char *category);
void help_man(const char *out);
