#include "args.h"
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef NSIG
#define NSIG 65
#endif

const swordfish_flag_desc_t swordfish_flags[] = {
    {"-S", "Select which PIDs to kill (interactive prompt)"},
    {"-k", "Actually send the signal (default is to only list matches)"},
    {"-x", "Exact match process names (default: substring match)"},
    {"-y", "Auto-confirm kills; skip prompts and sudo confirmation"},
    {"-p", "Print raw PIDs only"},
    {"-<SIGNAL>", "Shorthand to specify signal (e.g. -9, -KILL)"},
    {"-u <USER>", "Filter processes by username"},
    {"-v", "Enable verbose output"},
    {"--help", "Show this help message and exit"},
};
const size_t swordfish_flags_count = sizeof(swordfish_flags) / sizeof(swordfish_flags[0]);

const swordfish_usage_example_t swordfish_examples[] = {
    {"%s -k firefox", "Kill all processes with 'firefox' in the name"},
    {"%s -kx bash", "Kill all exact matches of 'bash'"},
    {"%s -Sk KILL vim", "Interactively select vim processes and send SIGKILL"},
    {"%s -ky firefox vim bash",
     "Kill all 'firefox', 'vim', and 'bash' processes without confirmation"},
};
const size_t swordfish_examples_count = sizeof(swordfish_examples) / sizeof(swordfish_examples[0]);

const swordfish_signal_t signals[] = {
    {"HUP", SIGHUP},   {"INT", SIGINT},   {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"TERM", SIGTERM},
    {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"STOP", SIGSTOP}, {"CONT", SIGCONT},
};
const size_t signals_count = sizeof(signals) / sizeof(signals[0]);

void usage(const char *prog) {
    fprintf(stderr,
            "Swordfish : A pkill-like CLI tool\n"
            "Usage: %s -[Skxypu:v] pattern [pattern ...]\n",
            prog);
    for (size_t i = 0; i < swordfish_flags_count; ++i) {
        fprintf(stderr, "  %-14s: %s\n", swordfish_flags[i].flag, swordfish_flags[i].desc);
    }
    fprintf(stderr, "  pattern       : One or more process name patterns\n");
    fprintf(stderr, "For more information, please run '%s --help'\n", prog);
}

void help(const char *prog) {
    printf("Swordfish : A pkill-like CLI tool\n\n");
    printf("Usage:\n  %s [OPTIONS] pattern [pattern ...]\n\n", prog);
    printf("Options:\n");
    for (size_t i = 0; i < swordfish_flags_count; ++i) {
        printf("  %-16s%s\n", swordfish_flags[i].flag, swordfish_flags[i].desc);
    }
    printf("\nPatterns:\n  One or more patterns to match process names against.\n  Matching is "
           "case-insensitive substring unless -x is used.\n\nExamples:\n");
    for (size_t i = 0; i < swordfish_examples_count; ++i) {
        printf("  ");
        printf(swordfish_examples[i].usage, prog);
        printf("%*s%s\n", 30 - (int)strlen(swordfish_examples[i].usage), "",
               swordfish_examples[i].desc);
    }
}

static bool is_numeric(const char *s) {
    while (*s) {
        if (!isdigit(*s++))
            return false;
    }
    return true;
}

int get_signal(const char *sigstr) {
    if (is_numeric(sigstr)) {
        int signum = atoi(sigstr);
        if (signum > 0 && signum < NSIG)
            return signum;
        return -1;
    }

    for (size_t i = 0; i < signals_count; i++) {
        if (strcasecmp(sigstr, signals[i].name) == 0)
            return signals[i].sig;
    }
    return -1;
}

int parse_args(int argc, char **argv, swordfish_args_t *args) {
    // Initialize defaults before any argument parsing
    args->sig_str = "TERM";
    args->sig = SIGTERM;
    args->do_kill = false;
    args->select_mode = false;
    args->exact_match = false;
    args->print_pids_only = false;
    args->auto_confirm = false;
    args->user = NULL;
    args->do_verbose = false;

    // Check for --help before getopt
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            help(argv[0]);
            exit(0);
        }
    }

    // Support -<SIGNAL> as shorthand (e.g. -9, -KILL, -TERM)
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] && argv[1][1] != '-' &&
        (isdigit(argv[1][1]) || isalpha(argv[1][1]))) {
        const char *sigstr = argv[1] + 1;
        int sig = get_signal(sigstr);
        if (sig != -1) {
            args->do_kill = true;
            args->sig = sig;
            args->sig_str = sigstr;
            // Remove this arg from argv for getopt
            for (int i = 1; i < argc - 1; ++i)
                argv[i] = argv[i + 1];
            argc--;
        }
    }

    int opt;
    while ((opt = getopt(argc, argv, "Skxypu:v")) != -1) {
        switch (opt) {
        case 'S':
            args->select_mode = true;
            break;
        case 'k':
            args->do_kill = true;
            break;
        case 'x':
            args->exact_match = true;
            break;
        case 'y':
            args->auto_confirm = true;
            break;
        case 'p':
            args->print_pids_only = true;
            break;
        case 'u':
            args->user = optarg;
            break;
        case 'v':
            args->do_verbose = true;
            break;
        default:
            usage(argv[0]);
            return 2;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 2;
    }

    args->pattern_start_idx = optind;
    // Only override sig if not already set by -<SIGNAL>
    if (!args->do_kill || (args->sig_str && strcmp(args->sig_str, "TERM") == 0))
        args->sig = get_signal(args->sig_str);

    if (args->sig == -1) {
        fprintf(stderr, "Unknown signal: %s\n", args->sig_str);
        return 2;
    }

    return 0;
}
