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

## Features and Tweaks

- Config file support for default flags and settings.
- Improved piping support for safer read-only mode and to help other programs understand the swordfish output.
- More advabced regex arguments.
- Additional scripting hooks.
- Process tree support.
- Argument for hiding root processes.
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
- 
