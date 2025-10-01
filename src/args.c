#include "args.h"
#include "hooks.h"
#include "main.h"

#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Define NSIG since for some reason it breaks without it*/
#ifndef NSIG
#define NSIG 65
#endif

#define MAX_EXCLUDE_PATTERNS 16
static const char *short_opts = "SKkxyptvhr:u:";

/* Flag descriptions */
const swordfish_flag_desc_t swordfish_flags[] = {
    {"-S", "Select which PIDs to kill (interactive prompt)"},
    {"-k", "Send SIGTERM to matching processes (Graceful shutdown)"},
    {"-K", "Send SIGKILL to matching processes (Forceful shutdown)"},
    {"-x", "Exact match process names (default: substring match)"},
    {"-y", "Auto-confirm kills; skip prompts and sudo confirmation"},
    {"-p", "Print raw PIDs only"},
    {"-t", "Always select the top process"},
    {"-v", "Enable verbose output"},
    {"-r <time>", "Retry on failure after waiting <time> seconds"},
    {"-<SIGNAL>", "Shorthand to specify signal (e.g. -9, -KILL)"},
    {"-u <USER>", "Filter processes by username"},
    {"--sort <cpu|ram|age>", "Sort process list by CPU, RAM, or age"},
    {"--exclude <pattern>", "Exclude processes matching pattern"},
    {"--help/-h", "Shows this help message"},
};

/* Usage examples */
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

const size_t swordfish_flags_count = sizeof(swordfish_flags) / sizeof(swordfish_flags[0]);
const size_t swordfish_usage_count = sizeof(swordfish_usage) / sizeof(swordfish_usage[0]);

/* Known signals */
const swordfish_signal_t signals[] = {
    {"HUP", SIGHUP},   {"INT", SIGINT},   {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"TERM", SIGTERM},
    {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"STOP", SIGSTOP}, {"CONT", SIGCONT},
};
const size_t signals_count = sizeof(signals) / sizeof(signals[0]);

/* Prints the usage block
   Usually called on "-h" */
void usage(const char *prog) {
    const int usage_indent = 31;

    printf("Swordfish : A pkill-like CLI tool\n"
           "Usage: %s -%s pattern [pattern ...]\n",
           prog, short_opts);
    for (size_t i = 0; i < swordfish_usage_count; ++i) {
        printf("  ");
        printf(swordfish_usage[i].usage, prog);
        printf("%*s%s\n", usage_indent - (int)strlen(swordfish_usage[i].usage), "", swordfish_usage[i].desc);
    }
    printf("  pattern %-36s%s  One or more process name patterns\n", "", "");
    printf("For more information, please run '%s --help'\n", prog);
}

/* Prints full help block
   Usually called on "--help" */
void help(const char *prog) {
    const int options_indent = 22;
    const int examples_indent = 31;

    printf("Swordfish : A pkill-like CLI tool\n\n");
    printf("Usage:\n  %s -%s pattern [pattern ...]\n\n", prog, short_opts);
    printf("Options:\n");
    for (size_t i = 0; i < swordfish_flags_count; ++i) {
        printf("  %-*s%s\n", options_indent, swordfish_flags[i].flag, swordfish_flags[i].desc);
    }
    printf("\nPatterns:\n  One or more patterns to match process names against.\n  Matching is "
           "case-insensitive substring unless -x is used.\n\nExamples:\n");
    for (size_t i = 0; i < swordfish_usage_count; ++i) {
        printf("  ");
        printf(swordfish_usage[i].usage, prog);
        printf("%*s%s\n", examples_indent - (int)strlen(swordfish_usage[i].usage), "", swordfish_usage[i].desc);
    }
}

static bool is_numeric(const char *s) {
    while (*s) {
        if (!isdigit(*s++))
            return false;
    }
    return true;
}

/* Gets the signal number from a string
   Returns -1 if failed */
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

/* Parse command-line arguments. Returns 0 on success, 1 on error
   Handles argument parsing and validation */
int parse_args(int argc, char **argv, swordfish_args_t *args) {
    // Initialize defaults
    args->sig_str = "TERM";
    args->sig = SIGTERM;
    args->do_term = false;
    args->do_kill = false;
    args->do_sig = false;
    args->select_mode = false;
    args->exact_match = false;
    args->print_pids_only = false;
    args->auto_confirm = false;
    args->user = NULL;
    args->do_verbose = false;
    args->sort_mode = SWSORT_NONE;
    args->sort_key = NULL;
    args->exclude_patterns = NULL;
    args->exclude_count = 0;
    args->top_only = false;
    args->retry_time = 0;
    args->run_static = false;
    args->pre_hook[0] = '\0';
    args->post_hook[0] = '\0';

    // Step 1: Pre-scan for -<SIGNAL> args like -9, -KILL, -TERM, -SIGTERM
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] && argv[i][1] != '-') {
            const char *sigstr = argv[i] + 1;
            char sigbuf[32];
            // Accept -SIGTERM as well as -TERM
            if (strncasecmp(sigstr, "SIG", 3) == 0) {
                strncpy(sigbuf, sigstr + 3, sizeof(sigbuf) - 1);
                sigbuf[sizeof(sigbuf) - 1] = '\0';
                sigstr = sigbuf;
            }
            // Only try as a signal if numeric OR matches a known signal name
            bool maybe_signal = is_numeric(sigstr);
            if (!maybe_signal) {
                for (size_t s = 0; s < signals_count; s++) {
                    if (strcasecmp(sigstr, signals[s].name) == 0) {
                        maybe_signal = true;
                        break;
                    }
                }
            }
            if (!maybe_signal) {
                // Not a signal, leave it for getopt
                continue;
            }
            int sig = get_signal(sigstr);
            if (sig != -1) {
                args->do_sig = true;
                args->sig = sig;
                args->sig_str = sigstr;
                // Remove this arg so getopt doesn’t see it
                for (int j = i; j < argc - 1; j++) {
                    argv[j] = argv[j + 1];
                }
                argc--;
                i--; // re-check current index
            } else {
                ERROR("Unknown signal: %s", sigstr);
                return 1;
            }
        }
    }

    // Step 2: Define long options
    static struct option long_opts[] = {
        {"sort", required_argument, NULL, 1000},      {"help", no_argument, NULL, 1001},
        {"exclude", required_argument, NULL, 1002},   {"pre-hook", required_argument, NULL, 1003},
        {"post-hook", required_argument, NULL, 1004}, {0, 0, 0, 0}};

    // Step 3: Parse grouped short flags and long opts
    int opt, longindex = 0;
    static const char *exclude_patterns[MAX_EXCLUDE_PATTERNS];
    int exclude_count = 0;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, &longindex)) != -1) {
        switch (opt) {
        case 'S':
            args->select_mode = true;
            break;
        case 'k':
            args->do_term = true;
            break;
        case 'K':
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
        case 't':
            args->top_only = true;
            break;
        case 'r':
            args->retry_time = atoi(optarg);
            if (args->retry_time < 0)
                args->retry_time = 0;
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
            break;

        case 1000: // --sort
            args->sort_key = optarg;
            if (strcmp(args->sort_key, "cpu") == 0)
                args->sort_mode = SWSORT_CPU;
            else if (strcmp(args->sort_key, "ram") == 0)
                args->sort_mode = SWSORT_RAM;
            else if (strcmp(args->sort_key, "age") == 0)
                args->sort_mode = SWSORT_AGE;
            else {
                ERROR("Unknown sort key: %s (use -h for help)", args->sort_key);
                return 1;
            }
            break;

        case 1001: // --help
            help(argv[0]);
            exit(0);
            break;

        case 1002: // --exclude
            if (exclude_count < MAX_EXCLUDE_PATTERNS) {
                exclude_patterns[exclude_count++] = optarg;
            } else {
                ERROR("Too many --exclude patterns (max %d)", MAX_EXCLUDE_PATTERNS);
                return 1;
            }
            break;

        case 1003: // --pre-hook
            safe_strncpy(args->pre_hook, optarg, sizeof(args->pre_hook));
            break;

        case 1004: // --post-hook
            safe_strncpy(args->post_hook, optarg, sizeof(args->post_hook));
            break;

        default:
            ERROR("please specify at least one operation (use -h for help)");
            return 1;
        }
    }

    // Step 4: Ensure at least one pattern is provided
    if (argc >= 2 && optind >= argc) {
        ERROR("Missing process name pattern(s)");
        return 1;
    }

    // if there is zero args, throw error
    if (argc <= 1) {
        ERROR("please specify at least one operation (use -h for help)");
        return 1;
    }
    args->pattern_start_idx = optind;

    args->exclude_patterns = exclude_patterns;
    args->exclude_count = exclude_count;

    // Step 5: Detect "static run" mode (just pattern with no flags)
    if (argc - 1 == 1 && !args->do_sig) { // Only one argument besides argv[0]
        const char *arg = argv[1];
        if (arg[0] != '-') {
            args->run_static = true;
            args->do_term = false;
            args->do_kill = false;
            args->select_mode = false;
            args->auto_confirm = false;
        }
    }

    return 0;
}
