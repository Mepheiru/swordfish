#include "process.h"
#include "args.h"
#include "hooks.h"
#include "main.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
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

static bool read_status_field(pid_t pid, proc_status_t *status) {
    char path[PATH_MAX], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Uid:", 4) == 0)
            sscanf(line, "Uid:\t%u", &status->uid);
        else if (strncmp(line, "State:", 6) == 0)
            sscanf(line, "State:\t%c", &status->state);
        else if (strncmp(line, "Threads:", 8) == 0)
            sscanf(line, "Threads:\t%s", status->threads);
    }
    fclose(f);
    return true;
}

static const char *get_proc_cmdline(pid_t pid) {
    static char cmdline[256];
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return "unknown";

    size_t len = fread(cmdline, 1, sizeof(cmdline) - 1, f);
    fclose(f);

    if (len == 0) {
        strcpy(cmdline, "unknown");
    } else {
        // Replace null bytes with spaces
        for (size_t i = 0; i < len; ++i)
            if (cmdline[i] == '\0')
                cmdline[i] = ' ';
        cmdline[len] = '\0';
    }

    return cmdline;
}

static void strtolower(char *s) {
    for (; *s; ++s)
        *s = tolower((unsigned char)*s);
}

static bool is_output_terminal(void) {
    return isatty(STDOUT_FILENO);
}

bool is_interactive(void) {
    return isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
}

static void compile_patterns(const swordfish_args_t *args, pattern_list_t *plist,
                             compiled_pattern_t *compiled) {
    for (int i = 0; i < plist->pattern_count; ++i) {
        const char *pat_orig = plist->patterns[i];
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
                c->type = PAT_SUBSTR; // optional fallback, or exit if you prefer
            } else {
                c->type = PAT_REGEX;
            }
        }
    }
}

static bool match_process(const char *name, const char *cmdline, compiled_pattern_t *compiled,
                          int pattern_count) {
    char name_lc[256], cmdline_lc[256];
    safe_strncpy(name_lc, name, sizeof(name_lc) - 1);
    name_lc[sizeof(name_lc) - 1] = '\0';
    safe_strncpy(cmdline_lc, cmdline, sizeof(cmdline_lc) - 1);
    cmdline_lc[sizeof(cmdline_lc) - 1] = '\0';
    strtolower(name_lc);
    strtolower(cmdline_lc);

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

static void free_compiled_patterns(compiled_pattern_t *compiled, int count) {
    for (int i = 0; i < count; ++i)
        if (compiled[i].type == PAT_REGEX)
            regfree(&compiled[i].regex);
}

static bool entry_matches(const process_info_t *p, pattern_list_t *plist,
                          const swordfish_args_t *args, compiled_pattern_t *compiled) {
    // Exclude patterns (same as before)
    if (args->exclude_patterns && args->exclude_count > 0) {
        char name_lc[256], cmdline_lc[256];
        safe_strncpy(name_lc, p->name, sizeof(name_lc));
        name_lc[sizeof(name_lc) - 1] = '\0';
        safe_strncpy(cmdline_lc, p->cmdline, sizeof(cmdline_lc));
        cmdline_lc[sizeof(cmdline_lc) - 1] = '\0';
        strtolower(name_lc);
        strtolower(cmdline_lc);
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

static void print_proc_info(const process_info_t *p, int sig, const swordfish_args_t *args,
                            const char *prefix, bool include_signal) {
    bool tty = is_output_terminal();

    const char *owner_fmt = "%s";
    if (tty && strcmp(p->owner, "root") == 0)
        owner_fmt = "\033[33m%s\033[0m"; // only color if terminal

    long uptime = 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up = 0;
        if (fscanf(f, "%lf", &up) == 1)
            uptime = (long)up;
        fclose(f);
    }
    long age_min = (uptime - (p->start_time / sysconf(_SC_CLK_TCK))) / 60;

    if (args->do_verbose || !tty) {
        printf("%s%d (%s) cmdl (%s) threads (%s) owned by ", prefix, p->pid, p->name, p->cmdline,
               p->status.threads);
        printf(owner_fmt, p->owner);
        if (tty) {
            printf(" | CPU: %.1f%% | RAM: %.1f MB | Age: %ld min", p->cpu, p->ram / 1024.0,
                   age_min > 0 ? age_min : 0);
        }
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
    } else {
        printf("%s%d (%s) owned by ", prefix, p->pid, p->name);
        printf(owner_fmt, p->owner);
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
        if (tty) {
            if (args->sort_mode == SWSORT_CPU)
                printf(" [%.1f%%]", p->cpu);
            else if (args->sort_mode == SWSORT_RAM)
                printf(" [%.1f MB]", p->ram / 1024.0);
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

static long get_proc_ram_kb(pid_t pid) {
    char status_path[PATH_MAX], line[256];
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
    FILE *f = fopen(status_path, "r");
    if (!f)
        return 0;
    long ram = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %ld", &ram);
            break;
        }
    }
    fclose(f);
    return ram;
}

static double get_proc_cpu(pid_t pid) {
    char stat_path[PATH_MAX], buf[1024];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    FILE *f = fopen(stat_path, "r");
    if (!f)
        return 0.0;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0.0;
    }
    fclose(f);
    unsigned long utime = 0, stime = 0, cutime = 0, cstime = 0, starttime = 0;
    int field = 1;
    char *p = buf;
    while (field <= 22 && *p) {
        if (field == 14)
            utime = strtoul(p, NULL, 10);
        else if (field == 15)
            stime = strtoul(p, NULL, 10);
        else if (field == 16)
            cutime = strtoul(p, NULL, 10);
        else if (field == 17)
            cstime = strtoul(p, NULL, 10);
        else if (field == 22)
            starttime = strtoul(p, NULL, 10);
        if (*p == ' ')
            field++;
        p++;
    }
    double total_time = utime + stime + cutime + cstime;
    double uptime_seconds = 0;
    FILE *uf = fopen("/proc/uptime", "r");
    if (uf) {
        double up = 0;
        if (fscanf(uf, "%lf", &up) == 1)
            uptime_seconds = up;
        fclose(uf);
    }
    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    double seconds = uptime_seconds - ((double)starttime / ticks_per_sec);
    if (seconds <= 0)
        return 0.0;
    return 100.0 * (total_time / ticks_per_sec) / seconds;
}

static int cmp_cpu(const void *a, const void *b) {
    const process_info_t *pa = a, *pb = b;
    return (pb->cpu > pa->cpu) - (pb->cpu < pa->cpu);
}

static int cmp_ram(const void *a, const void *b) {
    const process_info_t *pa = a, *pb = b;
    return (pb->ram > pa->ram) - (pb->ram < pa->ram);
}

static int cmp_age(const void *a, const void *b) {
    const process_info_t *pa = a, *pb = b;
    return (pa->start_time > pb->start_time) - (pa->start_time < pb->start_time);
}

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

static int find_matching_processes(const swordfish_args_t *args, pattern_list_t *plist,
                                   process_info_t *matches, compiled_pattern_t *compiled) {
    DIR *proc = opendir("/proc");
    if (!proc) {
        perror("opendir /proc");
        return -1;
    }

    int matched = 0;
    struct dirent *entry;
    while ((entry = readdir(proc))) {
        if (!is_proc_dir(entry->d_name))
            continue;

        process_info_t p = {0};
        p.pid = atoi(entry->d_name);

        // Skip the scanner process itself
        if (p.pid == getpid())
            continue;

        char comm_path[PATH_MAX];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
        FILE *f = fopen(comm_path, "r");
        if (!f)
            continue;
        if (!fgets(p.name, sizeof(p.name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);
        p.name[strcspn(p.name, "\n")] = 0;

        safe_strncpy(p.cmdline, get_proc_cmdline(p.pid), sizeof(p.cmdline));
        read_status_field(p.pid, &p.status);
        safe_strncpy(p.owner, get_proc_user(p.status.uid), sizeof(p.owner));
        p.ram = get_proc_ram_kb(p.pid);
        p.start_time = get_proc_start_time(p.pid);
        p.cpu = get_proc_cpu(p.pid);

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

static void confirm_and_act(const swordfish_args_t *args, int count, int *selected,
                            process_info_t *matches) {
    // Show warning if any selected process is root
    if (!args->run_static && has_root_process(count, selected, matches, 0) && is_interactive()) {
        WARN("At least one selected process is owned by root!");
    }

    if (count == 0)
        return;

    int sig = args->do_kill ? SIGKILL : args->sig;

    // Pre-kill hook
    run_hook(args->pre_hook, matches[selected[0]].pid, matches[selected[0]].name);

    if ((args->do_term || (args->do_kill && !args->auto_confirm)) && is_interactive()) {
        for (int i = 0; i < count; ++i)
            print_proc_info(&matches[selected[i]], sig, args, "  PID ", false);

        if (count == 1)
            printf("The process above will be affected (signal %d - %s):\n", sig, strsignal(sig));
        else
            printf("The processes above will be affected (signal %d - %s):\n", sig, strsignal(sig));
        printf("Proceed? [y/N]: ");
        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (tolower(confirm[0]) != 'y') {
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        int idx = selected[i];
        if (is_zombie_process(matches[idx].pid)) {
            printf("PID %d (%s) is a zombie process and may not be killed\n", matches[idx].pid,
                   matches[idx].name);
            continue;
        }

        if (!args->do_term && !args->do_kill) {
            print_proc_info(&matches[idx], sig, args, "", false);
            continue;
        }

        if (kill(matches[idx].pid, sig) == 0)
            print_proc_info(&matches[idx], sig, args, sig == SIGTERM ? "Killed " : "Terminated ", true);
        else
            ERROR("Failed to send signal %d to PID %d (%s): %s", sig, matches[idx].pid,
                  matches[idx].name, strerror(errno));
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
        // Sort if requested
        if (args->sort_mode == SWSORT_CPU)
            qsort(matches, matched, sizeof(process_info_t), cmp_cpu);
        else if (args->sort_mode == SWSORT_RAM)
            qsort(matches, matched, sizeof(process_info_t), cmp_ram);
        else if (args->sort_mode == SWSORT_AGE)
            qsort(matches, matched, sizeof(process_info_t), cmp_age);

        if (args->print_pids_only) {
            for (int i = 0; i < matched; ++i)
                printf("%d\n", matches[i].pid);
            return matched > 0 ? 0 : 1;
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
        } else {
            printf("No processes matched\n");
        }

        if (args->retry_time <= 0)
            break;
        sleep(args->retry_time);
        tries++;
    }

    free_compiled_patterns(compiled, plist->pattern_count);
    return 0;
}
