# Swordfish
`Swordfish` is a pkill-like CLI tool that's feature-rich and written in C. It lets you find and kill processes with ease and safety.

## Why use this instead of pkill?
Swordfish was created to make managing processes easier. It started as a project for me (seaslug) to learn C, which then snowballed into a full on process manager. Why use Swordfish instead of pkill or other tools? Below are some reasons!
- You prefer more control via grouped flags (e.g. `-ky`)
- You want more safety if you kill the wrong process
- You like lightweight, clean, CLI tools
- You want to not only kill processes but also view information about them quickly
- You want something significantly faster than standard GNU process tools (for some reason)

## Performance
Swordfish is significantly faster than standard GNU process tools, which are already fast enough for most situations. This tool has no right to be this fast but we've done it anyway.
Here's the results of myself benchmarking it:

| Tool | Mean time | Relative |
|------|-----------|----------|
| swordfish | 5.7ms | 1x (baseline) |
| pkill | 18.7ms | 3.28x slower |
| pgrep | 18.5ms | 3.18x slower |

*Benchmarked with hyperfine --shell=none on an AMD Ryzen 7 5700X. Results may vary.*

## Features
- Grouped flags like `-Sky` (inspired by pacman)
- Raw signal support (e.g. `-10`, `-15`, `TERM`, `KILL`)
- Lightweight and dependency-free (for now)
- Pre and post-kill script hooks (`--pre-hook <file>` / `--post-hook <file>`)
- Basic regex support
- Static mode for read-only listing
- Verbose process info
- Sorting modes (RAM, CPU, age)
- Pattern exclusions (`--exclude <pattern>`)
- Built-in retry functionality (`-r <time>`)
- Auto-completions for Bash, Fish, and Zsh
- And much more...

More can be found on the help pages

![swordfish preview](assets/img1.png)

![swordfish preview](assets/img2.png)

## Usage Examples

```bash
# Kill all 'nvim' processes using SIGTERM
swordfish -k nvim

# Kill all 'nvim' processes using SIGKILL
swordfish -K nvim

# Kill all 'nvim' and 'firefox' processes without the confirmation
swordfish -ky nvim firefox

# Kill processes selected via user input
swordfish -Sk bash

# Recursively terminate 'firefox' every 1 second
swordfish -kyr 1 firefox

# Run 'script1.sh' and 'script2.sh' before/after killing Neovim
swordfish -TERM --pre-hook script1.sh --post-hook script2.sh nvim
```

## Installation
From the AUR:
```bash
yay -S swordfish-git
```

Or manually:
```bash
git clone https://aur.archlinux.org/swordfish-git.git
cd swordfish-git
makepkg -si
```

## Building From Source
Building from source is fairly easy. All you need is `gcc` and `make`. The Makefile is fairly built out, meaning it can do quite a lot.

Default build (dev environment):
```bash
git clone https://github.com/Foox-dev/swordfish
cd swordfish
make
# Binary is output to the build directory in root
```

Release build (recommended):
```bash
git clone https://github.com/Foox-dev/swordfish
cd swordfish
make rel
# Binary is output to build/release
```

By default, the build script will automatically compile docs into `docs`. For now this is not an issue since the project is relatively small.

## License
MIT
