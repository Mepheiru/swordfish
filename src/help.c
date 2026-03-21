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

static void man_general(FILE *out, bool isman);
static void man_signals(FILE *out, bool isman);
static void man_filter(FILE *out, bool isman);
static void man_behavior(FILE *out, bool isman);
static void man_misc(FILE *out, bool isman);
static void man_args(FILE *out, bool isman);

const swordfish_option_t swordfish_options[] = {
    {"-k", NULL, "Send signal to matching processes (default: SIGTERM)", true, "operation"},
    {"-S", NULL, "Interactively select which processes to act on", true, "operation"},
    {"-F", NULL, "Interactively select processes with a fuzzy finder", true, "operation"},
    {"-x", NULL, "Exact match process names (default: substring match)", true, "modifier"},
    {"-y", NULL, "Auto-confirm; skip all prompts", true, "modifier"},
    {"-v", NULL, "Increase verbosity level up to -vvv for maximum verbosity", true, "modifier"},
    {"-t", NULL, "Always act on only the top matched process", true, "modifier"},
    {"-p", NULL, "Print raw PIDs only", true, "modifier"},
    {"-n", NULL, "Dry run; show what would happen without doing it", true, "modifier"},
    {"-w", NULL, "Wait for the process to die after sending signal", true, "modifier"},
    {"-r", NULL, "Hide processes owned by root", true, "modifier"},
};
const size_t swordfish_options_count = sizeof(swordfish_options) / sizeof(swordfish_options[0]);

const swordfish_option_t swordfish_options_lopt[] = {
    {"--user", "<user>", "Filter processes by username", false, "filter"},
    {"--exclude", "<pattern>", "Exclude processes matching pattern", false, "filter"},
    {"--sort", "<ram|age>", "Sort process list by RAM or age", false, "filter"},
    {"--parent", "<pid>", "Match only children of the given parent PID", false, "filter"},
    {"--session", "<sid>", "Match processes by session ID", false, "filter"},
    {"--pidfile", "<file>", "Read target PID from file", false, "filter"},
    {"--retry", "<seconds>", "Retry every <seconds> if no match found", false, "behavior"},
    {"--timeout", "<seconds>", "Escalate to SIGKILL after <seconds> if process does not die", false, "behavior"},
    {"--pre-hook", "<script>", "Run <script> before sending signals", false, "hook"},
    {"--post-hook", "<script>", "Run <script> after sending signals", false, "hook"},
    {"--completions", "<shell>", "Generate shell completions for fish, bash, or zsh", false, "misc"},
    {"--man", "[file]", "Generate man page, optionally writing to <file>", false, "misc"},
    {"--version", NULL, "Show installed version", false, "misc"},
    {"--help", "[category]", "Show help, optionally for a specific category", false, "misc"},
};
const size_t swordfish_options_lopt_count = sizeof(swordfish_options_lopt) / sizeof(swordfish_options_lopt[0]);

const swordfish_usage_example_t swordfish_usage[] = {
    {"-k firefox", "Kill all processes with 'firefox' in the name (SIGTERM)"},
    {"-k9 firefox", "Kill all processes with 'firefox' using SIGKILL"},
    {"-kx bash", "Kill all exact matches of 'bash'"},
    {"-k9y firefox", "Send SIGKILL to all 'firefox' processes without confirmation"},
    {"-Sky firefox", "Interactively select firefox processes and kill without confirmation"},
    {"-kHUP nginx", "Send SIGHUP to nginx (reload config)"},
    {"-ky --retry 5 firefox", "Kill 'firefox', retrying every 5 seconds until none remain"},
    {"--pre-hook notify.sh nvim", "Run 'notify.sh' before killing Neovim"},
};
const size_t swordfish_usage_count = sizeof(swordfish_usage) / sizeof(swordfish_usage[0]);

const swordfish_completion_guide_t swordfish_completion_guide[] = {
    {"fish", "Generate fish shell completions"},
    {"bash", "Generate bash shell completions"},
    {"zsh", "Generate zsh shell completions"},
};

/* Known signals */
const swordfish_signal_t signals[] = {
    {"HUP", SIGHUP}, {"INT", SIGINT}, {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"TERM", SIGTERM},
    {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"STOP", SIGSTOP}, {"CONT", SIGCONT},
};
const size_t signals_count = sizeof(signals) / sizeof(signals[0]);

const swordfish_help_category_info_t help_categories[] = {
    {"arguments", "Arguments", "Full argument reference"},
    {"general", "General", "Basic usage and common flags"},
    {"signals", "Signals", "How Swordfish sends signals"},
    {"filter", "Filtering", "Which processes are matched"},
    {"behavior", "Behavior", "Confirmation and execution behavior"},
    {"misc", "Misc", "Completions, versioning, and man pages"},
};
const size_t help_category_count = sizeof(help_categories) / sizeof(help_categories[0]);

static void print_embedded(FILE *out, const unsigned char *start, const unsigned char *end) {
    fwrite(start, 1, (size_t)(end - start), out);
}

/* Prints the usage block
   Usually called on "-h" */
void usage(const char *prog) {
    const int usage_indent = 11;
    const int usage_indent_d = 1;

    printf("Swordfish -- A fast process manager\n"
           "Usage: %s [operation] [modifiers] pattern [pattern ...]\n\n",
           prog);

    printf("Operations (mutually exclusive):\n");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        if (!swordfish_options[i].common) continue;
        if (strcmp(swordfish_options[i].category, "operation") != 0) continue;
        printf("  %-*s%s\n", usage_indent, swordfish_options[i].flag,
               swordfish_options[i].desc);
    }

    printf("\nModifiers (stackable):\n");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        if (!swordfish_options[i].common) continue;
        if (strcmp(swordfish_options[i].category, "modifier") != 0) continue;
        printf("  %-*s%s\n", usage_indent, swordfish_options[i].flag,
               swordfish_options[i].desc);
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
        } else if (strcmp(category, "arguments") == 0) {
            man_args(stdout, false);
            return;
        } else {
            ERROR("Unknown help category: %s", category);
            ERROR("Run '%s --help' to see available categories", "swordfish");
            return;
        }
    }

    print_embedded(stdout, start, end);
}

static void skip_header(const unsigned char **start, const unsigned char *end) {
    int newlines = 0;
    const unsigned char *p = *start;
    while (p < end && newlines < 2) {
        if (*p == '\n') newlines++;
        p++;
    }
    *start = p;
}

static void man_general(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_general_txt_start;
    const unsigned char *end = _binary_docs_man_general_txt_end;
    if (isman) skip_header(&start, end);
    print_embedded(out, start, end);
}

static void man_signals(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_signals_txt_start;
    const unsigned char *end = _binary_docs_man_signals_txt_end;
    if (isman) skip_header(&start, end);
    print_embedded(out, start, end);
}

static void man_filter(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_filter_txt_start;
    const unsigned char *end = _binary_docs_man_filter_txt_end;
    if (isman) skip_header(&start, end);
    print_embedded(out, start, end);
}

static void man_behavior(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_behavior_txt_start;
    const unsigned char *end = _binary_docs_man_behavior_txt_end;
    if (isman) skip_header(&start, end);
    print_embedded(out, start, end);
}

static void man_misc(FILE *out, bool isman) {
    const unsigned char *start = _binary_docs_man_misc_txt_start;
    const unsigned char *end = _binary_docs_man_misc_txt_end;
    if (isman) skip_header(&start, end);
    print_embedded(out, start, end);
}

static void man_args(FILE *out, bool isman) {
    const int indent = 24;

    if (!isman) {
        fprintf(out, "Arguments -- Full Argument Reference -- Compiled for v%s\n\n", SWORDFISH_VERSION);
    }

    fprintf(out, "Operations (mutually exclusive, at most one per invocation):\n");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        if (strcmp(swordfish_options[i].category, "operation") != 0) continue;
        fprintf(out, "  %-*s%s\n", indent, swordfish_options[i].flag, swordfish_options[i].desc);
    }

    fprintf(out, "\nModifiers (stackable, combine freely with operations):\n");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        if (strcmp(swordfish_options[i].category, "modifier") != 0) continue;
        fprintf(out, "  %-*s%s\n", indent, swordfish_options[i].flag, swordfish_options[i].desc);
    }

    fprintf(out, "\nFiltering (long opts):\n");
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        if (strcmp(swordfish_options_lopt[i].category, "filter") != 0) continue;
        char buf[64] = {0};
        if (swordfish_options_lopt[i].arg)
            snprintf(buf, sizeof(buf), "%s %s", swordfish_options_lopt[i].flag, swordfish_options_lopt[i].arg);
        else
            snprintf(buf, sizeof(buf), "%s", swordfish_options_lopt[i].flag);
        fprintf(out, "  %-*s%s\n", indent, buf, swordfish_options_lopt[i].desc);
    }

    fprintf(out, "\nBehavior (long opts):\n");
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        if (strcmp(swordfish_options_lopt[i].category, "behavior") != 0) continue;
        char buf[64] = {0};
        if (swordfish_options_lopt[i].arg)
            snprintf(buf, sizeof(buf), "%s %s", swordfish_options_lopt[i].flag, swordfish_options_lopt[i].arg);
        else
            snprintf(buf, sizeof(buf), "%s", swordfish_options_lopt[i].flag);
        fprintf(out, "  %-*s%s\n", indent, buf, swordfish_options_lopt[i].desc);
    }

    fprintf(out, "\nHooks (long opts):\n");
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        if (strcmp(swordfish_options_lopt[i].category, "hook") != 0) continue;
        char buf[64] = {0};
        if (swordfish_options_lopt[i].arg)
            snprintf(buf, sizeof(buf), "%s %s", swordfish_options_lopt[i].flag, swordfish_options_lopt[i].arg);
        else
            snprintf(buf, sizeof(buf), "%s", swordfish_options_lopt[i].flag);
        fprintf(out, "  %-*s%s\n", indent, buf, swordfish_options_lopt[i].desc);
    }

    fprintf(out, "\nMisc (long opts):\n");
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        if (strcmp(swordfish_options_lopt[i].category, "misc") != 0) continue;
        char buf[64] = {0};
        if (swordfish_options_lopt[i].arg)
            snprintf(buf, sizeof(buf), "%s %s", swordfish_options_lopt[i].flag, swordfish_options_lopt[i].arg);
        else
            snprintf(buf, sizeof(buf), "%s", swordfish_options_lopt[i].flag);
        fprintf(out, "  %-*s%s\n", indent, buf, swordfish_options_lopt[i].desc);
    }
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
        if (opt->arg)
            fprintf(out, " [%s %s]", opt->flag, opt->arg);
        else
            fprintf(out, " [%s]", opt->flag);
    }
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        const swordfish_option_t *opt = &swordfish_options_lopt[i];
        if (opt->arg)
            fprintf(out, " [%s %s]", opt->flag, opt->arg);
        else
            fprintf(out, " [%s]", opt->flag);
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

    fprintf(out, ".SH ARGUMENTS\n"); 
    man_args(out, true);

    fprintf(out, ".SH EXAMPLES\n");
    for (size_t i = 0; i < swordfish_usage_count; ++i) {
        fprintf(out, ".TP\n%s\n%s\n", swordfish_usage[i].usage, swordfish_usage[i].desc);
    }

    fprintf(out, ".SH SUPORTED SIGNALS\n");
    for (size_t i = 0; i < signals_count; ++i) {
        fprintf(out, ".TP\n%s : Signal number %d\n", signals[i].name, signals[i].sig);
    }

    if (path)
        fclose(out);
}
