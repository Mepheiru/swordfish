#include "process.h"
#include "args.h"
#include "hooks.h"
#include "main.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>

/* Check if a directory name is a valid process directory */
static bool is_proc_dir(const char *name) {
    for (; *name; ++name)
        if (!isdigit(*name))
            return false;
    return true;
}

bool is_all_digits(const char *s) {
    if (!s || *s == '\0')
        return false;
    for (; *s; ++s)
        if (!isdigit(*s))
            return false;
    return true;
}

bool is_zombie_process(pid_t pid) {
    char path[PATH_MAX], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    bool zombie = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "State:", 6) == 0) {
            char state;
            sscanf(line, "State:\t%c", &state);
            zombie = (state == 'Z');
            break;
        }
    }
    fclose(f);
    return zombie;
}

static const char *get_proc_user(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "unknown";
}

static void str_to_lower(char *s) {
    for (; *s; ++s)
        *s = tolower((unsigned char)*s);
}

static bool is_output_terminal(void) {
    return isatty(STDOUT_FILENO);
}

bool is_interactive(void) {
    return isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
}

/* Parse and compile REGEX patterns */
static void compile_patterns(const swordfish_args_t *args, pattern_list_t *plist,
                             compiled_pattern_t *compiled) {
    for (int i = 0; i < plist->pattern_count; ++i) {
        const char *pat_orig = plist->patterns[i];

        // Skip ?-args
        if (pat_orig[0] == '?') {
            compiled[i].type = PAT_SKIP;
            compiled[i].pattern[0] = '\0';
            continue;
        }

        compiled_pattern_t *c = &compiled[i];

        safe_strncpy(c->pattern, pat_orig, sizeof(c->pattern) - 1);
        c->pattern[sizeof(c->pattern) - 1] = '\0';

        size_t len = strlen(c->pattern);
        bool force_exact = false;

        if ((args->exact_match) ||
            (len >= 2 && c->pattern[0] == '^' && c->pattern[len - 1] == '$')) {
            force_exact = true;
            if (!args->exact_match) {
                c->pattern[len - 1] = '\0';
                memmove(c->pattern, c->pattern + 1, strlen(c->pattern + 1) + 1);
            }
        }

        if (force_exact) {
            c->type = PAT_EXACT;
        } else {
            int rc = regcomp(&c->regex, c->pattern, REG_ICASE | REG_NOSUB | REG_EXTENDED);
            if (rc != 0) {
                char errbuf[256];
                regerror(rc, &c->regex, errbuf, sizeof(errbuf));
                ERROR("Invalid regex pattern \"%s\": %s", c->pattern, errbuf);
                c->type = PAT_SUBSTR;
            } else {
                c->type = PAT_REGEX;
            }
        }
    }
}

/* Match a process against compiled patterns */
static bool match_process(const char *name, const char *cmdline, compiled_pattern_t *compiled,
                          int pattern_count) {
    char name_lc[256], cmdline_lc[256];
    safe_strncpy(name_lc, name, sizeof(name_lc) - 1);
    name_lc[sizeof(name_lc) - 1] = '\0';
    safe_strncpy(cmdline_lc, cmdline, sizeof(cmdline_lc) - 1);
    cmdline_lc[sizeof(cmdline_lc) - 1] = '\0';
    str_to_lower(name_lc);
    str_to_lower(cmdline_lc);

    for (int i = 0; i < pattern_count; ++i) {
        compiled_pattern_t *c = &compiled[i];

        switch (c->type) {
        case PAT_EXACT:
            if (strcmp(name_lc, c->pattern) == 0 || strcmp(cmdline_lc, c->pattern) == 0)
                return true;
            break;

        case PAT_REGEX:
            if (regexec(&c->regex, name_lc, 0, NULL, 0) == 0 ||
                regexec(&c->regex, cmdline_lc, 0, NULL, 0) == 0)
                return true;
            break;

        case PAT_SUBSTR:
            if (strstr(name_lc, c->pattern) != NULL || strstr(cmdline_lc, c->pattern) != NULL)
                return true;
            break;
        }
    }
    return false;
}

/* Free compiled regex patterns to not waste memory */
static void free_compiled_patterns(compiled_pattern_t *compiled, int count) {
    for (int i = 0; i < count; ++i)
        if (compiled[i].type == PAT_REGEX)
            regfree(&compiled[i].regex);
}

/* Check if a process matches the given REGEX patterns */
static bool entry_matches(const process_info_t *p, pattern_list_t *plist,
                          const swordfish_args_t *args, compiled_pattern_t *compiled) {
    // Exclude patterns (same as before)
    if (args->exclude_patterns && args->exclude_count > 0) {
        char name_lc[256], cmdline_lc[256];
        safe_strncpy(name_lc, p->name, sizeof(name_lc));
        name_lc[sizeof(name_lc) - 1] = '\0';
        safe_strncpy(cmdline_lc, p->cmdline, sizeof(cmdline_lc));
        cmdline_lc[sizeof(cmdline_lc) - 1] = '\0';
        str_to_lower(name_lc);
        str_to_lower(cmdline_lc);
        for (int i = 0; i < args->exclude_count; ++i) {
            const char *ex = args->exclude_patterns[i];
            if (strstr(name_lc, ex) != NULL || strstr(cmdline_lc, ex) != NULL)
                return false;
        }
    }

    for (int i = 0; i < plist->pattern_count; ++i) {
        if (plist->pattern_is_pid[i] && is_all_digits(plist->patterns[i]) &&
            atoi(plist->patterns[i]) == p->pid)
            return true;
    }

    return match_process(p->name, p->cmdline, compiled, plist->pattern_count);
}

/* Helper for printing proc info based on the display mode */
static void print_proc_info(const process_info_t *p, int sig, const swordfish_args_t *args,
                            const char *prefix, bool include_signal) {
    bool tty = is_output_terminal();

    const char *owner_fmt = "%s";
    if (tty && strcmp(p->owner, "root") == 0)
        owner_fmt = "\033[33m%s\033[0m"; // only color if terminal supports it

    long uptime = 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up = 0;
        if (fscanf(f, "%lf", &up) == 1)
            uptime = (long)up;
        fclose(f);
    }
    long age_min = (uptime - (p->start_time / sysconf(_SC_CLK_TCK))) / 60;

    // Verbose
    if (args->do_verbose || !tty) {
        printf("%s %d '%s' cmdl: '%s' threads: '%s' owned by ", prefix, p->pid, p->name, p->cmdline,
               p->status.threads);
        printf(owner_fmt, p->owner);

        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
    }

    // Normal 
    else {
        printf("%s %d '%s' owned by ", prefix, p->pid, p->name);
        printf(owner_fmt, p->owner);
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
        if (tty) {
            if (args->sort_mode == SWSORT_RAM)
                printf(" [%ld MB]", p->ram);
            else if (args->sort_mode == SWSORT_AGE)
                printf(" [%ld min]", age_min > 0 ? age_min : 0);
        }
    }
    printf("\n");
}

static long get_proc_start_time(pid_t pid) {
    char stat_path[PATH_MAX], buf[1024];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    FILE *f = fopen(stat_path, "r");
    if (!f)
        return 0;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    char *p = buf;
    int field = 1;
    long start_time = 0;
    while (field <= 22 && *p) {
        if (field == 22) {
            sscanf(p, "%ld", &start_time);
            break;
        }
        if (*p == ' ')
            field++;
        p++;
    }
    return start_time;
}

static int cmp_ram(const void *a, const void *b) {
    const process_info_t *pa = a, *pb = b;
    return (pb->ram > pa->ram) - (pb->ram < pa->ram);
}

static int cmp_age(const void *a, const void *b) {
    const process_info_t *pa = a, *pb = b;
    return (pa->start_time > pb->start_time) - (pa->start_time < pb->start_time);
}

/* Check if any selected(arg2) process is owned by root */
static bool has_root_process(int count, int *selected, process_info_t *matches, int matched_total) {
    if (count > 0 && selected) {
        // Check only selected processes
        for (int i = 0; i < count; ++i)
            if (strcmp(matches[selected[i]].owner, "root") == 0)
                return true;
    } else {
        // Check all matches
        for (int i = 0; i < matched_total; ++i)
            if (strcmp(matches[i].owner, "root") == 0)
                return true;
    }
    return false;
}

/* Find all processes matching the given patterns
   Returns the number of matching processes */
static int find_matching_processes(const swordfish_args_t *args, pattern_list_t *plist,
                                   process_info_t *matches, compiled_pattern_t *compiled) {
    DIR *proc = opendir("/proc");
    if (!proc) {
        perror("opendir /proc");
        return -1;
    }

    int matched = 0;
    struct dirent *entry;
    pid_t self_pid = getpid();

    while ((entry = readdir(proc))) {
        if (!is_proc_dir(entry->d_name))
            continue;

        pid_t pid = atoi(entry->d_name);
        if (pid == self_pid)
            continue;

        process_info_t p = {0};
        p.pid = pid;

        // Read /proc/<pid>/stat for name, start time, and UID (via /proc/<pid>/status)
        char stat_path[PATH_MAX], status_path[PATH_MAX];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
        snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);

        FILE *fstat = fopen(stat_path, "r");
        if (!fstat)
            continue;

        char comm_buf[256] = {0};
        char state = '?';
        unsigned long utime = 0, stime = 0;
        long rss = 0;
        long num_threads = 0;

        // parse /proc/<pid>/stat
        // I'm sorry
        int n = fscanf(
            fstat,
            "%*d (%255[^)]) %c "          // comm, state
            "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s "  // skip 4–13
            "%lu %lu "                    // utime, stime
            "%*s %*s %*s %*s "            // skip 16–19
            "%ld"                         // num_threads
            "%*s %*s %*s "                // skip 21-23
            "%ld",                        // rss
            comm_buf,
            &state,
            &utime,
            &stime,
            &num_threads,
            &rss
        );

        fclose(fstat);

        if (n != 6)
            continue;

        safe_strncpy(p.name, comm_buf, sizeof(p.name));

        // Set the thread count
        snprintf(p.status.threads, sizeof(p.status.threads), "%ld", num_threads);

        // Get proc owner
        struct stat st;
        char procpath[64];
        snprintf(procpath, sizeof(procpath), "/proc/%d", pid);

        if (stat(procpath, &st) == 0) {
            uid_t uid = st.st_uid;
            safe_strncpy(p.owner, get_proc_user(uid), sizeof(p.owner));
        }
             
        // Read /proc/<pid>/cmdline for command line
        char cmdline_path[PATH_MAX];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
        FILE *fcmd = fopen(cmdline_path, "r");
        if (fcmd) {
            size_t n = fread(p.cmdline, 1, sizeof(p.cmdline) - 1, fcmd);
            fclose(fcmd);
            // cmdline is null-separated, replace with spaces
            for (size_t i = 0; i < n; ++i) {
                if (p.cmdline[i] == '\0') p.cmdline[i] = ' ';
            }
            p.cmdline[n] = '\0';
        } else {
            p.cmdline[0] = '\0';
        }

        // RAM calculation: use system page size
        long page_size = sysconf(_SC_PAGESIZE);
        switch (args->sort_mode) {
            case SWSORT_RAM:
                p.ram = rss * page_size / (1024 * 1024);  // convert to MB
                break;
            case SWSORT_AGE:
                p.start_time = get_proc_start_time(pid);
                break;
            default:
                // If no sort, or unknown, only fill basic info
                break;
        }

        if (args->user && strcasecmp(p.owner, args->user) != 0)
            continue;
        if (!entry_matches(&p, plist, args, compiled))
            continue;

        if (matched < MAX_MATCHES)
            matches[matched++] = p;
    }

    closedir(proc);
    return matched;
}

/* Interactive process selection for "-S" argument */
static void select_processes(int matched, process_info_t *matches, int *selected, int *count,
                             const swordfish_args_t *args, int sig) {
    if (!is_interactive()) {
        // Non-interactive → select all
        for (int i = 0; i < matched; ++i)
            selected[(*count)++] = i;
        return;
    }

    printf("Select which processes to act on:\n");
    for (int i = 0; i < matched; ++i) {
        printf("[%d] ", i + 1);
        print_proc_info(&matches[i], sig, args, "", false);
    }

    printf("Enter numbers (e.g., 1,2,5-7) or leave empty for all: ");
    char input[256] = {0};
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    if (strlen(input) == 0) {
        for (int i = 0; i < matched; ++i)
            selected[(*count)++] = i;
    } else {
        char *token = strtok(input, ",");
        while (token && *count < matched) {
            char *dash = strchr(token, '-');
            if (dash) {
                *dash = '\0';
                int start = atoi(token), end = atoi(dash + 1);
                for (int j = start; j <= end && *count < matched; ++j)
                    if (j > 0 && j <= matched)
                        selected[(*count)++] = j - 1;
            } else {
                int idx = atoi(token) - 1;
                if (idx >= 0 && idx < matched)
                    selected[(*count)++] = idx;
            }
            token = strtok(NULL, ",");
        }
    }
}

/* Confirm and act on selected processes. Text is displayed based on mode */
static void confirm_and_act(const swordfish_args_t *args, int count, int *selected,
                            process_info_t *matches) {
    // Show warning if any selected process is root
    if (!args->run_static && has_root_process(count, selected, matches, 0) && is_interactive()) {
        WARN("At least one selected process is owned by root!");
    }

    if (count == 0)
        return;

    // Use SIGKILL if -K, otherwise use whatever signal is in args->sig
    int sig = args->do_kill ? SIGKILL : args->sig;

    // Pre-kill hook
    run_hook(args->pre_hook, matches[selected[0]].pid, matches[selected[0]].name);

    // Confirm mode
    if (!args->run_static && !args->auto_confirm && is_interactive()) {
        for (int i = 0; i < count; ++i)
            print_proc_info(&matches[selected[i]], sig, args, "  PID", false);

        if (count == 1)
            printf("The process above will be affected (signal %d - %s)\n", sig, strsignal(sig));
        else
            printf("The processes above will be affected (signal %d - %s)\n", sig, strsignal(sig));
        printf("Proceed? [y/N]: ");

        // If the user confirms, proceed to the for loop below
        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (tolower(confirm[0]) != 'y') {
            return;
        }
    }

    // If the user has not specified any signal, just print proc info
    if (args->run_static) {
        for (int i = 0; i < count; ++i) {
            int idx = selected[i];
            print_proc_info(&matches[idx], sig, args, "", false);
        }
        return;
    }

    // Else, act on selected processes
    for (int i = 0; i < count; ++i) {
        int idx = selected[i];
        if (is_zombie_process(matches[idx].pid)) {
            printf("PID %d (%s) is a zombie process and may not be killed\n", matches[idx].pid,
                   matches[idx].name);
            continue;
        }

        if (kill(matches[idx].pid, sig) == 0) {
            print_proc_info(&matches[idx], sig, args, strsignal(sig), true);
        } else {
            ERROR("Failed to send signal %d to PID %d (%s): %s", sig, matches[idx].pid,
                   matches[idx].name, strerror(errno));
        }
    }
    // Post-kill hook
    run_hook(args->post_hook, matches[selected[0]].pid, matches[selected[0]].name);
}

int scan_processes(const swordfish_args_t *args, pattern_list_t *plist) {
    compiled_pattern_t compiled[MAX_PATTERNS];
    compile_patterns(args, plist, compiled);

    int tries = 0;
    while (1) {
        process_info_t matches[MAX_MATCHES];
        int matched = find_matching_processes(args, plist, matches, compiled);

        // Only sort if there are multiple matches
        if (matched > 1) {
            switch (args->sort_mode) {
                case SWSORT_RAM: qsort(matches, matched, sizeof(process_info_t), cmp_ram); break;
                case SWSORT_AGE: qsort(matches, matched, sizeof(process_info_t), cmp_age); break;
                default: break;
            }
        }

        if (args->print_pids_only) {
            for (int i = 0; i < matched; ++i)
                printf("%d\n", matches[i].pid);
            break;
        }

        int selected[MAX_MATCHES], count = 0;
        if (matched > 0) {
            if (args->top_only) {
                selected[count++] = 0;
            } else if (args->select_mode && !args->auto_confirm) {
                select_processes(matched, matches, selected, &count, args, args->sig);
            } else {
                for (int i = 0; i < matched; ++i)
                    selected[count++] = i;
            }
            confirm_and_act(args, count, selected, matches);
        } else if (tries == 0) {
            printf("No processes matched\n");
        }

        if (args->retry_time <= 0)
            break;

        if (matched == 0 || args->retry_time > 0) {
            sleep(args->retry_time);
            tries++;
        }
    }

    free_compiled_patterns(compiled, plist->pattern_count);
    return 0;
}
