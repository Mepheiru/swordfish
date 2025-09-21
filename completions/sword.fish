# Swordfish fish completions

# === Options ===
complete -c swordfish -s S -d "Select which PIDs to kill (interactive prompt)"
complete -c swordfish -s k -d "Send SIGTERM to matching processes (graceful shutdown)"
complete -c swordfish -s K -d "Send SIGKILL to matching processes (forceful shutdown)"
complete -c swordfish -s x -d "Exact match process names"
complete -c swordfish -s y -d "Auto-confirm kills; skip prompts and sudo confirmation"
complete -c swordfish -s p -d "Print raw PIDs only"
complete -c swordfish -s t -d "Always select the top process"
complete -c swordfish -s v -d "Enable verbose output"
complete -c swordfish -s r -d "Retry on failure after waiting <time> seconds" -x
complete -c swordfish -s u -d "Filter processes by username" -a "(cut -d: -f1 /etc/passwd)"
complete -c swordfish -l sort -d "Sort process list" -a "cpu ram age"
complete -c swordfish -l exclude -d "Exclude processes matching pattern" -x
complete -c swordfish -l help -d "Show help message and exit"

# Allow -<SIGNAL> shorthand (e.g. -9, -KILL)
complete -c swordfish -o 9 -d "Send SIGKILL (-9)"
complete -c swordfish -o KILL -d "Send SIGKILL (same as -K)"
complete -c swordfish -o TERM -d "Send SIGTERM (same as -k)"

# === Dynamic completion for process names ===
# ps -eo comm= lists process names; sort -u removes duplicates
complete -c swordfish -a "(ps -eo comm= | sort -u)" -d "Process name"