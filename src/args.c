#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>

#include "args.h"
#include "help.h"
#include "hooks.h"
#include "main.h"

#ifndef NSIG
#define NSIG 65
#endif

#define MAX_EXCLUDE_PATTERNS 16

static const char *short_opts = "kSWxyvtpnwr";

static struct option long_opts[] = {
    {"sort",        required_argument, NULL, LOPT_SORT},
    {"help",        optional_argument, NULL, LOPT_HELP},
    {"exclude",     required_argument, NULL, LOPT_EXCLUDE},
    {"pre-hook",    required_argument, NULL, LOPT_PRE_HOOK},
    {"post-hook",   required_argument, NULL, LOPT_POST_HOOK},
    {"completions", required_argument, NULL, LOPT_COMPLETIONS},
    {"version",     no_argument,       NULL, LOPT_VERSION},
    {"man",         optional_argument, NULL, LOPT_MAN},
    {"user",        required_argument, NULL, LOPT_USER},
    {"retry",       required_argument, NULL, LOPT_RETRY},
    {"timeout",     required_argument, NULL, LOPT_TIMEOUT},
    {"format",      required_argument, NULL, LOPT_FORMAT},
    {"parent",      required_argument, NULL, LOPT_PARENT},
    {"session",     required_argument, NULL, LOPT_SESSION},
    {"pidfile",     required_argument, NULL, LOPT_PIDFILE},
    {0, 0, 0, 0}
};

static bool is_numeric(const char *s) {
    while (*s)
        if (!isdigit((unsigned char)*s++))
            return false;
    return true;
}

int get_signal(const char *sigstr) {
    if (is_numeric(sigstr)) {
        int signum = atoi(sigstr);
        if (signum > 0 && signum < NSIG)
            return signum;
        return -1;
    }

    for (size_t i = 0; i < signals_count; i++)
        if (strcasecmp(sigstr, signals[i].name) == 0)
            return signals[i].sig;

    return -1;
}

static void init_args(swordfish_args_t *args) {
    args->sig_str           = "TERM";
    args->sig               = SIGTERM;
    args->user              = NULL;
    args->format            = NULL;
    args->pidfile           = NULL;
    args->help_topic        = NULL;
    args->exclude_patterns  = NULL;
    args->sig               = SIGTERM;
    args->retry_time        = 0;
    args->timeout           = 0;
    args->session_id        = 0;
    args->parent_pid        = 0;
    args->exclude_count     = 0;
    args->pattern_start_idx = 0;
    args->verbose_level     = 0;
    args->operation         = SWOP_STATIC;
    args->sort_mode         = SWSORT_NONE;
    args->exact_match       = 0;
    args->auto_confirm      = 0;
    args->print_pids_only   = 0;
    args->top_only          = 0;
    args->hide_root         = 0;
    args->dry_run           = 0;
    args->wait_for_death    = 0;
    args->pre_hook[0]       = '\0';
    args->post_hook[0]      = '\0';
}

/* Extract signal embedded in -k<sig> and rewrite argv so getopt
   sees plain -k plus any trailing modifiers as a separate flag.
   Returns the extracted signal or SIGTERM if none embedded. */
static int extract_kill_signal(int *argc, char **argv, char *rewrite_buf, 
                                size_t buf_size) {
    for (int i = 1; i < *argc; i++) {
        if (!argv[i] || argv[i][0] != '-' || argv[i][1] != 'k')
            continue;

        const char *after_k = argv[i] + 2;

        /* plain -k with nothing after, or -k followed immediately
           by a lowercase modifier — no signal embedded */
        if (*after_k == '\0' || islower((unsigned char)*after_k))
            continue;

        /* consume uppercase and digit chars as the signal */
        const char *p = after_k;
        while (*p && (isupper((unsigned char)*p) || isdigit((unsigned char)*p)))
            p++;

        size_t sig_len = p - after_k;
        char sig_buf[16] = {0};
        if (sig_len >= sizeof(sig_buf))
            return -1;

        memcpy(sig_buf, after_k, sig_len);
        sig_buf[sig_len] = '\0';

        int sig = get_signal(sig_buf);
        if (sig < 0)
            return -1;

        /* rewrite -k9y as -ky so getopt handles the modifiers normally */
        if (*p != '\0') {
            snprintf(rewrite_buf, buf_size, "-%s", p);
            argv[i] = rewrite_buf;

            /* insert plain -k before the rewritten modifiers */
            if (*argc + 1 < 64) {
                memmove(&argv[i + 1], &argv[i], (*argc - i) * sizeof(char *));
                argv[i] = "-k";
                (*argc)++;
            }
        } else {
            /* -k9 with no trailing modifiers — just rewrite as -k */
            argv[i] = "-k";
        }

        return sig;
    }

    return SIGTERM;
}

int parse_args(int *argc, char **argv, swordfish_args_t *args) {
    init_args(args);

    int local_argc = *argc;
    static const char *exclude_buf[MAX_EXCLUDE_PATTERNS];
    int exclude_count = 0;

    /* rewrite buffer lives here so argv[i] can point into it safely */
    char kill_rewrite[32] = {0};

    int sig = extract_kill_signal(&local_argc, argv, kill_rewrite,
                                   sizeof(kill_rewrite));
    if (sig < 0) {
        ERROR("Invalid signal specified");
        return 1;
    }
    args->sig = sig;

    optind = 1;
    int opt, longindex = 0;

    while ((opt = getopt_long(local_argc, argv, short_opts,
                               long_opts, &longindex)) != -1) {
        switch (opt) {

        /* Operations — mutually exclusive */
        case 'k':
            if (args->operation != SWOP_STATIC) {
                ERROR("Only one operation flag allowed");
                return 1;
            }
            args->operation = SWOP_KILL;
            break;

        case 'S':
            if (args->operation != SWOP_STATIC) {
                ERROR("Only one operation flag allowed");
                return 1;
            }
            args->operation = SWOP_SELECT;
            break;

        case 'W':
            if (args->operation != SWOP_STATIC) {
                ERROR("Only one operation flag allowed");
                return 1;
            }
            args->operation = SWOP_WATCH;
            break;

        /* Modifiers */
        case 'x': args->exact_match     = 1; break;
        case 'y': args->auto_confirm    = 1; break;
        case 'p': args->print_pids_only = 1; break;
        case 't': args->top_only        = 1; break;
        case 'n': args->dry_run         = 1; break;
        case 'w': args->wait_for_death  = 1; break;
        case 'r': args->hide_root       = 1; break;

        case 'v':
            /* cap at 3 — beyond that verbosity is meaningless */
            if (args->verbose_level < 3)
                args->verbose_level++;
            break;

        /* Long opts */
        case LOPT_SORT:
            if (strcmp(optarg, "ram") == 0)
                args->sort_mode = SWSORT_RAM;
            else if (strcmp(optarg, "age") == 0)
                args->sort_mode = SWSORT_AGE;
            else {
                ERROR("Unknown sort mode: %s (expected ram or age)", optarg);
                return 1;
            }
            break;

        case LOPT_HELP:
            if (optarg) {
                args->help_topic = optarg;
            } else if (optind < local_argc && argv[optind][0] != '-') {
                args->help_topic = argv[optind++];
            } else {
                args->help_topic = NULL;
            }
            args->pattern_start_idx = optind;
            return 0;

        case LOPT_EXCLUDE:
            if (exclude_count >= MAX_EXCLUDE_PATTERNS) {
                ERROR("Too many --exclude patterns (max %d)", MAX_EXCLUDE_PATTERNS);
                return 1;
            }
            exclude_buf[exclude_count++] = optarg;
            break;

        case LOPT_PRE_HOOK:
            safe_strncpy(args->pre_hook, optarg, sizeof(args->pre_hook));
            break;

        case LOPT_POST_HOOK:
            safe_strncpy(args->post_hook, optarg, sizeof(args->post_hook));
            break;

        case LOPT_COMPLETIONS:
            if (strcmp(optarg, "fish") == 0)
                generate_fish_completions(argv[0], 
                    (optind < *argc) ? argv[optind++] : NULL);
            else if (strcmp(optarg, "bash") == 0)
                generate_bash_completions(argv[0],
                    (optind < *argc) ? argv[optind++] : NULL);
            else if (strcmp(optarg, "zsh") == 0)
                generate_zsh_completions(argv[0],
                    (optind < *argc) ? argv[optind++] : NULL);
            else {
                ERROR("Unknown shell: %s (expected fish, bash, or zsh)", optarg);
                return 1;
            }
            exit(0);

        case LOPT_VERSION:
            printf("Swordfish %s\n", SWORDFISH_VERSION);
            exit(0);

        case LOPT_MAN: {
            const char *path = (optind < *argc) ? argv[optind++] : NULL;
            gen_man(path);
            exit(0);
        }

        case LOPT_USER:
            args->user = optarg;
            break;

        case LOPT_RETRY:
            args->retry_time = atoi(optarg);
            if (args->retry_time < 0)
                args->retry_time = 0;
            break;

        case LOPT_TIMEOUT:
            args->timeout = atoi(optarg);
            if (args->timeout < 0)
                args->timeout = 0;
            break;

        case LOPT_FORMAT:
            args->format = optarg;
            break;

        case LOPT_PARENT:            
            args->parent_pid = (pid_t)atoi(optarg);
            break;

        case LOPT_SESSION:
            args->session_id = atoi(optarg);
            break;

        case LOPT_PIDFILE:
            args->pidfile = optarg;
            break;

        default:
            return 1;
        }
    }

    args->pattern_start_idx = optind;
    args->exclude_patterns  = exclude_buf;
    args->exclude_count     = exclude_count;

    /* sig is only meaningful for kill operation — reset to SIGTERM
       if user somehow passed a signal without -k */
    if (args->operation != SWOP_KILL)
        args->sig = SIGTERM;

    /* no pattern and no pidfile means nothing to match against */
    if (args->pattern_start_idx >= local_argc && !args->pidfile) {
        ERROR("Missing process name pattern(s)");
        return 1;
    }

    *argc = local_argc;
    return 0;
}
