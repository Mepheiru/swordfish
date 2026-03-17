# Swordfish

## What is Swordfish?
Swordfish is a fast and modern process management tool for Linux. It can find, display, and kill processes faster and with better UX than standard GNU util tools.

## What Swordfish is NOT
- Swordfish is NOT trying to be a drop-in compatible replacement for pkill/pgrep.
  It has its own argument convention and does things differently.
- Swordfish is NOT a system monitor (that's top-like tools)
- Swordfish is NOT a process supervisor (that's systemd)
- Swordfish is NOT a general POSIX tool. It's linux first intentionally.

## Philosophy
The default path the tool takes should be the fastest possible path to get the required result. Every flag the user doesn't pass should not cost performance. `swordfish helix` should be instant. Expensive features exist but are opt in.

## Argument Structure
- Operations are capital flags: they change what swordfish fundamentally does.
- Modifiers are lowercase flags: they change how the operation behaves.
- Anything that takes a value is a long opt.
- `-k` is the kill operation, signal embeds directly after it (`-k9`, `-kHUP`)
- If it takes a value it's a long opt. If it doesn't it's a short opt.

## Performance Contract
The default path the code takes in basic operations must always be faster than pkill and pgrep.
New features that hit this space should be benchmarked and compared to the last stable Swordfish version, along with pkill and pgrep.
Expensive features are always opt in and should never degrade basic operation speed.
