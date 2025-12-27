#include "args.h"
#include "hooks.h"
#include "main.h"
#include "process.h"

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

/* Flag descriptions
   Remember that this effects the completion generation
   Update REGULARLY!!!
*/
const swordfish_flag_desc_t swordfish_flags[] = {
    {"-S", "Select which PIDs to kill (interactive prompt)"},
    {"-k", "Send SIGTERM to matching processes (Graceful shutdown)"},
    {"-K", "Send SIGKILL to matching processes (Forceful shutdown)"},
    {"-x", "Exact match process names (default: substring match)"},
    {"-y", "Auto-confirm kills; skip prompts and sudo confirmation"},
    {"-p", "Print raw PIDs only"},
    {"-t", "Always select the top process"},
    {"-v", "Increase verbosity level up to -vvv for maximum verbosity"},
    {"-h", "Show help message"},
    {"-r <time>", "Retry on failure after waiting <time> seconds"},
    {"-<SIGNAL>", "Shorthand to specify signal (e.g. -9, -KILL)"},
    {"-u <USER>", "Filter processes by username"},
    {"?<ram|age>", "Sort process list by RAM or age (e.g., ?ram, ?age)"},
    {"--sort <ram|age>", "Sort process list by RAM or age"},
    {"--exclude <pattern>", "Exclude processes matching pattern"},
    {"--help/-h", "Shows this help message"},
    {"--pre-hook <script>", "Run <script> before sending signals"},
    {"--post-hook <script>", "Run <script> after sending signals"},
    {"--completions <shell>", "Generate shell completions for <shell> (fish, bash, zsh) and output to file if provided"},
    {"--version", "Shows the current version of your Swordfish install. That's it."},
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

const swordfish_completion_guide_t swordfish_completion_guide[] = {
    {"fish", "Generate fish shell completions"},
    {"bash", "Generate bash shell completions"},
    {"zsh", "Generate zsh shell completions"},
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

    // Output flag list and descriptions
    printf("== Swordfish : A pkill-like CLI tool ==\n\n");
    printf("== Usage ==\n  %s -%s pattern [pattern ...]\n\n", prog, short_opts);
    printf("== Options ==\n");
    for (size_t i = 0; i < swordfish_flags_count; ++i) {
        printf("  %-*s%s\n", options_indent, swordfish_flags[i].flag, swordfish_flags[i].desc);
    }

    // Output usage examples
    printf("\n== Patterns ==:\n  One or more patterns to match process names against.\n  Matching is "
           "case-insensitive substring unless -x is used.\n\nExamples:\n");
    for (size_t i = 0; i < swordfish_usage_count; ++i) {
        printf("  ");
        printf(swordfish_usage[i].usage, prog);
        printf("%*s%s\n", examples_indent - (int)strlen(swordfish_usage[i].usage), "", swordfish_usage[i].desc);
    }

    // Output known signals
    printf("\n== Recognized Signals ==\n");
    for (size_t i = 0; i < signals_count; ++i) {
        printf("  %-5s : Signal number %d\n", signals[i].name, signals[i].sig);
    }
    printf("\n");

    // Output completion guide
    printf("== Shell Completions ==\n");
    printf("Swordfish can generate shell completions for the following shells:\n");
    for (size_t i = 0; i < sizeof(swordfish_completion_guide) / sizeof(swordfish_completion_guide[0]); ++i) {
        printf("  %-5s : %s\n", swordfish_completion_guide[i].name,
               swordfish_completion_guide[i].desc);
    }
    printf("By running '%s --completions <shell> [file]'\n", prog);
    printf("Or outputting to stdout if no file is provided.\n");
    printf("\n");
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
int parse_args(int *argc, char **argv, swordfish_args_t *args) {
    int local_argc = *argc;

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
    args->verbose_level = 0;
    args->sort_mode = SWSORT_NONE;
    args->sort_key = NULL;
    args->exclude_patterns = NULL;
    args->exclude_count = 0;
    args->top_only = false;
    args->retry_time = 0;
    args->run_static = false;
    args->pre_hook[0] = '\0';
    args->post_hook[0] = '\0';

    static const char *exclude_patterns[MAX_EXCLUDE_PATTERNS];
    int exclude_count = 0;

    static struct option long_opts[] = {
        {"sort", required_argument, NULL, 1000},
        {"help", no_argument, NULL, 1001},
        {"exclude", required_argument, NULL, 1002},
        {"pre-hook", required_argument, NULL, 1003},
        {"post-hook", required_argument, NULL, 1004},
        {"completions", required_argument, NULL, 1005},
        {"version", no_argument, NULL, 1006},
        {0, 0, 0, 0}
    };

    for (int i = 1; i < local_argc; i++) {
        if (!argv[i]) continue;
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            const char *p = argv[i] + 1;
            int all_digits = 1;
            while (*p) {
                if (!isdigit((unsigned char)*p)) {
                    all_digits = 0;
                    break;
                }
                p++;
            }
            if (all_digits) {
                args->sig = atoi(argv[i] + 1); // skip the leading '-'
                args->do_sig = true;
                args->sig_str = argv[i];

                // erase so getopt_long ignores it
                argv[i] = NULL;
            }
        }
    }
    // compact argv to remove NULLs so getopt works cleanly
    int write = 1;
    for (int read = 1; read < local_argc; read++) {
        if (argv[read] != NULL) {
            argv[write++] = argv[read];
        }
    }
    local_argc = write;
    argv[local_argc] = NULL;
    optind = 1;

    int opt, longindex = 0;
    while ((opt = getopt_long(local_argc, argv, short_opts, long_opts, &longindex)) != -1) {
        switch (opt) {
            case 'S': args->select_mode = true; break;
            case 'k': args->do_term = true; args->do_sig = true; break;
            case 'K': args->do_kill = true; args->do_sig = true; break;
            case 'x': args->exact_match = true; break;
            case 'y': args->auto_confirm = true; break;
            case 'p': args->print_pids_only = true; break;
            case 'u': args->user = optarg; break;
            case 'v': if (args->verbose_level < 3) args->verbose_level++; break;
            case 't': args->top_only = true; break;
            case 'r': args->retry_time = atoi(optarg); if (args->retry_time < 0) args->retry_time = 0; break;
            case 'h': usage(argv[0]); exit(0); break;

            // Long opts
            case 1000: args->sort_key = optarg; break;
            case 1001: help(argv[0]); exit(0); break;
            case 1002: 
                if (exclude_count < MAX_EXCLUDE_PATTERNS) exclude_patterns[exclude_count++] = optarg;
                else { ERROR("Too many --exclude patterns"); return 1; }
                break;
            case 1003: safe_strncpy(args->pre_hook, optarg, sizeof(args->pre_hook)); break;
            case 1004: safe_strncpy(args->post_hook, optarg, sizeof(args->post_hook)); break;
            case 1005: // --completions
                if (strcmp(optarg, "fish") == 0) generate_fish_completions(argv[0], (optind < *argc) ? argv[optind++] : NULL);
                else if (strcmp(optarg, "bash") == 0) generate_bash_completions(argv[0], (optind < *argc) ? argv[optind++] : NULL);
                else if (strcmp(optarg, "zsh") == 0) generate_zsh_completions(argv[0], (optind < *argc) ? argv[optind++] : NULL);
            
                else { ERROR("Unknown shell for completions: %s", optarg); return 1; }
                exit(0);
                break;

            case 1006: printf("Swordfish %s\n", SWORDFISH_VERSION); exit(0); break;

            default:
                // ERROR("Test");
                return 1;
        }
    }

    // Handle ?ram / ?age arguments
    for (int i = 1; i < local_argc; i++) {
        if (strcmp(argv[i], "?ram") == 0) {
            args->sort_mode = SWSORT_RAM;
        } else if (strcmp(argv[i], "?age") == 0) {
            args->sort_mode = SWSORT_AGE;
        } else if (argv[i][0] == '?') {
            ERROR("Unknown sort mode: %s", argv[i]);
            return 1;
        }
    }

    // Only set pattern_start_idx once, after all parsing
    args->pattern_start_idx = optind;
    args->exclude_patterns = exclude_patterns;
    args->exclude_count = exclude_count;

    if ((local_argc - args->pattern_start_idx) >= 1 && !args->do_sig && !args->do_term && !args->do_kill) {
        INFO("static run");
        args->run_static = true;
    }

    // Ensure at least one pattern
    if (args->pattern_start_idx >= local_argc) {
        ERROR("Missing process name pattern(s)");
        return 1;
    }
    *argc = local_argc;
    return 0;
}

