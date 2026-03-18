#include "test.h"
#include "../src/args.h"

#include <signal.h>
#include <string.h>

// suppress expected-failure stderr noise for tests that intentionally
// pass bad input — caller should restore stderr after if needed
#include <stdio.h>

/* helpers */

// builds a fresh args parse from a literal argv
// argc is computed from the sentinel NULL — argv must be NULL-terminated
#define PARSE(argv_arr, out) ({ \
    int _argc = 0; \
    while ((argv_arr)[_argc]) _argc++; \
    parse_args(&_argc, (char **)(argv_arr), out); \
})

void test_args(void) {
    swordfish_args_t args;

    SUITE("get_signal");

    CHECK(get_signal("9")    == SIGKILL,  "numeric 9 -> SIGKILL");
    CHECK(get_signal("15")   == SIGTERM,  "numeric 15 -> SIGTERM");
    CHECK(get_signal("1")    == SIGHUP,   "numeric 1 -> SIGHUP");
    CHECK(get_signal("TERM") == SIGTERM,  "TERM -> SIGTERM");
    CHECK(get_signal("HUP")  == SIGHUP,   "HUP -> SIGHUP");
    CHECK(get_signal("kill") == SIGKILL,  "kill (lowercase) -> SIGKILL");
    CHECK(get_signal("KILL") == SIGKILL,  "KILL -> SIGKILL");
    CHECK(get_signal("999")  == -1,       "out-of-range signal returns -1");
    CHECK(get_signal("FAKE") == -1,       "unknown name returns -1");

    SUITE("parse_args — defaults");

    {
        char *argv[] = {"swordfish", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.operation    == SWOP_STATIC, "default operation is SWOP_STATIC");
        CHECK(args.sig          == SIGTERM,     "default signal is SIGTERM");
        CHECK(args.sort_mode    == SWSORT_NONE, "default sort mode is SWSORT_NONE");
        CHECK(args.exact_match  == 0,           "exact_match off by default");
        CHECK(args.auto_confirm == 0,           "auto_confirm off by default");
        CHECK(args.dry_run      == 0,           "dry_run off by default");
        CHECK(args.verbose_level == 0,          "verbose_level 0 by default");
    }

    SUITE("parse_args — operations");

    {
        char *argv[] = {"swordfish", "-k", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.operation == SWOP_KILL, "-k sets SWOP_KILL");
    }

    {
        char *argv[] = {"swordfish", "-S", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.operation == SWOP_SELECT, "-S sets SWOP_SELECT");
    }

    {
        char *argv[] = {"swordfish", "-F", NULL};
        PARSE(argv, &args);
        CHECK(args.operation == SWOP_FUZZY, "-F sets SWOP_FUZZY");
    }

    SUITE("parse_args — signal extraction");

    {
        char arg1[] = "-k9";
        char *argv[] = {"swordfish", arg1, "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.sig == SIGKILL, "-k9 extracts SIGKILL");
        CHECK(args.operation == SWOP_KILL, "-k9 still sets SWOP_KILL");
    }

    {
        char arg1[] = "-kHUP";
        char *argv[] = {"swordfish", arg1, "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.sig == SIGHUP, "-kHUP extracts SIGHUP");
    }

    {
        char arg1[] = "-kTERM";
        char *argv[] = {"swordfish", arg1, "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.sig == SIGTERM, "-kTERM extracts SIGTERM");
    }

    // modifiers after signal should still be applied
    {
        char arg1[] = "-k9y";
        char *argv[] = {"swordfish", arg1, "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.sig == SIGKILL,   "-k9y extracts SIGKILL");
        CHECK(args.auto_confirm == 1, "-k9y applies -y modifier");
    }

    SUITE("parse_args — modifiers");

    {
        char *argv[] = {"swordfish", "-x", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.exact_match == 1, "-x sets exact_match");
    }

    {
        char *argv[] = {"swordfish", "-y", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.auto_confirm == 1, "-y sets auto_confirm");
    }

    {
        char *argv[] = {"swordfish", "-p", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.print_pids_only == 1, "-p sets print_pids_only");
    }

    {
        char *argv[] = {"swordfish", "-n", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.dry_run == 1, "-n sets dry_run");
    }

    {
        char *argv[] = {"swordfish", "-t", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.top_only == 1, "-t sets top_only");
    }

    {
        char *argv[] = {"swordfish", "-w", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.wait_for_death == 1, "-w sets wait_for_death");
    }

    {
        char *argv[] = {"swordfish", "-r", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.hide_root == 1, "-r sets hide_root");
    }

    // verbose caps at 3
    {
        char *argv[] = {"swordfish", "-v", "-v", "-v", "-v", "-v", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.verbose_level == 3, "-v capped at 3");
    }

    // stackable modifiers
    {
        char *argv[] = {"swordfish", "-xyn", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.exact_match == 1 && args.auto_confirm == 1 && args.dry_run == 1,
              "stacked -xyn applies all three");
    }

    SUITE("parse_args — long opts");

    {
        char *argv[] = {"swordfish", "--sort", "ram", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.sort_mode == SWSORT_RAM, "--sort ram sets SWSORT_RAM");
    }

    {
        char *argv[] = {"swordfish", "--sort", "age", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.sort_mode == SWSORT_AGE, "--sort age sets SWSORT_AGE");
    }

    {
        char *argv[] = {"swordfish", "--user", "seaslug", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.user != NULL && strcmp(args.user, "seaslug") == 0,
              "--user sets user field");
    }

    {
        char *argv[] = {"swordfish", "--format", "%pid %name", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.format != NULL && strcmp(args.format, "%pid %name") == 0,
              "--format stores format string");
    }

    {
        char *argv[] = {"swordfish", "--timeout", "5", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.timeout == 5, "--timeout sets timeout");
    }

    {
        char *argv[] = {"swordfish", "--retry", "3", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.retry_time == 3, "--retry sets retry_time");
    }

    {
        char *argv[] = {"swordfish", "--exclude", "systemd", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.exclude_count == 1, "--exclude increments exclude_count");
        CHECK(strcmp(args.exclude_patterns[0], "systemd") == 0,
              "--exclude stores pattern");
    }

    {
        char *argv[] = {"swordfish", "--parent", "1234", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.parent_pid == 1234, "--parent sets parent_pid");
    }

    {
        char *argv[] = {"swordfish", "--session", "42", "firefox", NULL};
        PARSE(argv, &args);
        CHECK(args.session_id == 42, "--session sets session_id");
    }

    SUITE("parse_args — error cases");

    // two operation flags should fail
    {
        FILE *devnull = fopen("/dev/null", "w");
        FILE *old_stderr = stderr;
        stderr = devnull;

        char *argv[] = {"swordfish", "-k", "-S", "firefox", NULL};
        int rc = PARSE(argv, &args);
        CHECK(rc != 0, "two operation flags returns error");

        stderr = old_stderr;
        fclose(devnull);
    }

    // no pattern with non-fuzzy op should fail
    {
        FILE *devnull = fopen("/dev/null", "w");
        FILE *old_stderr = stderr;
        stderr = devnull;

        char *argv[] = {"swordfish", "-k", NULL};
        int rc = PARSE(argv, &args);
        CHECK(rc != 0, "missing pattern returns error");

        stderr = old_stderr;
        fclose(devnull);
    }

    // negative timeout clamps to 0, not an error
    {
        char *argv[] = {"swordfish", "--timeout", "-5", "firefox", NULL};
        int rc = PARSE(argv, &args);
        CHECK(rc == 0 && args.timeout == 0, "negative timeout clamped to 0");
    }
}
