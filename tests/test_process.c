// Unit tests for process.c using Criterion
#include <criterion/criterion.h>
#include <criterion/logging.h>
#include "../src/process.h"
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h> // For FILE and popen

// Helper to run swordfish and capture output
static int run_swordfish(const char *args, char *output, size_t outlen)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s/build/swordfish %s 2>&1", getenv("PWD"), args);
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;
    size_t n = fread(output, 1, outlen - 1, fp);
    output[n] = '\0';
    int status = pclose(fp);
    return status;
}

// Helper to check if a process with a given PID has a specific argv[0] (cmdline)
static int check_proc_cmdline(pid_t pid, const char *expected)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0)
        return 0;
    buf[n] = '\0';
    return strstr(buf, expected) != NULL;
}

Test(is_all_digits, valid_digits)
{
    cr_assert(is_all_digits("123456"));
    cr_assert(is_all_digits("0"));
}

Test(is_all_digits, invalid_digits)
{
    cr_assert_not(is_all_digits("12a34"));
    cr_assert_not(is_all_digits("abc"));
    cr_assert_not(is_all_digits(""));
}

Test(is_all_digits, leading_zeros)
{
    cr_log_info("Testing is_all_digits with leading zeros\n");
    cr_assert(is_all_digits("000123"), "'000123' should be all digits");
}

Test(is_all_digits, whitespace_and_special)
{
    cr_log_info("Testing is_all_digits with whitespace and special chars\n");
    cr_assert_not(is_all_digits(" 123"), "' 123' should not be all digits");
    cr_assert_not(is_all_digits("123 "), "'123 ' should not be all digits");
    cr_assert_not(is_all_digits("12\n34"), "'12\\n34' should not be all digits");
    cr_assert_not(is_all_digits("12\t34"), "'12\\t34' should not be all digits");
    cr_assert_not(is_all_digits("12-34"), "'12-34' should not be all digits");
}

Test(is_zombie_process, detects_zombie)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        _exit(0);
    }
    else
    {
        sleep(1); // let child exit
        cr_assert(is_zombie_process(pid));
        int status;
        waitpid(pid, &status, 0); // clean up
    }
}

Test(is_zombie_process, detects_non_zombie)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        sleep(5);
        _exit(0);
    }
    else
    {
        sleep(1);
        cr_assert_not(is_zombie_process(pid));
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
}

Test(is_zombie_process, non_existent_pid)
{
    cr_log_info("Testing is_zombie_process with a non-existent PID\n");
    pid_t fake_pid = 999999; // unlikely to exist
    cr_assert_not(is_zombie_process(fake_pid), "Non-existent PID should not be a zombie");
}

Test(is_zombie_process, parent_is_zombie)
{
    cr_log_info("Testing is_zombie_process with parent process (should not be zombie)\n");
    cr_assert_not(is_zombie_process(getpid()), "Current process should not be a zombie");
}

Test(cli, dry_run_does_not_kill)
{
    cr_log_info("Testing dry run does not kill process\n");
    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("sh", "sh", "-c", "exec -a swordfishtest sleep 10", NULL);
        _exit(1);
    }
    else
    {
        sleep(1);
        cr_assert(check_proc_cmdline(pid, "swordfishtest"), "Test process did not start with expected argv[0]");
        char out[256];
        int status = run_swordfish("-N swordfishtest", out, sizeof(out));
        cr_log_info("swordfish output: %s\n", out);
        cr_assert(WIFEXITED(status), "swordfish did not exit cleanly");
        cr_assert_eq(kill(pid, 0), 0, "dry run killed the process");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
}

Test(cli, user_level_process_no_sudo_needed)
{
    cr_log_info("Testing user process does not require sudo\n");
    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("sh", "sh", "-c", "exec -a swordfishtest sleep 10", NULL);
        _exit(1);
    }
    else
    {
        sleep(1);
        cr_assert(check_proc_cmdline(pid, "swordfishtest"), "Test process did not start with expected argv[0]");
        char out[256];
        run_swordfish("-k swordfishtest", out, sizeof(out));
        cr_log_info("swordfish output: %s\n", out);
        cr_assert_not(strstr(out, "sudo"), "Should not prompt for sudo for user process");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
}

Test(cli, argument_chaining)
{
    cr_log_info("Testing argument chaining -ky\n");
    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("sh", "sh", "-c", "exec -a swordfishtest sleep 10", NULL);
        _exit(1);
    }
    else
    {
        sleep(1);
        cr_assert(check_proc_cmdline(pid, "swordfishtest"), "Test process did not start with expected argv[0]");
        char out[256];
        run_swordfish("-ky swordfishtest", out, sizeof(out));
        cr_log_info("swordfish output: %s\n", out);
        cr_assert(strstr(out, "signal"), "Chained arguments did not send signal");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
}

Test(cli, all_arguments_expected)
{
    cr_log_info("Testing all main arguments\n");
    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("sh", "sh", "-c", "exec -a swordfishtest sleep 10", NULL);
        _exit(1);
    }
    else
    {
        sleep(1);
        cr_assert(check_proc_cmdline(pid, "swordfishtest"), "Test process did not start with expected argv[0]");
        char out[512];
        int status = run_swordfish("-N -x -p swordfishtest", out, sizeof(out));
        cr_log_info("swordfish output: %s\n", out);
        cr_assert(WIFEXITED(status), "swordfish did not exit cleanly");
        cr_assert(strlen(out) > 0 || strstr(out, "No processes matched"), "Expected dry run output or PIDs");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
}
