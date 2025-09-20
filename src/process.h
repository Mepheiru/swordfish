#ifndef PROCESS_H
#define PROCESS_H
#include "args.h"

int scan_processes(const swordfish_args_t *args, char **patterns, int pattern_count);
void drop_privileges(void);

bool is_all_digits(const char *s);
bool is_zombie_process(pid_t pid);

#endif // PROCESS_H
