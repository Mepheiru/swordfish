# Swordfish TODO

Stuff that needs to be done. This document also contains some potential ideas for new features or tweaks.

## TODO

- Code cleanup:
  - Comments need to be quite a lot better.
  - Rename many functions to better suit their function.
  - Remove unused code.
- Rework how arguments are used to better fit the vision. Current way it's set up is messy and confusing.
- Write "vision.md". It will contain the projects philosophy and the vision of what swordfish will be when finished.
- Write "program.md". It will layout how the program runs and the purpose of each file. Write AFTER the main function is reorginized!!!
- Improve error handling greatly.
- exit codes. 0 = found and acted, 1 = no match, 2 = permission denied, 3 = partial success. scripting much cleaner.
- Graceful kill timeout. Send SIGTERM, wait N seconds, escalate to SIGKILL automatically if the process doesn't die. `-kt 5 firefox` style.

## Features and Tweaks

- Config file support for default flags and settings.
- Improved piping support for safer read-only mode and to help other programs understand the swordfish output.
- More advanced regex arguments.
- Additional scripting hooks.
- Process tree support.
- Argument for watching processes update live (spying).
- JSON/CSV export of process lists.
- Plugin and scripting support.
- Ncurses interface.
- Add impl tests to swordfish.
- Sort mode for process state (running, sleeping, zombie).
- Process fuzzy finder? I have no idea.
- Proper systemd integration.
- Better handling of processes with rapidly changing state (race conditions).
- Watch mode. Notify the user (via desktop notification or CLI) when a watched process changes state or exits.
- Save or load filter rules. Allow the user to specify filter rules in a config, then use "swordfish (filter_rule_name) (process)" to load it in.
- Parrent PID matching support (-P)
- Pidfile support (-f)
- Session ID matching stuff.
- CPU usage tracking.
- Memory leak detection. Watch a process's RSS grow over time and alert when it crosses a threshold.
- Process ancestry. Show the full parent chain, not just PPID.
- Detect duplicate processes. Warn when multiple instances of something that should be a singleton are running.
- Batch operations. Kill all processes over X MB RAM, kill all zombies, kill everything owned by a user.
- Color themes!!!
- Custom output format strings. `swordfish --format "%pid %name %ram" discord` like ps's `-o`.
- Diff mode. Show what processes appeared or disappeared between two runs.
- Protected process list. Config file of processes swordfish will never kill even with `-y`.
- Kill log. Append every signal sent to a log file with timestamp, PID, name, user.
- `--wait` flag. Send signal then block until the process is actually gone. Useful in scripts where you need to know it's dead before continuing.
- Named filter profiles already on the list. `swordfish @cleanup` loads a saved filter set.
- Environment variable support for default flags. `SWORDFISH_DEFAULT_FLAGS=-y`.
- Resource budgets. `swordfish --budget ram=4GB` — if the sum of matched processes exceeds the budget, kill the biggest one.
