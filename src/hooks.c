#include "hooks.h"
#include "main.h"
#include "help.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void safe_strncpy(char *dst, const char *src, size_t size) {
    if (!dst || !src || size == 0)
        return;

    size_t i = 0;
    for (; i < size - 1 && src[i]; i++)
        dst[i] = src[i];

    dst[i] = '\0';
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
        char *const argv[] = {(char *)hook, pidbuf, (char *)(name ? name : ""), NULL};

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

        execve(hook, argv, (char *const *)envp);

        if (errno == ENOENT) {
            char buf[512];
            if ((size_t)snprintf(buf, sizeof(buf), "/usr/bin/%s", hook) < sizeof(buf))
                execve(buf, argv, (char *const *)envp);
            if ((size_t)snprintf(buf, sizeof(buf), "/bin/%s", hook) < sizeof(buf))
                execve(buf, argv, (char *const *)envp);
        }
        ERROR("Failed to exec hook '%s': %s", hook, strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        ERROR("Hook '%s' waitpid failed: %s", hook, strerror(errno));
        return;
    }

    if (WIFEXITED(status)) {
        int es = WEXITSTATUS(status);
        if (es != 0)
            ERROR("Hook '%s' exited with status %d for PID %d (%s)", hook, es, (int)pid, name ? name : "");
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        ERROR("Hook '%s' killed by signal %d for PID %d (%s)", hook, sig, (int)pid, name ? name : "");
    }
}

/* Generate shell completions for fish */
void generate_fish_completions(const char *prog, const char *file_path) {
    FILE *out = stdout;
    if (file_path) {
        out = fopen(file_path, "w");
        if (!out) {
            ERROR("Failed to open file: %s", file_path);
            return;
        }
    }

    extern const swordfish_option_t swordfish_options[];
    extern const size_t swordfish_options_count;
    extern const swordfish_option_t swordfish_options_lopt[];
    extern const size_t swordfish_options_lopt_count;

    fprintf(out, "# Generated for Swordfish %s\n", SWORDFISH_VERSION);
    fprintf(out, "# Sword fish completions (it was too good not to do that)\n\n");

    fprintf(out, "# === Short options ===\n");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        const char *flag = swordfish_options[i].flag;
        const char *desc = swordfish_options[i].desc;
        if (!flag || flag[0] != '-' || flag[1] == '-') continue;
        fprintf(out, "complete -c %s -s %c -d \"%s\"\n", prog, flag[1], desc);
    }

    fprintf(out, "\n# === Long options ===\n");
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        const char *flag = swordfish_options_lopt[i].flag;
        const char *desc = swordfish_options_lopt[i].desc;
        if (!flag || flag[0] != '-' || flag[1] != '-') continue;
        const char *longflag = flag + 2;
        char longopt[64] = {0};
        size_t j = 0;
        while (longflag[j] && longflag[j] != ' ' && longflag[j] != '<' && j < sizeof(longopt) - 1) {
            longopt[j] = longflag[j];
            j++;
        }
        longopt[j] = '\0';
        fprintf(out, "complete -c %s -l %s -d \"%s\"\n", prog, longopt, desc);
    }

    fprintf(out, "\n# === Signal shorthands ===\n");
    fprintf(out, "complete -c %s -o 9 -d \"Send SIGKILL (-9)\"\n", prog);
    fprintf(out, "complete -c %s -o KILL -d \"Send SIGKILL (same as -K)\"\n", prog);
    fprintf(out, "complete -c %s -o TERM -d \"Send SIGTERM (same as -k)\"\n", prog);

    fprintf(out, "\n# === Dynamic completion for process names ===\n");
    fprintf(out, "complete -c %s -a \"(ps -eo comm= | sort -u)\" -d \"Process name\"\n", prog);

    if (file_path) {
        printf("Fish completions written into %s\n", file_path);
        fclose(out);
    }
}

/* Generate shell completions for bash */
void generate_bash_completions(const char *prog, const char *file_path) {
    FILE *out = stdout;
    if (file_path) {
        out = fopen(file_path, "w");
        if (!out) {
            ERROR("Failed to open file: %s", file_path);
            return;
        }
    }

    extern const swordfish_option_t swordfish_options[];
    extern const size_t swordfish_options_count;
    extern const swordfish_option_t swordfish_options_lopt[];
    extern const size_t swordfish_options_lopt_count;

    fprintf(out, "# Generated for Swordfish %s\n", SWORDFISH_VERSION);
    fprintf(out, "# Bash completion for %s\n\n", prog);
    fprintf(out, "_swordfish_completions()\n{\n");
    fprintf(out, "    local cur prev opts\n");
    fprintf(out, "    COMPREPLY=()\n");
    fprintf(out, "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n");
    fprintf(out, "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n\n");

    fprintf(out, "    opts=\"\n");
    for (size_t i = 0; i < swordfish_options_count; ++i) {
        if (swordfish_options[i].flag)
            fprintf(out, "    %s\n", swordfish_options[i].flag);
    }
    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        if (swordfish_options_lopt[i].flag)
            fprintf(out, "    %s\n", swordfish_options_lopt[i].flag);
    }
    fprintf(out, "    -9\n    -KILL\n    -TERM\n");
    fprintf(out, "\"\n\n");

    fprintf(out, "    if [[ \"${cur}\" == -* ]]; then\n");
    fprintf(out, "        COMPREPLY=( $(compgen -W \"${opts}\" -- \"${cur}\") )\n");
    fprintf(out, "        return 0\n");
    fprintf(out, "    fi\n\n");

    fprintf(out, "    case \"${prev}\" in\n");
    fprintf(out, "        -u)\n");
    fprintf(out, "            COMPREPLY=( $(compgen -W \"$(cut -d: -f1 /etc/passwd)\" -- \"${cur}\") )\n");
    fprintf(out, "            return 0\n");
    fprintf(out, "            ;;\n");
    fprintf(out, "        --sort)\n");
    fprintf(out, "            COMPREPLY=( $(compgen -W \"cpu ram age\" -- \"${cur}\") )\n");
    fprintf(out, "            return 0\n");
    fprintf(out, "            ;;\n");
    fprintf(out, "        --exclude)\n");
    fprintf(out, "            COMPREPLY=( $(compgen -W \"$(ps -eo comm= | sort -u)\" -- \"${cur}\") )\n");
    fprintf(out, "            return 0\n");
    fprintf(out, "            ;;\n");
    fprintf(out, "    esac\n\n");

    fprintf(out, "    local cur_lc=$(echo \"$cur\" | tr '[:upper:]' '[:lower:]')\n");
    fprintf(out, "    local procs=$(ps -eo comm= | tr '[:upper:]' '[:lower:]' | sort -u)\n");
    fprintf(out, "    COMPREPLY=( $(compgen -W \"${procs}\" -- \"${cur_lc}\") )\n");
    fprintf(out, "    return 0\n");
    fprintf(out, "}\n\n");
    fprintf(out, "complete -F _swordfish_completions %s\n", prog);

    if (file_path) {
        printf("Bash completions written into %s\n", file_path);
        fclose(out);
    }
}

/* Generate shell completions for zsh */
void generate_zsh_completions(const char *prog, const char *file_path) {
    FILE *out = stdout;
    if (file_path) {
        out = fopen(file_path, "w");
        if (!out) {
            ERROR("Failed to open file: %s", file_path);
            return;
        }
    }

    extern const swordfish_option_t swordfish_options[];
    extern const size_t swordfish_options_count;
    extern const swordfish_option_t swordfish_options_lopt[];
    extern const size_t swordfish_options_lopt_count;

    fprintf(out, "#compdef %s\n\n", prog);
    fprintf(out, "# Generated for Swordfish %s\n", SWORDFISH_VERSION);
    fprintf(out, "_swordfish() {\n");
    fprintf(out, "    _swordfish_procs() {\n");
    fprintf(out, "        local -a procs\n");
    fprintf(out, "        procs=(\"${(@f)$(ps -eo comm= | tr '[:upper:]' '[:lower:]' | sort -u)}\")\n");
    fprintf(out, "        compadd -a procs\n");
    fprintf(out, "    }\n\n");
    fprintf(out, "    _arguments \\\n");

    for (size_t i = 0; i < swordfish_options_count; ++i) {
        const char *flag = swordfish_options[i].flag;
        const char *desc = swordfish_options[i].desc;
        if (!flag) continue;
        fprintf(out, "      '%s[%s]' \\\n", flag, desc);
    }

    for (size_t i = 0; i < swordfish_options_lopt_count; ++i) {
        const char *flag = swordfish_options_lopt[i].flag;
        const char *desc = swordfish_options_lopt[i].desc;
        if (!flag) continue;
        const char *longflag = flag + 2;
        if (strncmp(longflag, "sort", 4) == 0)
            fprintf(out, "      '--sort[%s]:sort:(cpu ram age)' \\\n", desc);
        else if (strncmp(longflag, "exclude", 7) == 0)
            fprintf(out, "      '--exclude[%s]:process:_swordfish_procs' \\\n", desc);
        else
            fprintf(out, "      '--%s[%s]' \\\n", longflag, desc);
    }

    fprintf(out, "      '-9[Send SIGKILL (-9)]' \\\n");
    fprintf(out, "      '-KILL[Send SIGKILL (same as -K)]' \\\n");
    fprintf(out, "      '-TERM[Send SIGTERM (same as -k)]' \\\n");
    fprintf(out, "      '*::process:_swordfish_procs'\n");
    fprintf(out, "}\n\n");
    fprintf(out, "compdef _swordfish %s\n", prog);

    if (file_path) {
        printf("Zsh completions written into %s\n", file_path);
        fclose(out);
    }
}
