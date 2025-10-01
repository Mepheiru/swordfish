#include "main.h"
#include "args.h"
#include "process.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// TODO: make sure that run_static is set properly when using only patterns that dont send sigs
// TODO: EGC: --sort age <pattern> for some reason uses sig 15, when it should run_static

//TODO: make sure sort works properly
//TODO: make sure that the proc search dosent read all of /proc every time, and only reads stats for the specified sort mode (if any)

/* Main entry point */
int main(int arg_count, char **argv) {
    swordfish_args_t args;
    int ret = parse_args(arg_count, argv, &args);
    if (ret)
        return ret;

    int pattern_count = arg_count - args.pattern_start_idx;
    char **patterns = &argv[args.pattern_start_idx];

    /* if args contains an empty string, ask user for confirmation.
        We dont check if pattern_count is 0, because
        parse_args already handles that case. */
    bool has_empty_pattern = false;
    for (int i = 0; i < pattern_count; ++i) {
        if (patterns[i][0] == '\0') {
            has_empty_pattern = true;
            break;
        }
    }

    /* Actually send the warning message for empty string */
    if (has_empty_pattern && !args.auto_confirm && is_interactive()) {
        WARN("Empty pattern specified. \nThis may match all processes and could be " COLOR_WARN
             "dangerous!" COLOR_RESET);
        printf("Proceed? [y/N]: ");
        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (tolower(confirm[0]) != 'y') {
            return 1;
        }
    }

    bool pattern_is_pid[pattern_count];
    for (int i = 0; i < pattern_count; ++i)
        pattern_is_pid[i] = is_all_digits(patterns[i]);
    pattern_list_t plist = {
        .patterns = patterns, .pattern_is_pid = pattern_is_pid, .pattern_count = pattern_count};
    return scan_processes(&args, &plist);
}
