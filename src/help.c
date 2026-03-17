#include "help.h"
#include "main.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern const unsigned char _binary_docs_man_help_txt_start[];
extern const unsigned char _binary_docs_man_help_txt_end[];
extern const unsigned char _binary_docs_man_general_txt_start[];
extern const unsigned char _binary_docs_man_general_txt_end[];
extern const unsigned char _binary_docs_man_signals_txt_start[];
extern const unsigned char _binary_docs_man_signals_txt_end[];
extern const unsigned char _binary_docs_man_filter_txt_start[];
extern const unsigned char _binary_docs_man_filter_txt_end[];
extern const unsigned char _binary_docs_man_behavior_txt_start[];
extern const unsigned char _binary_docs_man_behavior_txt_end[];
extern const unsigned char _binary_docs_man_misc_txt_start[];
extern const unsigned char _binary_docs_man_misc_txt_end[];
extern const unsigned char _binary_docs_man_args_txt_start[];
extern const unsigned char _binary_docs_man_args_txt_end[];
extern const unsigned char _binary_docs_man_perf_txt_start[];
extern const unsigned char _binary_docs_man_perf_txt_end[];

static void man_general(FILE *out, bool isman);
static void man_signals(FILE *out, bool isman);
static void man_filter(FILE *out, bool isman);
static void man_behavior(FILE *out, bool isman);
// static void man_output(FILE *out, bool isman);
static void man_misc(FILE *out, bool isman);
static void man_args(FILE *out, bool isman);
static void man_perf(FILE *out, bool isman);

const swordfish_option_t swordfish_options[] = {
    /* Operations */
    {"-k", NULL, NULL, "Send signal to matching processes (default: SIGTERM). Signal embeds directly: -k9, -kHUP, -k15", true},
    {"-S", NULL, NULL, "Interactively select which processes to act on", true},
    {"-W", NULL, NULL, "Watch matching processes live", true},

    /* Modifiers */
    {"-x", NULL, NULL, "Exact match process names (default: substring match)", true},
    {"-y", NULL, NULL, "Auto-confirm; skip all prompts", true},
    {"-v", NULL, NULL, "Increase verbosity level up to -vvv for maximum verbosity", true},
    {"-t", NULL, NULL, "Always act on only the top matched process", true},
    {"-p", NULL, NULL, "Print raw PIDs only", true},
    {"-n", NULL, NULL, "Dry run; show what would happen without doing it", true},
    {"-w", NULL, NULL, "Wait for the process to die after sending signal", true},
    {"-r", NULL, NULL, "Hide processes owned by root", true},

    /* Long opts */
    {NULL, "--sort", "<ram|age>", "Sort process list by RAM or age", false},
    {NULL, "--exclude", "<pattern>", "Exclude processes matching pattern", false},
    {NULL, "--user", "<user>", "Filter processes by username", false},
    {NULL, "--retry", "<seconds>", "Retry every <seconds> if no match found", false},
    {NULL, "--timeout", "<seconds>", "Escalate to SIGKILL after <seconds> if process does not die", false},
    {NULL, "--format", "<string>", "Custom output format string", false},
    {NULL, "--parent", "<pid>", "Match only children of the given parent PID", false},
    {NULL, "--session", "<sid>", "Match processes by session ID", false},
    {NULL, "--pidfile", "<file>", "Read target PID from file", false},
    {NULL, "--pre-hook", "<script>", "Run <script> before sending signals", false},
    {NULL, "--post-hook", "<script>", "Run <script> after sending signals", false},
    {NULL, "--completions", "<shell>", "Generate shell completions for fish, bash, or zsh", false},
    {NULL, "--man", "[file]", "Generate man page, optionally writing to <file>", false},
    {NULL, "--version", NULL, "Show installed version", false},
    {NULL, "--help", "[category]", "Show help, optionally for a specific category", false},
};
const size_t swordfish_options_count = sizeof(swordfish_options) / sizeof(swordfish_options[0]);

const swordfish_usage_example_t swordfish_usage[] = {
    {"-k firefox",          "Kill all processes with 'firefox' in the name (SIGTERM)"},
    {"-k9 firefox",         "Kill all processes with 'firefox' using SIGKILL"},
    {"-kx bash",            "Kill all exact matches of 'bash'"},
    {"-k9y firefox",        "Send SIGKILL to all 'firefox' processes without confirmation"},
    {"-Sky firefox",        "Interactively select firefox processes and kill without confirmation"},
    {"-kHUP nginx",         "Send SIGHUP to nginx (reload config)"},
    {"-ky --retry 5 firefox", "Kill 'firefox', retrying every 5 seconds until none remain"},
    {"--pre-hook notify.sh nvim", "Run 'notify.sh' before killing Neovim"},
};

const swordfish_completion_guide_t swordfish_completion_guide[] = {
    {"fish", "Generate fish shell completions"},
    {"bash", "Generate bash shell completions"},
    {"zsh", "Generate zsh shell completions"},
};
const size_t swordfish_usage_count = sizeof(swordfish_usage) / sizeof(swordfish_usage[0]);

/* Known signals */
const swordfish_signal_t signals[] = {
    {"HUP", SIGHUP},   {"INT", SIGINT},   {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"TERM", SIGTERM},
    {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"STOP", SIGSTOP}, {"CONT", SIGCONT},
};
const size_t signals_count = sizeof(signals) / sizeof(signals[0]);

const swordfish_help_category_info_t help_categories[] = {
    {"arguments", "Arguments",   "Full argument reference"},
    {"general",   "General",     "Basic usage and common flags"},
    {"signals",   "Signals",     "How Swordfish sends signals"},
    {"filter",    "Filtering",   "Which processes are matched"},
    {"behavior",  "Behavior",    "Confirmation and execution behavior"},
    {"misc",      "Misc",        "Completions, versioning, and man pages"},
    {"perf",      "Performance", "How Swordfish is optimized"},
};
const size_t help_category_count = sizeof(help_categories) / sizeof(help_categories[0]);

const swordfish_option_map_t option_category_map[] = {
    {"general",  "-k",  NULL},
    {"general",  "-S",  NULL},
    {"general",  "-W",  NULL},
    {"general",  "-x",  NULL},
    {"general",  "-y",  NULL},
    {"general",  "-v",  NULL},

    {"signals",  "-k",  NULL},
    {"signals",  NULL,  "--timeout"},

    {"filter",   "-x",  NULL},
    {"filter",   "-r",  NULL},
    {"filter",   NULL,  "--user"},
    {"filter",   NULL,  "--exclude"},
    {"filter",   NULL,  "--sort"},
    {"filter",   NULL,  "--parent"},
    {"filter",   NULL,  "--session"},
    {"filter",   NULL,  "--pidfile"},

    {"behavior", "-y",  NULL},
    {"behavior", "-t",  NULL},
    {"behavior", "-n",  NULL},
    {"behavior", "-w",  NULL},
    {"behavior", "-v",  NULL},
    {"behavior", NULL,  "--retry"},
    {"behavior", NULL,  "--pre-hook"},
    {"behavior", NULL,  "--post-hook"},

    {"misc",     "-p",  NULL},
    {"misc",     NULL,  "--format"},
    {"misc",     NULL,  "--completions"},
    {"misc",     NULL,  "--man"},
    {"misc",     NULL,  "--version"},
    {"misc",     NULL,  "--help"},
};
const size_t option_category_map_count =
    sizeof(option_category_map) / sizeof(option_category_map[0]);


static void print_embedded(FILE *out, const unsigned char *start, const unsigned char *end) {
    fwrite(start, 1, (size_t)(end - start), out);
}

/* Prints the usage block
   Usually called on "-h" */
void usage(const char *prog) {
    const int usage_indent = 11;
    const int usage_indent_d = 1;

    printf("Swordfish — A fast process manager\n"
           "Usage: %s [operation] [modifiers] pattern [pattern ...]\n\n",
           prog);
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        if (!swordfish_options[i].common)
            continue;

        if (swordfish_options[i].short_flag) {
            printf("  %-*s%s\n", usage_indent, swordfish_options[i].short_flag,
                   swordfish_options[i].desc);
        } else if (swordfish_options[i].long_flag) {
            printf("  %-*s%s\n", usage_indent, swordfish_options[i].long_flag,
                   swordfish_options[i].desc);
        }
    }
    printf("  pattern %-*s%s  One or more process names\n", usage_indent_d, "", "");
    printf("\nFor more information, please run '%s --help'\n", prog);
}

/* Prints full help block
   Usually called on "--help" or "--help <category>" */
void help(const char *category) {
    const unsigned char *start = _binary_docs_man_help_txt_start;
    const unsigned char *end = _binary_docs_man_help_txt_end;

    if (category) {
        if (strcmp(category, "general") == 0) {
            man_general(stdout, false);
            return;
        } else if (strcmp(category, "signals") == 0) {
            man_signals(stdout, false);
            return;
        } else if (strcmp(category, "filter") == 0) {
            man_filter(stdout, false);
            return;
        } else if (strcmp(category, "behavior") == 0) {
            man_behavior(stdout, false);
            return;
        } else if (strcmp(category, "misc") == 0) {
            man_misc(stdout, false);
            return;
        } else if (strcmp(category, "perf") == 0) {
            man_perf(stdout, false);
            return;
        } else if (strcmp(category, "arguments") == 0) {
            man_args(stdout, false);
            return;
        } else {
            ERROR("Unknown help category: %s", category);
            ERROR("Run '%s --help' to see available categories", "swordfish");
            return;
        }
    }

    // help.txt
    print_embedded(stdout, start, end);
}

// Functions called to display a help category.
// Theses are also used to generate the man pages
static void man_general(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_general_txt_start;
    const unsigned char *end = _binary_docs_man_general_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

static void man_signals(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_signals_txt_start;
    const unsigned char *end = _binary_docs_man_signals_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

static void man_filter(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_filter_txt_start;
    const unsigned char *end = _binary_docs_man_filter_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

static void man_behavior(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_behavior_txt_start;
    const unsigned char *end = _binary_docs_man_behavior_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

// static void man_output(FILE *out, bool isman) {
//     if (!isman) fprintf(out, "Output\n\n");
//     for (size_t i = 0; i < option_category_map_count; ++i) {
//         const swordfish_option_map_t *map = &option_category_map[i];
//         if (strcmp(map->category, "output") != 0) continue;
//         const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
//         if (!opt) continue;
//         char buf[64];
//         format_option(buf, sizeof(buf), opt);
//         fprintf(out, "  %-22s %s\n", buf, opt->desc);
//     }
//     fprintf(out, "\n");
// }

static void man_misc(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_misc_txt_start;
    const unsigned char *end = _binary_docs_man_misc_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

static void man_args(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_args_txt_start;
    const unsigned char *end = _binary_docs_man_args_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

static void man_perf(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_perf_txt_start;
    const unsigned char *end = _binary_docs_man_perf_txt_end;

     if (isman) {
        int newlines = 0;
        const unsigned char *p = start;
        while (p < end && newlines < 2) {
            if (*p == '\n') newlines++;
            p++;
        }
        start = p; // new start position
    }

    print_embedded(out, start, end);
}

void gen_man(const char *path) {
    FILE *out = path ? fopen(path, "w") : stdout;

    if (!out) {
        ERROR("Invalid file path");
        return;
    }

    fprintf(out,
            ".TH SWORDFISH 1 \"$(date +\"%%Y-%%m-%%d\")\" \"Swordfish %s\" \"User Commands\"\n\n",
            SWORDFISH_VERSION);
    fprintf(out, ".SH NAME\nswordfish \\- A pkill-like CLI tool\n\n");
    fprintf(out, ".SH SYNOPSIS\n\n swordfish");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        const swordfish_option_t *opt = &swordfish_options[i];
        if (opt->short_flag && opt->arg) {
            fprintf(out, " [%s %s]", opt->short_flag, opt->arg);
        } else if (opt->short_flag) {
            fprintf(out, " [%s]", opt->short_flag);
        } else if (opt->long_flag && opt->arg) {
            fprintf(out, " [%s %s]", opt->long_flag, opt->arg);
        } else if (opt->long_flag) {
            fprintf(out, " [%s]", opt->long_flag);
        }
    }
    fprintf(out, " pattern...\n\n");

    fprintf(out, ".SH GENERAL OPTIONS\n");
    man_general(out, true);

    fprintf(out, ".SH SIGNAL CONTROL\n");
    man_signals(out, true);

    fprintf(out, ".SH FILTERING\n");
    man_filter(out, true);

    fprintf(out, ".SH BEHAVIOR\n");
    man_behavior(out, true);

    fprintf(out, ".SH MISCELLANEOUS\n");
    man_misc(out, true);

    // EXAMPLES
    fprintf(out, ".SH EXAMPLES\n");
    for (size_t i = 0; i < swordfish_usage_count; ++i) {
        fprintf(out, ".TP\n%s\n%s\n", swordfish_usage[i].usage, swordfish_usage[i].desc);
    }

    // SIGNALS
    fprintf(out, ".SH SIGNALS\n");
    for (size_t i = 0; i < signals_count; ++i) {
        fprintf(out, ".TP\n%s : Signal number %d\n", signals[i].name, signals[i].sig);
    }

    if (path)
        fclose(out);
}
