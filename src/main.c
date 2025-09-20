#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
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

static int maybe_reexec_with_sudo(int argc, char **argv, const swordfish_args_t *args) {
    if (!args->do_kill)
        return 0;
    if (!process_requires_sudo(argv[args->pattern_start_idx]))
        return 0;
    if (args->auto_confirm) {
        char **new_argv = malloc((argc + 2) * sizeof(char *));
        if (!new_argv) {
            perror("malloc");
            return 2;
        }
        new_argv[0] = "sudo";
        for (int i = 0; i < argc; ++i)
            new_argv[i + 1] = argv[i];
        new_argv[argc + 1] = NULL;
        execvp("sudo", new_argv);
        perror("execvp failed");
        free(new_argv);
        return 2;
    } else {
        fprintf(stderr, "Warning: Some matching processes are not owned by you.\n");
        printf("Rerun with sudo? [y/N]: ");
        char response[8] = {0};
        fgets(response, sizeof(response), stdin);
        if (response[0] == 'y' || response[0] == 'Y') {
            char **new_argv = malloc((argc + 2) * sizeof(char *));
            if (!new_argv) {
                perror("malloc");
                return 2;
            }
            new_argv[0] = "sudo";
            for (int i = 0; i < argc; ++i)
                new_argv[i + 1] = argv[i];
            new_argv[argc + 1] = NULL;
            execvp("sudo", new_argv);
            perror("execvp failed");
            free(new_argv);
            return 2;
        }
    }
    return 0;
}

static void maybe_drop_privileges(const swordfish_args_t *args) {
    if (!args->do_kill && geteuid() == 0) {
        drop_privileges();
    }
}

int main(int argc, char **argv) {
    swordfish_args_t args;

    // Call parse_args to check what arguments were passed
    // If no arguments, print usage and exit[2]
    if (parse_args(argc, argv, &args) != 0)
        return 2;

    int sudo_result = maybe_reexec_with_sudo(argc, argv, &args);
    if (sudo_result != 0)
        return sudo_result;

    maybe_drop_privileges(&args);

    return scan_processes(&args, &argv[args.pattern_start_idx], argc - args.pattern_start_idx);
}
