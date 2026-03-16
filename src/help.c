#include "help.h"
#include "args.h"

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void man_general(FILE *out);
static void man_signals(FILE *out);
static void man_filter(FILE *out);
static void man_behavior(FILE *out);
static void man_output(FILE *out);
static void man_misc(FILE *out);

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
    {NULL, "--sort", "<ram|age>", "Sort process list by RAM or age", false},
    {NULL, "--exclude", "<pattern>", "Exclude processes matching pattern", false},
    {NULL, "--pre-hook", "<script>", "Run <script> before sending signals", false},
    {NULL, "--post-hook", "<script>", "Run <script> after sending signals", false},
    {NULL, "--completions", "<shell>", "Generate shell completions for <shell> (fish, bash, zsh) and output to file if provided", false},
    {NULL, "--version", NULL, "Shows the current version of your Swordfish install. That's it.", false},
    {"-u", NULL, "<USER>", "Filter processes by username", false},
};
const size_t swordfish_options_count = sizeof(swordfish_options) / sizeof(swordfish_options[0]);

const swordfish_usage_example_t swordfish_usage[] = {
    {"%s -k firefox", "Kill all processes with 'firefox' in the name"},
    {"%s -kx bash", "Kill all exact matches of 'bash'"},
    {"%s -Sk KILL vim", "Interactively select vim processes and send SIGKILL"},
    {"%s -ky firefox vim bash",
     "Kill all 'firefox', 'vim', and 'bash' processes without confirmation"},
    {"%s -kyr 1 firefox", "Recursively kill 'firefox' every 1 second"},
    {"%s --pre-hook script1.sh nvim",
     "Run 'script1.sh' before killing Neovim"},
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
    {"general",  "General Options",  "Commonly used options"},
    {"signals",  "Signal Control",   "How Swordfish sends signals"},
    {"filter",   "Filtering",        "Which processes are matched"},
    {"behavior", "Behavior",         "Confirmation and execution behavior"},
    {"output",   "Output",           "Output and verbosity control"},
    {"misc",     "Miscellaneous",    "Less commonly used options"},
};
const size_t help_category_count =
    sizeof(help_categories) / sizeof(help_categories[0]);

const swordfish_option_map_t option_category_map[] = {
    {"general",  "-h", "--help"},
    {"general",  "-v", NULL},
    {"general",  "-S", NULL},

    {"signals",  "-k", NULL},
    {"signals",  "-K", NULL},

    {"filter",   "-x", NULL},
    {"filter",   "-u", NULL},
    {"filter",   NULL, "--exclude"},
    {"filter",   NULL, "--sort"},

    {"behavior", "-y", NULL},
    {"behavior", "-t", NULL},
    {"behavior", "-r", NULL},

    {"output",   "-p", NULL},

    {"misc",     NULL, "--pre-hook"},
    {"misc",     NULL, "--post-hook"},
    {"misc",     NULL, "--completions"},
    {"misc",     NULL, "--version"},
};
const size_t option_category_map_count =
    sizeof(option_category_map) / sizeof(option_category_map[0]);


static void format_option(char *buf, size_t size, const swordfish_option_t *opt) {
    buf[0] = '\0';

    if (opt->short_flag && opt->long_flag) {
        snprintf(buf, size, "%s, %s",
                 opt->short_flag, opt->long_flag);
    } else if (opt->short_flag) {
        snprintf(buf, size, "%s", opt->short_flag);
    } else if (opt->long_flag) {
        snprintf(buf, size, "%s", opt->long_flag);
    }

    if (opt->arg) {
        strncat(buf, " ", size - strlen(buf) - 1);
        strncat(buf, opt->arg, size - strlen(buf) - 1);
    }
}

static const swordfish_option_t *
find_option(const char *short_flag, const char *long_flag) {
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        const swordfish_option_t *opt = &swordfish_options[i];
        if ((short_flag && opt->short_flag &&
             strcmp(opt->short_flag, short_flag) == 0) ||
            (long_flag && opt->long_flag &&
             strcmp(opt->long_flag, long_flag) == 0))
            return opt;
    }
    return NULL;
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
            printf("  %-*s%s\n",
                  usage_indent,
                  swordfish_options[i].short_flag,
                  swordfish_options[i].desc);
        } else if (swordfish_options[i].long_flag) {
            printf("  %-*s%s\n",
                  usage_indent,
                  swordfish_options[i].long_flag,
                  swordfish_options[i].desc);
        }
    }
    printf("  pattern %-*s%s  One or more process names\n", usage_indent_d, "", "");
    printf("\nFor more information, please run '%s --help'\n", prog);
}

/* Prints full help block
   Usually called on "--help" or "--help <category>" */
void help(const char *prog, const char *category) {
    if (category) {
        if (strcmp(category, "general") == 0) { man_general(stdout); return; }
        if (strcmp(category, "signals") == 0) { man_signals(stdout); return; }
        if (strcmp(category, "filter") == 0) { man_filter(stdout); return; }
        if (strcmp(category, "behavior") == 0) { man_behavior(stdout); return; }
        if (strcmp(category, "output") == 0) { man_output(stdout); return; }
        if (strcmp(category, "misc") == 0) { man_misc(stdout); return; }
        // Not found
        printf("Unknown help category: %s\n", category);
        printf("Available categories:\n");
        for (size_t i = 0; i < help_category_count; ++i)
            printf("  %-10s %s\n", help_categories[i].name, help_categories[i].description);
        printf("\nRun '%s --help <category>' for details.\n", prog);
        return;
    }
    // General help
    printf("Swordfish — A pkill-like CLI tool\n\n");
    printf("Usage: %s [options] pattern [pattern ...]\n\n", prog);
    printf("Help categories:\n");
    for (size_t i = 0; i < help_category_count; ++i)
        printf("  %-10s %s\n", help_categories[i].name, help_categories[i].description);
    printf("\nRun '%s --help <category>' for details.\n", prog);
}

static void man_general(FILE *out) {
    fprintf(out, "General Options\n\n");
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "general") != 0) continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt) continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

static void man_signals(FILE *out) {
    fprintf(out, "Signal Control\n\n");
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "signals") != 0) continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt) continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

static void man_filter(FILE *out) {
    fprintf(out, "Filtering\n\n");
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "filter") != 0) continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt) continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

static void man_behavior(FILE *out) {
    fprintf(out, "Behavior\n\n");
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "behavior") != 0) continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt) continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

static void man_output(FILE *out) {
    fprintf(out, "Output\n\n");
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "output") != 0) continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt) continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

static void man_misc(FILE *out) {
    fprintf(out, "Miscellaneous\n\n");
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "misc") != 0) continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt) continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

void help_man(FILE *out) {
    fprintf(out, "Swordfish — A pkill-like CLI tool\n\n");
    fprintf(out, "Usage: swordfish [options] pattern...\n\n");
    man_general(out);
    man_signals(out);
    man_filter(out);
    man_behavior(out);
    man_output(out);
    man_misc(out);
    fprintf(out, "Examples\n\n");
    for (size_t i = 0; i < swordfish_usage_count; ++i) {
        fprintf(out, "  %s\n    %s\n", swordfish_usage[i].usage, swordfish_usage[i].desc);
    }
    fprintf(out, "\nSignals\n\n");
    for (size_t i = 0; i < signals_count; ++i)
        fprintf(out, "  %-5s : Signal number %d\n", signals[i].name, signals[i].sig);
}