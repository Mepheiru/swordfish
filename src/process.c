#define _GNU_SOURCE
#include "process.h"
#include "args.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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

void drop_privileges(void) {
    if (geteuid() != 0)
        return;

    uid_t uid = getuid();
    gid_t gid = getgid();
    if (setgid(gid) != 0 || setuid(uid) != 0) {
        fprintf(stderr, "Failed to drop privileges: %s\n", strerror(errno));
        exit(2);
    }
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

    if (fgets(cmdline, sizeof(cmdline), f))
        cmdline[strcspn(cmdline, "\n")] = 0;
    else
        strcpy(cmdline, "unknown");
    fclose(f);
    return cmdline;
}

static void strtolower(char *s) {
    for (; *s; ++s)
        *s = tolower((unsigned char)*s);
}

static bool pattern_matches(const swordfish_args_t *args, const char *name, const char *cmdline,
                            char **lower_patterns, int pattern_count) {
    char name_lc[256], cmdline_lc[256];

    strncpy(name_lc, name, sizeof(name_lc));
    name_lc[sizeof(name_lc) - 1] = '\0';
    strncpy(cmdline_lc, cmdline, sizeof(cmdline_lc));
    cmdline_lc[sizeof(cmdline_lc) - 1] = '\0';

    strtolower(name_lc);
    strtolower(cmdline_lc);

    for (int i = 0; i < pattern_count; ++i) {
        if ((args->exact_match && (strcmp(name_lc, lower_patterns[i]) == 0 ||
                                   strcmp(cmdline_lc, lower_patterns[i]) == 0)) ||
            (!args->exact_match && (strstr(name_lc, lower_patterns[i]) != NULL ||
                                    strstr(cmdline_lc, lower_patterns[i]) != NULL))) {
            return true;
        }
    }
    return false;
}

static bool entry_matches(const process_info_t *p, pattern_list_t *plist,
                          const swordfish_args_t *args) {
    for (int i = 0; i < plist->pattern_count; ++i) {
        if (plist->pattern_is_pid[i] && is_all_digits(plist->patterns[i]) &&
            atoi(plist->patterns[i]) == p->pid)
            return true;
    }
    return pattern_matches(args, p->name, p->cmdline, plist->patterns, plist->pattern_count);
}

static void print_proc_info(const process_info_t *p, int sig, const swordfish_args_t *args,
                            const char *prefix, bool include_signal) {
    const char *owner_fmt = (strcmp(p->owner, "root") == 0) ? "\033[33m%s\033[0m" : "%s";
    if (args->do_verbose) {
        printf("[VERBOSE] %s%d (%s) cmdl (%s) threads (%s) owned by ", prefix, p->pid, p->name,
               p->cmdline, p->status.threads);
        printf(owner_fmt, p->owner);
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
        printf("\n");
    } else {
        printf("%s%d (%s) owned by ", prefix, p->pid, p->name);
        printf(owner_fmt, p->owner);
        if (include_signal)
            printf(" [signal %d (%s)]", sig, strsignal(sig));
        printf("\n");
    }
}

static int find_matching_processes(const swordfish_args_t *args, pattern_list_t *plist,
                                   process_info_t *matches) {
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

        strncpy(p.cmdline, get_proc_cmdline(p.pid), sizeof(p.cmdline));
        read_status_field(p.pid, &p.status);
        strncpy(p.owner, get_proc_user(p.status.uid), sizeof(p.owner));

        if (args->user && strcasecmp(p.owner, args->user) != 0)
            continue;
        if (!entry_matches(&p, plist, args))
            continue;

        if (matched < MAX_MATCHES)
            matches[matched++] = p;
    }

    closedir(proc);
    return matched;
}

static void select_processes(int matched, process_info_t *matches, int *selected, int *count) {
    printf("Select which processes to act on:\n");
    for (int i = 0; i < matched; ++i)
        printf("[%d] PID %d (%s)\n", i + 1, matches[i].pid, matches[i].name);

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
    if (args->do_kill && !args->auto_confirm && count > 0) {
        printf("The following processes will be killed (signal %d - %s):\n", args->sig,
               strsignal(args->sig));
        for (int i = 0; i < count; ++i)
            print_proc_info(&matches[selected[i]], args->sig, args, "  PID ", false);

        printf("Proceed? [y/N]: ");
        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (tolower(confirm[0]) != 'y') {
            printf("Aborted.\n");
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        int idx = selected[i];
        if (is_zombie_process(matches[idx].pid)) {
            printf("PID %d (%s) is a zombie process and may not be killed.\n", matches[idx].pid,
                   matches[idx].name);
            continue;
        }

        if (!args->do_kill) {
            print_proc_info(&matches[idx], args->sig, args, "", false);
            continue;
        }

        if (kill(matches[idx].pid, args->sig) == 0)
            print_proc_info(&matches[idx], args->sig, args,
                            args->sig == SIGTERM ? "Killed " : "Sent signal to ", true);
        else
            fprintf(stderr, "Failed to kill PID %d (%s): %s\n", matches[idx].pid, matches[idx].name,
                    strerror(errno));
    }
}

int scan_processes(const swordfish_args_t *args, pattern_list_t *plist) {
    process_info_t matches[MAX_MATCHES];
    int matched = find_matching_processes(args, plist, matches);
    if (matched < 0)
        return 2;

    if (args->print_pids_only) {
        for (int i = 0; i < matched; ++i)
            printf("%d\n", matches[i].pid);
        return matched > 0 ? 0 : 1;
    }

    if (matched == 0) {
        fprintf(stderr, "No processes matched.\n");
        return 1;
    }

    int selected[MAX_MATCHES], count = 0;
    if (args->select_mode && !args->auto_confirm)
        select_processes(matched, matches, selected, &count);
    else
        for (int i = 0; i < matched; ++i)
            selected[count++] = i;

    confirm_and_act(args, count, selected, matches);
    return 0;
}
