#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "process.h"
#include "hooks.h"
#include "main.h"
#include "args.h"
#include "tui.h"

/* Check if a directory name is a valid process directory */
static bool is_proc_dir(const char *name) {
    for (; *name; ++name)
        if (!isdigit(*name))
            return false;
    return true;
}

bool is_all_digits(const char *s) {
    if (!s || !*s)
        return false;

    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < '0' || *p > '9')
            return false;
        p++;
    }
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
    static struct {
        uid_t uid;
        char name[64];
        int set;
    } uidCache[64];
    static int count = 0;

    for (int i = 0; i < count; i++)
        if (uidCache[i].uid == uid)
        return uidCache[i].name;
       
    struct passwd *pw = getpwuid(uid);
    const char *name = pw ? pw->pw_name : "unknown";

    if (count < 64) {
        uidCache[count].uid = uid;
        strncpy(uidCache[count].name, name, 63);
        uidCache[count].name[63] = '\0';
        count++;
        return uidCache[count - 1].name;
    }

    /* cache full — return the looked-up name directly */
    return name;
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

static inline bool looks_like_regex(const char *s) {
    while (*s) {
        switch (*s) {
        case '^':
        case '$':
        case '.':
        case '*':
        case '+':
        case '?':
        case '[':
        case ']':
        case '(':
        case ')':
        case '{':
        case '}':
        case '|':
        case '\\':
            return true;
        }
        s++;
    }
    return false;
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

        bool has_regex = strpbrk(c->pattern, "^$.*+?[](){}|\\") != NULL;

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
            str_to_lower(c->pattern);
        } else if (has_regex) {
            int rc = regcomp(&c->regex, c->pattern, REG_ICASE | REG_NOSUB | REG_EXTENDED);
            if (rc != 0) {
                char errbuf[256];
                regerror(rc, &c->regex, errbuf, sizeof(errbuf));
                ERROR("Invalid regex pattern \"%s\": %s", c->pattern, errbuf);
                c->type = PAT_SUBSTR;
                str_to_lower(c->pattern);
            } else {
                c->type = PAT_REGEX;
            }
        } else {
            c->type = PAT_SUBSTR;
            str_to_lower(c->pattern);
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
    if (plist->pattern_count == 0)
        return true;
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

static void fmt_ram(char *buf, size_t len, long ram_mb) {
    if (ram_mb <= 0)
        snprintf(buf, len, "-");
    else if (ram_mb >= 1024)
        snprintf(buf, len, "%.1f GiB", ram_mb / 1024.0);
    else
        snprintf(buf, len, "%.1f MiB", (double)ram_mb);
}

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
    char ram_buf[32]; fmt_ram(ram_buf, sizeof(ram_buf), p->ram);

    // Verbose
    if (args->verbose_level >= 3 || !tty) {
        printf("[%c] %d \"%s\" cmdl: \"%s\" threads: \"%s\" owned by ", p->status.state, p->pid,
               p->name, p->cmdline, p->status.threads);
        printf(owner_fmt, p->owner);
        printf("\n    RAM: \"%s\" AGE: \"%ld min\"", ram_buf, age_min);

        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
    } else if (args->verbose_level == 2) {
        printf("[%c] %d \"%s\" cmdl: \"%s\" threads: \"%s\" owned by ", p->status.state, p->pid,
               p->name, p->cmdline, p->status.threads);
        printf(owner_fmt, p->owner);

        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
    } else if (args->verbose_level == 1) {
        // Medium
        printf("%d \"%s\" cmdl: \"%s\" threads: \"%s\" owned by ", p->pid, p->name, p->cmdline,
               p->status.threads);
        printf(owner_fmt, p->owner);
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
    }

    // Normal
    else {
        printf("%s %d \"%s\" owned by ", prefix, p->pid, p->name);
        printf(owner_fmt, p->owner);
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
    }

    // Append sort modes if any are enabled
    if (tty) {
        if (args->sort_mode == SWSORT_RAM)
            printf(" [%s]", ram_buf);
        else if (args->sort_mode == SWSORT_AGE)
            printf(" [%ld min]", age_min > 0 ? age_min : 0);
    }

    printf("\n");
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
    long page_size = sysconf(_SC_PAGESIZE);

    while ((entry = readdir(proc))) {
        if (!is_proc_dir(entry->d_name))
            continue;

        pid_t pid = atoi(entry->d_name);
        if (pid == self_pid)
            continue;

        process_info_t p = {0};
        p.pid = pid;

        char path[64];
        int base_len = snprintf(path, sizeof(path), "/proc/%d/", pid);

        memcpy(path + base_len, "stat", 5);
        int statfd = open(path, O_RDONLY);
        if (statfd < 0) continue;
        char stat_line[1024];
        ssize_t n = read(statfd, stat_line, sizeof(stat_line) - 1);
        close(statfd);
        if (n <= 0) continue;
        stat_line[n] = '\0';

        char *lparen = strchr(stat_line, '(');
        char *rparen = strrchr(stat_line, ')');
        if (!lparen || !rparen || lparen > rparen)
            continue;
        size_t comm_len = rparen - lparen - 1;
        if (comm_len >= sizeof(p.name))
            comm_len = sizeof(p.name) - 1;

        safe_strncpy(p.name, lparen + 1, comm_len + 1);
        p.name[comm_len] = '\0';

        char *after_rparen = rparen + 2;
        p.status.state = *after_rparen;

        /* Avoid strtok which is not re-entrant and
           would corrupt state if anything else in the call stack uses it */
        char *cursor = after_rparen + 2;
        int field_index = 3;

        long num_threads = 0;
        unsigned long long start_time = 0;
        long rss = 0;
        pid_t ppid = 0;
        int session = 0;
        int found_threads = 0, found_start = 0, found_rss = 0;

        while (*cursor) {
            while (*cursor == ' ') cursor++;
            if (!*cursor) break;

            char *end = cursor;
            while (*end && *end != ' ') end++;

            switch (field_index) {
            case 3: ppid = (pid_t)atoi(cursor); break;
            case 5: session = atoi(cursor); break;
            case 19: num_threads = atol(cursor); found_threads = 1; break;
            case 21: start_time = strtoull(cursor, NULL, 10); found_start = 1; break;
            case 23: rss = atol(cursor); found_rss = 1; break;
            }

            if (found_rss)
                break;

            cursor = end;
            field_index++;
        }

        if (!(found_threads && found_start && found_rss))
            continue;

        snprintf(p.status.threads, sizeof(p.status.threads), "%ld", num_threads);

        struct stat st;
        path[base_len - 1] = '\0';
        if (stat(path, &st) == 0)
            safe_strncpy(p.owner, get_proc_user(st.st_uid), sizeof(p.owner));
        path[base_len - 1] = '/';

        if (args->hide_root && strcasecmp(p.owner, "root") == 0)
            continue;

        if (args->user && strcasecmp(p.owner, args->user) != 0)
            continue;

        if (args->parent_pid && ppid != args->parent_pid)
            continue;

        if (args->session_id && session != args->session_id)
            continue;

        p.cmdline[0] = '\0';
        // always read cmdline for -F so fuzzy search has data
        if (args->operation == SWOP_FUZZY|| !entry_matches(&p, plist, args, compiled)) {
            memcpy(path + base_len, "cmdline", 8);
            FILE *fcmd = fopen(path, "r");
            if (fcmd) {
                size_t n = fread(p.cmdline, 1, sizeof(p.cmdline) - 1, fcmd);
                fclose(fcmd);
                for (size_t i = 0; i < n; ++i)
                    if (p.cmdline[i] == '\0')
                        p.cmdline[i] = ' ';
                p.cmdline[n] = '\0';
            }
            if (args->operation != SWOP_FUZZY && !entry_matches(&p, plist, args, compiled))
                continue;
        }

        if (args->operation == SWOP_FUZZY || args->sort_mode == SWSORT_RAM)
            p.ram = (rss * page_size) >> 20; // MiB via bitshift <3

        if (args->operation == SWOP_FUZZY || args->sort_mode == SWSORT_AGE)
            p.start_time = start_time;

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
static int confirm_and_act(const swordfish_args_t *args, int count, int *selected,
                            process_info_t *matches) {
    if (count == 0)
        return EXIT_NO_MATCH;

    int sig = args->sig;

    if (args->operation == SWOP_STATIC) {
        for (int i = 0; i < count; ++i)
            print_proc_info(&matches[selected[i]], sig, args, "", false);
        return EXIT_FOUND;
    }

    if (args->operation != SWOP_STATIC && !args->auto_confirm && is_interactive()) {
        for (int i = 0; i < count; ++i)
            print_proc_info(&matches[selected[i]], sig, args, "  PID", false);

        if (has_root_process(count, selected, matches, 0))
            WARN("At least one selected process is owned by root!");

        if (count == 1)
            printf("The process above will be affected (signal %d - %s)\n", sig, strsignal(sig));
        else
            printf("The processes above will be affected (signal %d - %s)\n", sig, strsignal(sig));
        printf("Proceed? [y/N]: ");

        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (tolower(confirm[0]) != 'y')
            return EXIT_NO_MATCH;
    }

    run_hook(args->pre_hook, matches[selected[0]].pid, matches[selected[0]].name);

    // dry run
    if (args->dry_run) {
        for (int i = 0; i < count; ++i) {
            int idx = selected[i];
            INFO("Would send signal %d (%s) to PID %d (%s)", sig, strsignal(sig),
                 matches[idx].pid, matches[idx].name);
        }
        return EXIT_FOUND;
    }

    int killed = 0;
    int failed = 0;
    bool all_eperm = true;

    for (int i = 0; i < count; ++i) {
        int idx = selected[i];
        if (is_zombie_process(matches[idx].pid)) {
            printf("PID %d (%s) is a zombie process and may not be killed\n",
                   matches[idx].pid, matches[idx].name);
            failed++;
            continue;
        }

        if (kill(matches[idx].pid, sig) == 0) {
            print_proc_info(&matches[idx], sig, args, strsignal(sig), true);
            killed++;
            all_eperm = false;
        } else {
            if (errno != EPERM)
                all_eperm = false;
            ERROR("Failed to send signal %d to PID %d (%s): %s", sig, matches[idx].pid,
                  matches[idx].name, strerror(errno));
            failed++;
        }
    }

    if (args->timeout > 0 || args->wait_for_death) {
        int timeout_ms = args->timeout * 1000;
        int elapsed_ms = 0;

        bool any_alive = true;
        while (any_alive) {
            any_alive = false;
            for (int i = 0; i < count; ++i) {
                int idx = selected[i];
                char proc_path[32];
                snprintf(proc_path, sizeof(proc_path), "/proc/%d", matches[idx].pid);
                if (access(proc_path, F_OK) == 0)
                    any_alive = true;
            }
            if (!any_alive)
                break;

            if (args->timeout > 0 && elapsed_ms >= timeout_ms) {
                // escalate all still-alive processes to SIGKILL
                for (int i = 0; i < count; ++i) {
                    int idx = selected[i];
                    char proc_path[32];
                    snprintf(proc_path, sizeof(proc_path), "/proc/%d", matches[idx].pid);
                    if (access(proc_path, F_OK) == 0) {
                        kill(matches[idx].pid, SIGKILL);
                        INFO("PID %d did not die after %ds, escalated to SIGKILL",
                             matches[idx].pid, args->timeout);
                    }
                }
                break;
            }

            usleep(10000);
            elapsed_ms += 10;
        }
    }

    run_hook(args->post_hook, matches[selected[0]].pid, matches[selected[0]].name);

    if (failed == 0) return EXIT_FOUND;
    if (killed == 0) return all_eperm ? EXIT_PERMISSION : EXIT_NO_MATCH;
    return EXIT_PARTIAL;
}

/* Scans the /proc directory for processes */
int scan_processes(const swordfish_args_t *args, pattern_list_t *plist) {
    compiled_pattern_t compiled[MAX_PATTERNS];
    compile_patterns(args, plist, compiled);

    int tries = 0;
    process_info_t *matches = malloc(MAX_MATCHES * sizeof(process_info_t));
    if (!matches) {
        ERROR("Out of memory");
        return EXIT_NO_MATCH;
    }
    while (1) {
        int matched = find_matching_processes(args, plist, matches, compiled);

        // Only sort if there are multiple matches
        if (matched > 1) {
            switch (args->sort_mode) {
            case SWSORT_RAM:
                qsort(matches, matched, sizeof(process_info_t), cmp_ram);
                break;
            case SWSORT_AGE:
                qsort(matches, matched, sizeof(process_info_t), cmp_age);
                break;
            default:
                break;
            }
        }

        if (args->print_pids_only) {
            for (int i = 0; i < matched; ++i)
                printf("%d\n", matches[i].pid);
            break;
        }

        int selected[MAX_MATCHES], count = 0;
        int result = EXIT_NO_MATCH;
        if (matched > 0) {
            if (args->top_only) {
                selected[count++] = 0;
            } else if (args->operation == SWOP_FUZZY) {
                tui_result_t tui = tui_run(args, matches, matched);
                result = tui.count > 0 ? EXIT_FOUND : EXIT_NO_MATCH;
            } else if (args->operation == SWOP_SELECT && !args->auto_confirm) {
                select_processes(matched, matches, selected, &count, args, args->sig);
            } else {
                for (int i = 0; i < matched; ++i)
                    selected[count++] = i;
            }
            result = confirm_and_act(args, count, selected, matches);
        } else if (tries == 0) {
            printf("No processes matched\n");
        }

        if (args->retry_time <= 0 || matched > 0) {
            free_compiled_patterns(compiled, plist->pattern_count);
            free(matches);
            return matched > 0 ? result : EXIT_NO_MATCH;
        }

        sleep(args->retry_time);
        tries++;
    }

    free_compiled_patterns(compiled, plist->pattern_count);
    free(matches);
    return EXIT_FOUND;
}
