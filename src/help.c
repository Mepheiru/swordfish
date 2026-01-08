#include "help.h"
#include "args.h"
#include "main.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char _binary_docs_general_txt_start[];
extern const char _binary_docs_general_txt_end[];

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
    {"%s -k firefox", "Kill all processes with 'firefox' in the name"},
    {"%s -kx bash", "Kill all exact matches of 'bash'"},
    {"%s -Sk KILL vim", "Interactively select vim processes and send SIGKILL"},
    {"%s -ky firefox vim bash",
     "Kill all 'firefox', 'vim', and 'bash' processes without confirmation"},
    {"%s -kyr 1 firefox", "Recursively kill 'firefox' every 1 second"},
    {"%s --pre-hook script1.sh nvim", "Run 'script1.sh' before killing Neovim"},
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

static void format_option(char *buf, size_t size, const swordfish_option_t *opt) {
    buf[0] = '\0';

    if (opt->short_flag && opt->long_flag) {
        snprintf(buf, size, "%s, %s", opt->short_flag, opt->long_flag);
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

static const swordfish_option_t *find_option(const char *short_flag, const char *long_flag) {
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        const swordfish_option_t *opt = &swordfish_options[i];
        if ((short_flag && opt->short_flag && strcmp(opt->short_flag, short_flag) == 0) ||
            (long_flag && opt->long_flag && strcmp(opt->long_flag, long_flag) == 0))
            return opt;
    }
    return NULL;
}

static void print_embedded(FILE *out, const char *start, const char *end) {
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
void help(const char *prog, const char *category) {
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

    // Help
    printf("Swordfish -- A pkill-like CLI tool\n\n");
    printf("Usage: %s [options] pattern [pattern ...]\n\n", prog);
    printf("Help categories:\n");
    for (size_t i = 0; i < help_category_count; ++i)
        printf("  %-10s %s\n", help_categories[i].name, help_categories[i].description);
    printf("\nRun '%s --help <category>' for more.\n", prog);
}

// Functions called to display a help category.
// Theses are also used to generate the man pages
static void man_general(FILE *out, bool isman) {
    if (!isman)
        fprintf(out, "General Options -- Basic Usage for swordfish -- Compiled for %s \n\n", SWORDFISH_VERSION);

    fprintf(out, "All flags (-<opt>) can be grouped together. For example: \"-K -S\" can be turned "
                 "into: \"-KS\".\nBellow are some of the commonly used options used to control how "
                 "swordfish interacts with and finds procceses.\n\n");

    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "general") != 0)
            continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt)
            continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");

    fprintf(out, "Using swordfish without any arguments, but supplying a pattern can be used to "
                 "search for processes and get info on them. Swordfish will only send signals to a "
                 "proccess when the user requests it.\nBy default, swordfish will ask for "
                 "confirmation from the user. However, the argument \"-y\" can be used to skip the "
                 "confirmation and execute imidietly.\n");
}

static void man_signals(FILE *out, bool isman) {
    if (!isman)
        fprintf(out, "Signal Control -- How swordfish sends signals -- Compiled for %s \n\n", SWORDFISH_VERSION);

    fprintf(out, "Swordfish gives the user extensive control over the signals sent to procceses. "
                 "Bellow are some arguments that effect how swordfish sends signals.\n\n");

    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "signals") != 0)
            continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt)
            continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");

    fprintf(out, "The signals \"-k\" and \"-K\" act as soft and hard kill respectivly.\nSignals "
                 "other then KILL/TERM can be sent by using \"-<sig>\". For example: \"-10\" would "
                 "send USR1 to the procces.\nAs stated in General Operation, flags can be grouped "
                 "together. If \"-k\" and \"-K\" where grouped together (-kK) the arugment parser "
                 "will use the last argument (K). \n");
}

static void man_filter(FILE *out, bool isman) {
    if (!isman)
        fprintf(out, "Filtering -- Which processes are matched -- Compiled for %s \n\n", SWORDFISH_VERSION);

    fprintf(out, "The process list can have filters placed on it to help narrow down the list or give more information on specified processes. Bellow are some of the arguments that effect filtering or information shown.\n\n");
    
    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "filter") != 0)
            continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt)
            continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");

    fprintf(out, "When sorting the process list, the alies \"?<ram|age>\" can also be used instead of the long-opt version. For example: \n\n");
    fprintf(out, "  %-22s %s\n", "?<ram|age>", "Alies for \"--sort\"");
    fprintf(out, "  %-22s %s\n\n", "?ram", "Will sort the process list by highest to lowest ram usage");
    fprintf(out, "The defualt filtering/sorting mode for swordfish is the same as the order in \"/proc\". Ussaly this will display as lowest to highest PID number.");
}

static void man_behavior(FILE *out, bool isman) {
    if (!isman)
        fprintf(out, "Behavior -- Confirmation and execution behavior -- Compiled for %s \n\n", SWORDFISH_VERSION);

    fprintf(out, "Temp\n\n");

    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "behavior") != 0)
            continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt)
            continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
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
    if (!isman)
        fprintf(out, "Miscellaneous -- temp desc -- Compiled for %s \n\n", SWORDFISH_VERSION);

    for (size_t i = 0; i < option_category_map_count; ++i) {
        const swordfish_option_map_t *map = &option_category_map[i];
        if (strcmp(map->category, "misc") != 0)
            continue;
        const swordfish_option_t *opt = find_option(map->short_flag, map->long_flag);
        if (!opt)
            continue;
        char buf[64];
        format_option(buf, sizeof(buf), opt);
        fprintf(out, "  %-22s %s\n", buf, opt->desc);
    }
    fprintf(out, "\n");
}

void help_man(const char *path) {
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
