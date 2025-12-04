#compdef swordfish

_swordfish() {
    # === Dynamic process completion ===
    _swordfish_procs() {
        local -a procs
        # lowercase for case-insensitive matching
        procs=("${(@f)$(ps -eo comm= | tr '[:upper:]' '[:lower:]' | sort -u)}")
        compadd -a procs
    }

    # === Options ===
    _arguments \
      '-S[Select which PIDs to kill (interactive prompt)]' \
      '-k[Send SIGTERM to matching processes (graceful shutdown)]' \
      '-K[Send SIGKILL to matching processes (forceful shutdown)]' \
      '-x[Exact match process names]' \
      '-y[Auto-confirm kills; skip prompts and sudo confirmation]' \
      '-p[Print raw PIDs only]' \
      '-t[Always select the top process]' \
      '-v[Enable verbose output]' \
      '-r+[Retry on failure after waiting <time> seconds]:time (s)' \
      '-u+[Filter processes by username]:username:_users' \
      '--sort[Sort process list]:sort:(cpu ram age)' \
      '--exclude[Exclude processes matching pattern]:process:_swordfish_procs' \
      '--help[Show help message and exit]' \
      '-9[Send SIGKILL (-9)]' \
      '-KILL[Send SIGKILL (same as -K)]' \
      '-TERM[Send SIGTERM (same as -k)]' \
      '*::process:_swordfish_procs'
}

# Register completion function
compdef _swordfish swordfish
