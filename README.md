# CatShellX

A fish-inspired interactive shell for **Mac OS X 10.4 Tiger**, built as a
universal **PowerPC/i386** binary in pure C (GNU C99). No runtime
dependencies — every non-portable libc helper (`getline`, `asprintf`,
`strcasestr`) is reimplemented for Tiger.

Version 0.1.0

---

## Features

### Line editor
A raw-mode terminal editor with a fish feel:

- Inline editing, UTF-8 aware rendering, ANSI-correct cursor positioning
- Word navigation: `Alt+Left` / `Alt+Right`
- Killing & yanking with a **kill ring**:
  - `Ctrl-W` / `Alt+Backspace` — delete word backwards
  - `Alt-D` — delete word forwards
  - `Ctrl-U` — kill to start of line
  - `Ctrl-K` — kill to end of line
  - `Ctrl-Y` — yank; `Alt-Y` — cycle through the kill ring
- **Undo/redo** with `Ctrl-_` (fish-style toggle: undo, undo again = redo)
- `Ctrl-T` — transpose characters
- `Ctrl-L` — clear screen
- `Ctrl-D` — delete character (EOF on an empty line)
- `Ctrl-C` — cancel the line

### History
- Persistent history in `~/.catshellx_history` (up to 1000 entries)
- `Up` / `Down` navigation
- `Ctrl-R` — incremental reverse search
- **Autosuggestion**: dimmed prefix match of history, accept with `Right`
  (or `Ctrl-F`) at end-of-line

### Completion
`Tab` completes:
- **Commands** — builtins, aliases and executables in `PATH`
- **File paths**
- Expands the common prefix; second `Tab` prints a menu when ambiguous

### Syntax highlighting
- Commands green when found (builtin / alias / `PATH`), red otherwise
- Quoted strings yellow, `$VAR` references magenta, operators blue,
  comments dim

### Prompt
- Default fish-like prompt: `user@host cwd ❯` (green/cyan/yellow)
- Custom prompt via `$CSX_PROMPT` with escapes (see
  [Configuration](#configuration))

### Shell language
- **Expansion**: `~`, `$VAR`, `${VAR}`, `$?`, `$(command)`, `{a,b}` braces,
  globs (`*` `?` `[]`), quote-aware
- **Pipelines** `a | b`, `&&`, `||`, `;`, background `&`
- **Redirections**: `>` `>>` `<` `2>` `2>>` `2>&1`
- **Shell variables**: `set`, `export`, `unset` with `$PWD`/`$OLDPWD`
  kept in sync by `cd`
- **Aliases**: `alias` / `unalias`, recursive first-token expansion
  (`alias ll='ls -l'`; `ll -a` runs `ls -l -a`)
- **Job control**: `&`, `jobs`, `fg`, `bg`, `Ctrl-C`, `Ctrl-Z` with proper
  terminal (foreground process group) handling
- **Startup file**: `~/.catshellxrc` is sourced at every interactive start

## Builtins

```
cd    pwd    echo    exit    true    false
help  type   env     set     export  unset
alias unalias source  .       jobs    fg     bg
```

## Installation

Requirements:

- Mac OS X 10.4 Tiger with Xcode (GCC 4.0.1)
- The 10.4u SDK at `/Developer/SDKs/MacOSX10.4u.sdk` (default location)

Build a universal (ppc + i386) binary:

```sh
make
```

> A detailed build guide — flags, targets, overrides, troubleshooting — is
> on the [wiki: Building](https://github.com/regalf/CatShellX/wiki/Building).

Run:

```sh
./catshellx
./catshellx -c 'echo hi'     # run a single command
./catshellx -h               # usage
./catshellx -v               # version
```

### Install

CatShellX is a plain command-line binary, so installation is a copy into
`PATH`:

```sh
make install                       # copies to /usr/local/bin/catshellx
make install PREFIX=$HOME          # or into $HOME/bin
```

> Tiger note: `/usr/local/bin` is not in the default `PATH` (there is no
> `/etc/paths` yet). Add this to `~/.bash_profile` (or the equivalent for
> your login shell):
>
> ```sh
> export PATH="/usr/local/bin:$PATH"
> ```

### Installer package

To build a double-clickable `.pkg` installer for distribution (uses the
`PackageMaker` shipped with Tiger's Xcode):

```sh
make pkg     # produces CatShellX-0.1.0.pkg
```

The `.pkg` installs `catshellx` to `/usr/local/bin` and is fully managed by
Apple's Installer (can also be removed via the Installer log / pkgutil-style
tools of the era).

## Testing

The project ships a PTY-based test harness (`tests/ptytest`) that drives the
shell with scripted keystrokes under a real pseudo-terminal:

```sh
make test
```

This runs the full suite (`edit1`, `editor2`, `suggest1`, `complete1`,
`expand1`, `jobs1`, `config1`). The harness uses a watchdog: any test that
does not finish in time is reported as a `WATCHDOG` failure.

## Configuration

Create `~/.catshellxrc`:

```
# aliases
alias ll='ls -l'
alias g='git'

# variables
export EDITOR=nano
MYVAR='hello world'

# custom prompt: user@host cwd $
set CSX_PROMPT='\u@\h \w \$ '
```

### `$CSX_PROMPT` escapes

| Escape | Meaning             |
|--------|---------------------|
| `\u`   | user name           |
| `\h`   | host (short)        |
| `\s`   | shell name          |
| `\w`   | cwd (`~` for HOME)  |
| `\W`   | basename of cwd     |
| `\d`   | date (`%a %b %d`)   |
| `\t`   | time (`HH:MM:SS`)   |
| `\n`   | newline             |
| `\$`   | literal `$`         |
| `\\`   | literal `\`         |
| `\e`   | ESC byte (colors)   |

Unknown escapes print the escaped character literally.

### Key bindings

| Key            | Action                        |
|----------------|-------------------------------|
| `Enter`        | execute line                  |
| `Ctrl-A`       | start of line                 |
| `Ctrl-E`       | end of line                   |
| `Left`/`Right` | move one character            |
| `Alt-Left`     | move one word back            |
| `Alt-Right`    | move one word forward         |
| `Ctrl-W`       | delete word backwards         |
| `Alt-Backspace`| delete word backwards         |
| `Alt-D`        | delete word forwards          |
| `Ctrl-U`       | kill to start of line         |
| `Ctrl-K`       | kill to end of line           |
| `Ctrl-Y`       | yank                          |
| `Alt-Y`        | cycle kill ring               |
| `Ctrl-_`       | undo / redo                   |
| `Ctrl-T`       | transpose characters          |
| `Ctrl-L`       | clear screen                  |
| `Ctrl-R`       | reverse incremental search    |
| `Up`/`Down`    | history navigation            |
| `Tab`          | complete                      |
| `Right` (end)  | accept autosuggestion         |
| `Ctrl-D`       | delete char / EOF on empty    |
| `Ctrl-C`       | cancel the line               |

## Project layout

```
CatShellX/
├── Makefile           # build + test targets (universal ppc/i386)
├── src/
│   ├── main.c         # CLI entry, interactive loop, startup rc
│   ├── line_editor.c  # raw-mode line editor + undo/kill ring
│   ├── parser.c       # tokenizer / parser
│   ├── expand.c       # word expansion (vars, subst, braces, globs)
│   ├── execute.c      # pipelines, redirections, job control
│   ├── builtins.c     # builtin commands
│   ├── vars.c         # shell variables
│   ├── alias.c        # aliases
│   ├── history.c      # persistent history
│   ├── suggest.c      # autosuggestions
│   ├── completion.c   # tab completion
│   ├── highlight.c    # syntax highlighting
│   ├── prompt.c       # prompt rendering (custom + default)
│   ├── util.c         # strbuf, portable getline/strcasestr
│   ├── shell.h        # public API
│   └── parser.h
└── tests/
    ├── ptytest.c      # PTY test harness
    ├── *.txt          # scripted keystroke tests
    ├── testrc         # rc file used by config1
    └── sourcerc       # file used by the `source` test
```

## Notes

- `make ssh` builds on a remote host over ssh (`ssh 192.168.1.9 "…"`) — used
  during development when the build machine is not the local one.
- The suite is tested against the Tiger toolchain; a plain `gcc` build with
  a modern libc also works for quick checks.

## License

[GPLv3](LICENSE). CatShellX is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your
option) any later version.
