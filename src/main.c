#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "process.h"

int process_requires_sudo(const char *pattern) {
    DIR *proc = opendir("/proc");
    if (!proc)
        return 0; // Fail safe: don’t escalate if can’t open /proc

    uid_t my_uid = geteuid();
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!isdigit(entry->d_name[0]))
            continue;

        char comm_path[512];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);

        FILE *comm_file = fopen(comm_path, "r");
        if (!comm_file)
            continue;

        char proc_name[256];
        if (!fgets(proc_name, sizeof(proc_name), comm_file)) {
            fclose(comm_file);
            continue;
        }

        // Remove trailing newline
        proc_name[strcspn(proc_name, "\n")] = 0;
        fclose(comm_file);

        if (strcmp(proc_name, pattern) != 0)
            continue;

        // Check UID
        char status_path[512];
        snprintf(status_path, sizeof(status_path), "/proc/%s/status", entry->d_name);

        FILE *status_file = fopen(status_path, "r");
        if (!status_file)
            continue;

        uid_t proc_uid = -1;
        char line[256];
        while (fgets(line, sizeof(line), status_file)) {
            if (strncmp(line, "Uid:", 4) == 0) {
                sscanf(line, "Uid:\t%u", &proc_uid);
                break;
            }
        }

        fclose(status_file);

        if (proc_uid != my_uid) {
            closedir(proc);
            return 1; // Needs sudo
        }
    }

    closedir(proc);
    return 0; // All matched processes are owned
}

int main(int argc, char **argv) {
    swordfish_args_t args;
    int ret = parse_args(argc, argv, &args);
    if (ret)
        return ret;
    drop_privileges();
    int pattern_count = argc - args.pattern_start_idx;
    char **patterns = &argv[args.pattern_start_idx];
    bool pattern_is_pid[pattern_count];
    for (int i = 0; i < pattern_count; ++i)
        pattern_is_pid[i] = is_all_digits(patterns[i]);
    pattern_list_t plist = {
        .patterns = patterns, .pattern_is_pid = pattern_is_pid, .pattern_count = pattern_count};
    return scan_processes(&args, &plist);
}
