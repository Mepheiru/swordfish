#include "hooks.h"
#include "main.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Copy up to size-1 chars, always null-terminate. Does not pad */
void safe_strncpy(char *dst, const char *src, size_t size) {
    if (!dst || !src || size == 0)
        return;

    size_t src_len = strlen(src);
    size_t copy_len = (src_len >= size) ? (size - 1) : src_len;

    if (copy_len > 0)
        memcpy(dst, src, copy_len);

    dst[copy_len] = '\0';
}

/* Run a hook command (post/pre) with PID and process name as arguments */
void run_hook(const char *hook, pid_t pid, const char *name) {
    if (!hook || hook[0] == '\0')
        return;

    char pidbuf[32];
    int n = snprintf(pidbuf, sizeof(pidbuf), "%d", (int)pid);
    if (n < 0 || (size_t)n >= sizeof(pidbuf)) {
        ERROR("Hook: PID formatting failed for %d", (int)pid);
        return;
    }

    pid_t child = fork();
    if (child < 0) {
        ERROR("Hook '%s' failed to fork: %s", hook, strerror(errno));
        return;
    }

    if (child == 0) {
        /* Child: prepare argv and a minimal, safer environment */
        char *const argv[] = {(char *)hook, pidbuf, (char *)(name ? name : ""), NULL};

        /* Build minimal env, but include $DISPLAY and DBUS_SESSION_BUS_ADDRESS if set */
        const char *display = getenv("DISPLAY");
        const char *dbus_addr = getenv("DBUS_SESSION_BUS_ADDRESS");
        char display_env[64], dbus_env[256];
        const char *envp[5];
        int envc = 0;
        envp[envc++] = "PATH=/usr/bin:/bin";
        if (display && *display) {
            snprintf(display_env, sizeof(display_env), "DISPLAY=%s", display);
            envp[envc++] = display_env;
        }
        if (dbus_addr && *dbus_addr) {
            snprintf(dbus_env, sizeof(dbus_env), "DBUS_SESSION_BUS_ADDRESS=%s", dbus_addr);
            envp[envc++] = dbus_env;
        }
        envp[envc] = NULL;

        /* execve expects a full path; try execve first, fallback to execvpe-like behavior: */
        execve(hook, argv, (char *const *)envp);

        if (errno == ENOENT) {
            char buf[512];
            if ((size_t)snprintf(buf, sizeof(buf), "/usr/bin/%s", hook) < sizeof(buf)) {
                execve(buf, argv, (char *const *)envp);
            }
            if ((size_t)snprintf(buf, sizeof(buf), "/bin/%s", hook) < sizeof(buf)) {
                execve(buf, argv, (char *const *)envp);
            }
        }
        ERROR("Failed to exec hook '%s': %s", hook, strerror(errno));
        _exit(127);
    }

    /* Parent: wait for child and report non-zero exit */
    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        ERROR("Hook '%s' waitpid failed: %s", hook, strerror(errno));
        return;
    }

    if (WIFEXITED(status)) {
        int es = WEXITSTATUS(status);
        if (es != 0) {
            ERROR("Hook '%s' exited with status %d for PID %d (%s)", hook, es, (int)pid,
                  name ? name : "");
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        ERROR("Hook '%s' killed by signal %d for PID %d (%s)", hook, sig, (int)pid,
              name ? name : "");
    }
}