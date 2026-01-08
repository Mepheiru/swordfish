#include "help.h"
#include "args.h"
#include "main.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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

static void man_general(FILE *out, bool isman);
static void man_signals(FILE *out, bool isman);
static void man_filter(FILE *out, bool isman);
static void man_behavior(FILE *out, bool isman);
// static void man_output(FILE *out, bool isman);
static void man_misc(FILE *out, bool isman);

const swordfish_option_t swordfish_options[] = {
    {"-S", NULL, NULL, "Select which PIDs to kill (interactive prompt)", true},
    {"-k", NULL, NULL, "Send SIGTERM to matching processes (Graceful shutdown)", true},
    {"-K", NULL, NULL, "Send SIGKILL to matching processes (Forceful shutdown)", true},
    {"-x", NULL, NULL, "Exact match process names (default: substring match)", true},
    {"-y", NULL, NULL, "Auto-confirm kills; skip prompts and sudo confirmation", true},
    {"-p", NULL, NULL, "Print raw PIDs only", true},
    {"-t", NULL, NULL, "Always select the top process", true},
    {"-v", NULL, NULL, "Increase verbosity level up to -vvv for maximum verbosity", true},
    {"-h", "--help", NULL, "Show help message", true},
    {"-r", NULL, "<time>", "Retry on failure after waiting <time> seconds", false},
    {"-R", NULL, NULL, "Hide processes that are owned by root", true},
    {NULL, "--sort", "<ram|age>", "Sort process list by RAM or age", false},
    {NULL, "--exclude", "<pattern>", "Exclude processes matching pattern", false},
    {NULL, "--pre-hook", "<script>", "Run <script> before sending signals", false},
    {NULL, "--post-hook", "<script>", "Run <script> after sending signals", false},
    {NULL, "--completions", "<shell> [file]",
     "Generate shell completions for <shell> (fish, bash, zsh) and output to file if provided",
     false},
    {NULL, "--man", "[file]", "Generates the man page for swordfish and output to file if provided",
     false},
    {NULL, "--version", NULL, "Shows the current version of your Swordfish install. That's it.",
     false},
    {"-u", NULL, "<USER>", "Filter processes by username", false},
};
const size_t swordfish_options_count = sizeof(swordfish_options) / sizeof(swordfish_options[0]);

const swordfish_usage_example_t swordfish_usage[] = {
    {"-k firefox", "Kill all processes with 'firefox' in the name"},
    {"-kx bash", "Kill all exact matches of 'bash'"},
    {"-Sk KILL vim", "Interactively select vim processes and send SIGKILL"},
    {"-ky firefox vim bash",
     "Kill all 'firefox', 'vim', and 'bash' processes without confirmation"},
    {"-kyr 1 firefox", "Recursively kill 'firefox' every 1 second"},
    {"--pre-hook script1.sh nvim", "Run 'script1.sh' before killing Neovim"},
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
    {"general", "General Usage", "Basic Usage for swordfish"},
    {"signals", "Signal Control", "How swordfish sends signals"},
    {"filter", "Filtering", "Which processes are matched"},
    {"behavior", "Behavior", "Confirmation and execution behavior"},
    {"output", "Output", "Output and verbosity control"},
    {"misc", "Miscellaneous", "Miscellaneous features"},
};
const size_t help_category_count = sizeof(help_categories) / sizeof(help_categories[0]);

const swordfish_option_map_t option_category_map[] = {
    {"general", "-h", "--help"},
    {"general", "-v", NULL},
    {"general", "-k", NULL},
    {"general", "-y", NULL},

    {"signals", "-k", NULL},
    {"signals", "-K", NULL},
    {"signals", "-<sig>", NULL},

    {"filter", "-x", NULL},
    {"filter", "-u", NULL},
    {"filter", "-R", NULL},
    {"filter", NULL, "--exclude"},
    {"filter", NULL, "--sort"},

    {"behavior", "-y", NULL},
    {"behavior", "-t", NULL},
    {"behavior", "-r", NULL},
    {"behavior", "-v", NULL},
    {"behavior", NULL, "--pre-hook"},
    {"behavior", NULL, "--post-hook"},

    // {"output",   "-p", NULL},

    {"output", "-p", NULL},
    {"misc", NULL, "--completions"},
    {"misc", NULL, "--man"},
    {"misc", NULL, "--version"},
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

    printf("Swordfish : A pkill-like CLI tool\n"
           "Usage: %s [option] pattern [pattern ...]\n\n",
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
        } else {
            ERROR("Unknown help category: %s", category);
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

    // SECTIONS
    fprintf(out, ".SH GENERAL OPTIONS\n");
    man_general(out, true);

    fprintf(out, ".SH SIGNAL CONTROL\n");
    man_signals(out, true);

    fprintf(out, ".SH FILTERING\n");
    man_filter(out, true);

    fprintf(out, ".SH BEHAVIOR\n");
    man_behavior(out, true);

    // fprintf(out, ".SH OUTPUT\n");
    // man_output(out, true);

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
