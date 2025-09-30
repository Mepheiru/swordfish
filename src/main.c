#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "args.h"
#include "process.h"
#include "main.h"

int main(int arg_count, char **argv) {
    // If invalid args, return error code
    swordfish_args_t args;
    int ret = parse_args(arg_count, argv, &args);
    if (ret)
        return ret;

    int pattern_count = arg_count - args.pattern_start_idx;
    char **patterns = &argv[args.pattern_start_idx];

    // if args contains an empty string, ask user for confirmation
    bool has_empty_pattern = false;
    for (int i = 0; i < pattern_count; ++i) {
        if (patterns[i][0] == '\0') {
            has_empty_pattern = true;
            break;
        }
    }

    if (has_empty_pattern && !args.auto_confirm && is_interactive()) {
        WARN("Empty pattern specified. \nThis may match all processes and could be " COLOR_WARN "dangerous!" COLOR_RESET);
        printf("Proceed? [y/N]: ");
        char confirm[8] = {0};
        fgets(confirm, sizeof(confirm), stdin);
        if (tolower(confirm[0]) != 'y') {
            printf("Aborted\n");
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
