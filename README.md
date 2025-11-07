## Custom Shell (HW2)

This repository contains my working solution for homework #2 of the VK System Programming course. The task is no longer described here—instead I document the shell I actually built, how the codebase is structured, and how to run the tooling that lives in this repo.

### What is implemented

- **Bash-like parser** (`2/parser.c`): streaming input handling with support for quotes, comments, command separators, and late evaluation. The parser is reused as-is and understands arbitrarily long lines.
- **Execution engine** (`2/my_solution.c`): spawns pipelines of any depth, wires pipes and stdout redirection (`>`/`>>`) to the last command, and restores original descriptors after each line.
- **Built-ins**: fully in-process `cd`, `exit`, `pwd`, `true`, `false`, and `echo`. `exit` honors Bash semantics—when executed directly it terminates the shell with the requested code, but inside a pipeline it behaves like a regular stage.
- **Process lifecycle**: each child PID is tracked in a small queue until waitpid() confirms completion; stdout/stderr from failures is reported via `perror`, and zombie cleanup helpers are in place.
- **Testing assets**: reproducible scenarios live in `2/tests.txt`, and `checker.py` can run them either one-by-one or in a single long session. There are also parser-focused unit tests (`2/test_parser.py`) and VK All Cups harness scripts under `allcups/`.

### Repository layout

- `2/` — the solution itself: `my_solution.c` (entry point), `parser.[ch]`, build system, tests, and task statements (`task_eng.txt`, `task_rus.txt`).
- `utils/` — helper libraries. Most notable is `heap_help/`, a lightweight leak detector described in `utils/heap_help/README.md`. There is also a tiny unit-test helper (`unit.*`, `unitpp.h`) that can be pulled into future tasks.
- `allcups/` — Dockerfile and scripts used by VK autograder. `allcups/test.sh` shows how different homework numbers are built and tested, which is handy when reproducing the remote environment locally.

### Build & run

```bash
cd 2
make              # produces ./mybash using my_solution.c + parser.c
./mybash          # interactive shell; accepts commands via stdin
```

Examples:

```bash
echo "hello" | tr a-z A-Z
cd /tmp
pwd >> /tmp/paths.log
exit 0
```

You can also feed scripts directly: `./mybash < tests/smoke.txt`.

### Testing workflow

1. **Functional suite**:
   ```bash
   cd 2
   python3 checker.py -e ./mybash --tests tests.txt
   ```
   Optional flags `--with_logic` / `--with_background` unlock bonus sections when the corresponding features are implemented.
2. **Parser-only checks**: `python3 test_parser.py` validates corner cases independently from the shell runtime.
3. **VK All Cups replica**: build the Docker image in `allcups/DockAllcups.dockerfile` and run `allcups/test.sh` with the same environment variables (`IS_LOCAL`, `HW`, etc.) that the platform uses.
4. **Leak detection**: compile your target together with `utils/heap_help/heap_help.c` (link with `-ldl -rdynamic`) and inspect the exit-time report or query `heaph_get_alloc_count()` manually.

### Known limitations

- Logical connectors `&&` and `||` are parsed but currently treated like simple sequencing; the return code is still propagated from the last command.
- Background execution via `&` is not implemented, so every pipeline is synchronous.
- Input redirection (`<`) and stderr-specific redirection are not supported.
- `echo` intentionally keeps to the simplest POSIX semantics (no `-n`, no escape processing).

Despite these limitations, the current shell passes the base VK tests (15 points) and serves as a solid foundation for the bonus features if/when I decide to extend it further.
