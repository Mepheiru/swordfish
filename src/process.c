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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static bool substring_match(const char *haystack, const char *needle) {
    return strcasestr(haystack, needle) != NULL;
}

static bool is_proc_dir(const char *name) {
    for (const char *p = name; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

bool is_zombie_process(pid_t pid) {
    char status_path[256];
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);

    FILE *file = fopen(status_path, "r");
    if (!file)
        return false; // Assume false if we can't read it

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "State:", 6) == 0) {
            // Format is "State:\tZ (zombie)"
            char state = 0;
            sscanf(line, "State:\t%c", &state);
            fclose(file);
            return state == 'Z';
        }
    }

    fclose(file);
    return false;
}

void drop_privileges(void) {
    if (geteuid() == 0) {
        uid_t uid = getuid();
        gid_t gid = getgid();
        if (setgid(gid) != 0 || setuid(uid) != 0) {
            fprintf(stderr, "Failed to drop privileges: %s\n", strerror(errno));
            exit(2);
        }
    }
}

static const char *get_proc_user(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "unknown";
}

static const char *get_proc_cmdl(pid_t pid) {
    static char cmdline[256];
    char cmdline_path[256];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(cmdline_path, "r");
    if (f) {
        if (fgets(cmdline, sizeof(cmdline), f)) {
            cmdline[strcspn(cmdline, "\n")] = 0; // Remove newline
            fclose(f);
            return cmdline;
        }
        fclose(f);
    }
    return "unknown";
}

static const char *get_proc_threads(pid_t pid) {
    static char threads[32];
    char status_path[256];
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
    FILE *f = fopen(status_path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Threads:", 8) == 0) {
                sscanf(line, "Threads:\t%s", threads);
                fclose(f);
                return threads;
            }
        }
        fclose(f);
    }
    return "unknown";
}

static bool pattern_matches(const swordfish_args_t *args, const char *name, const char *cmdline,
                            char **patterns, int pattern_count) {
    for (int i = 0; i < pattern_count; ++i) {
        if ((args->exact_match &&
             (strcasecmp(name, patterns[i]) == 0 || strcasecmp(cmdline, patterns[i]) == 0)) ||
            (!args->exact_match &&
             (substring_match(name, patterns[i]) || substring_match(cmdline, patterns[i])))) {
            return true;
        }
    }
    return false;
}

// Helper: check if string is all digits
bool is_all_digits(const char *s) {
    if (!s || *s == '\0')
        return false;
    for (; *s; ++s)
        if (!isdigit(*s))
            return false;
    return true;
}

// Helper: check if entry matches any pattern (PID or name)
static bool entry_matches(const struct dirent *entry, const char *name, const char *cmdline,
                          char **patterns, bool *pattern_is_pid, int pattern_count,
                          const swordfish_args_t *args) {
    for (int i = 0; i < pattern_count; ++i) {
        if (pattern_is_pid[i]) {
            if (strcmp(entry->d_name, patterns[i]) == 0)
                return true;
        }
    }
    return pattern_matches(args, name, cmdline, patterns, pattern_count);
}

// Helper: fill matches array, return number matched
static int find_matching_processes(const swordfish_args_t *args, char **patterns, int pattern_count,
                                   proc_entry_t *matches, bool *pattern_is_pid) {
    DIR *proc = opendir("/proc");
    if (!proc) {
        perror("opendir /proc");
        return -1;
    }
    int matched = 0;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!is_proc_dir(entry->d_name))
            continue;
        char comm_path[PATH_MAX];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
        FILE *f = fopen(comm_path, "r");
        if (!f)
            continue;
        char name[256];
        if (!fgets(name, sizeof(name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);
        name[strcspn(name, "\n")] = 0;
        // Read argv[0] from /proc/[pid]/cmdline
        char cmdline[256] = {0};
        char cmdline_path[PATH_MAX];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
        FILE *cf = fopen(cmdline_path, "r");
        if (cf) {
            if (fgets(cmdline, sizeof(cmdline), cf)) {
                // cmdline is null-separated, just use the first string
            }
            fclose(cf);
        }
        char status_path[PATH_MAX];
        snprintf(status_path, sizeof(status_path), "/proc/%s/status", entry->d_name);
        uid_t uid = -1;
        FILE *status = fopen(status_path, "r");
        if (status) {
            char line[256];
            while (fgets(line, sizeof(line), status)) {
                if (strncmp(line, "Uid:", 4) == 0) {
                    sscanf(line, "Uid:\t%u", &uid);
                    break;
                }
            }
            fclose(status);
        }
        if (args->user && strcasecmp(get_proc_user(uid), args->user) != 0)
            continue;
        if (entry_matches(entry, name, cmdline, patterns, pattern_is_pid, pattern_count, args)) {
            if (matched < MAX_MATCHES) {
                matches[matched].pid = atoi(entry->d_name);
                snprintf(matches[matched].name, sizeof(matches[matched].name), "%s", name);
                snprintf(matches[matched].owner, sizeof(matches[matched].owner), "%s",
                         get_proc_user(uid));
                matched++;
            }
        }
    }
    closedir(proc);
    return matched;
}

static void select_processes(int matched, proc_entry_t *matches, int *selected, int *count) {
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
                int start = atoi(token);
                int end = atoi(dash + 1);
                if (start > 0 && end >= start) {
                    for (int j = start; j <= end && *count < matched; ++j) {
                        int idx = j - 1;
                        if (idx >= 0 && idx < matched)
                            selected[(*count)++] = idx;
                    }
                }
            } else {
                int idx = atoi(token) - 1;
                if (idx >= 0 && idx < matched)
                    selected[(*count)++] = idx;
            }
            token = strtok(NULL, ",");
        }
    }
}

static void print_proc_info(const proc_entry_t *proc, int sig, const swordfish_args_t *args,
                            const char *prefix, bool include_signal, bool force_non_verbose) {
    if (args->do_verbose && !force_non_verbose) {
        if (include_signal)
            printf("[VERBOSE] %s%d (%s) cmdl (%s) threads (%s) owned by %s [signal %d (%s)]\n",
                   prefix, proc->pid, proc->name, get_proc_cmdl(proc->pid),
                   get_proc_threads(proc->pid), proc->owner, sig, strsignal(sig));
        else
            printf("[VERBOSE] %s%d (%s) cmdl (%s) threads (%s) owned by %s\n", prefix, proc->pid,
                   proc->name, get_proc_cmdl(proc->pid), get_proc_threads(proc->pid), proc->owner);
    } else {
        if (include_signal)
            printf("%s%d (%s) owned by %s [signal %d (%s)]\n", prefix, proc->pid, proc->name,
                   proc->owner, sig, strsignal(sig));
        else
            printf("%s%d (%s) owned by %s\n", prefix, proc->pid, proc->name, proc->owner);
    }
}

static void confirm_and_act(const swordfish_args_t *args, int count, int *selected,
                            proc_entry_t *matches) {
    // Confirmation prompt for killing processes (unless auto_confirm)
    if (args->do_kill && !args->auto_confirm && count > 0) {
        printf("The following processes will be killed (signal %d - %s):\n", args->sig,
               strsignal(args->sig));
        for (int i = 0; i < count; ++i) {
            int idx = selected[i];
            print_proc_info(&matches[idx], args->sig, args, "  PID ", false, false);
        }
        printf("Proceed? [y/N]: ");
        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (confirm[0] != 'y' && confirm[0] != 'Y') {
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
        if (args->do_kill) {
            if (kill(matches[idx].pid, args->sig) == 0)
                print_proc_info(&matches[idx], args->sig, args, "Sent signal to ", true,
                                true); // force non-verbose
            else
                fprintf(stderr, "Failed to kill PID %d (%s): %s\n", matches[idx].pid,
                        matches[idx].name, strerror(errno));
        } else {
            print_proc_info(&matches[idx], args->sig, args, "", false, false);
        }
    }
}

int scan_processes(const swordfish_args_t *args, char **patterns, int pattern_count) {
    bool pattern_is_pid[pattern_count];
    for (int i = 0; i < pattern_count; ++i)
        pattern_is_pid[i] = is_all_digits(patterns[i]);

    proc_entry_t matches[MAX_MATCHES];
    int matched = find_matching_processes(args, patterns, pattern_count, matches, pattern_is_pid);
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
    if (args->select_mode && !args->auto_confirm) {
        select_processes(matched, matches, selected, &count);
    } else {
        for (int i = 0; i < matched; ++i)
            selected[count++] = i;
    }
    confirm_and_act(args, count, selected, matches);
    return 0;
}
