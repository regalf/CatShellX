# CatShellX

A fish-inspired interactive shell for **Mac OS X 10.4 Tiger**, built as a
universal **PowerPC/i386** binary in pure C (GNU C99). No runtime
dependencies — every non-portable libc helper (`getline`, `asprintf`,
`strcasestr`) is reimplemented for Tiger.

Version 0.1.1

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
- **Window title**: sets the terminal title (`user@host: cwd`, or a
  `$CSX_TITLE` template) at every prompt via OSC 0, and clears it while a
  foreground job runs — a compatible emulator like TerminalX then shows the
  running program's name (`vim`, `less`, ...)

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
./Build/catshellx
./Build/catshellx -c 'echo hi'     # run a single command
./Build/catshellx -h               # usage
./Build/catshellx -v               # version
```

### Install

CatShellX is a plain command-line binary, so installation is a copy into
`PATH`. `cat_config` (the interactive configurator, see below) is installed
the same way:

```sh
make install                       # catshellx + cat_config into /usr/local/bin
make install PREFIX=$HOME          # or into $HOME/bin
```

> Tiger note: `/usr/local/bin` is not in the default `PATH` (there is no
> `/etc/paths` yet). The shell defines a default alias
> `cat_config='/usr/local/bin/cat_config'` at startup when the binary is
> installed there but not already reachable, so `cat_config` works even when
> the terminal launches the shell without sourcing a login profile. To also
> reach `catshellx` by name from bash, add this to `~/.bash_profile`:
>
> ```sh
> export PATH="/usr/local/bin:$PATH"
> ```

### Installer package

The build places the binaries in `Build/` and installers in `Packages/`.
To build a double-clickable `.pkg` installer for distribution (uses the
`PackageMaker` shipped with Tiger's Xcode), and a `.dmg` wrapper around it:

```sh
make pkg     # produces Packages/CatShellX-0.1.1.pkg
make dmg     # produces Packages/CatShellX-0.1.1.dmg (wraps the .pkg)
```

The `.pkg` installs `catshellx` and `cat_config` to `/usr/local/bin` and is
fully managed by Apple's Installer (can also be removed via the Installer log
/ pkgutil-style tools of the era).

## `cat_config` (TUI configurator)

`cat_config` is an interactive, terminal-based configurator in the spirit of
`fish_config`. It edits `~/.catshellxrc` for you, so you never need to type
the syntax by hand:

```sh
cat_config
```

Requires a real terminal (raw mode via termios). `j`/`k` or arrow keys
navigate, `Enter` selects, `Esc` goes back. On Tiger, `cat_config` is also
provided as a shell default alias (`/usr/local/bin/cat_config`) when it is
not already reachable via `PATH`, so the bare name works no matter how the
shell was launched. Main menu:

| Entry | What it edits |
|-------|---------------|
| **Prompt (live preview)** | `set CSX_PROMPT=…` |
| **Window title** | `set CSX_TITLE=…` / `set CSX_TITLE_OFF=1` |
| **Aliases** | `alias name='value'` |
| **Behavior** | `CSX_SUGGEST`, `CSX_HIGHLIGHT`, `CSX_BEEP`, `CSX_HISTSIZE` |
| **Save and exit** | writes the rc, exits |
| **Exit without saving** | discards changes (confirm prompt if dirty) |

### Prompt screen

Five presets — default fish (`user@host cwd ❯`), minimal (`\w> `),
bash-like (`\u@\h \w \$ `), fish-colored, custom template — plus a read-only
row showing the current prompt. A live preview renders the selected template
with the real user/host/cwd. The custom field starts empty unless you
already have a non-default custom prompt, so typing replaces rather than
appends.

### Title screen

Toggle the window title (OSC 0) on/off and edit the `$CSX_TITLE` template
(default `\u@\h: \w`), with a live preview.

### Aliases screen

`Enter` edits the selected alias' value; `Enter` on the `[Add new alias]`
row asks for name then value; `d` (or `x`/`Del`) deletes. Values are
single-quoted on save, double-quoted (escaping `\`/`"`) when they contain a
`'`.

### Behaviour screen

`Enter`/`Space` toggles; the history row opens a numeric field (1–1000):

| Row | rc line | Default |
|-----|---------|---------|
| Autosuggestions | `set CSX_SUGGEST=0` | ON |
| Syntax highlighting | `set CSX_HIGHLIGHT=0` | ON |
| Beeper (bell on errors) | `set CSX_BEEP=0` | ON |
| Window title | `set CSX_TITLE_OFF=1` | ON (title enabled) |
| History size | `set CSX_HISTSIZE=…` | 1000 |

Only **non-default** values are written to the rc.

### Field editor

Used for the custom prompt/title and alias name/value: `Ctrl-A`/`Home` and
`Ctrl-E`/`End`, `Ctrl-B`/`Ctrl-F` or `←`/`→` to move, `Ctrl-U` clear,
`Ctrl-K` kill to end, `Del`/`Backspace` delete, `Enter` confirms,
`Esc`/`Ctrl-C` cancels.

### Saving

**Save and exit** rewrites the rc as a managed block (`set CSX_PROMPT=…`,
`set CSX_TITLE=…`, non-default toggles, all aliases) followed by a
`# --- other settings ---` section holding **every other line verbatim**
(comments, `export`, your own variables). The previous file is kept as
`~/.catshellxrc.bak`; managed lines reset to their default are dropped.
Quitting with unsaved changes shows a save/discard/cancel prompt.

Prompt and title templates use the same escapes as `$CSX_PROMPT` (see the
table in [Configuration](#configuration)).

## Testing

The project ships a PTY-based test harness (`Build/ptytest`, built from
`tests/ptytest.c`) that drives the shell with scripted keystrokes under a
real pseudo-terminal:

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

# terminal title: user@host: cwd (default), or a template
set CSX_TITLE='\u@\h: \w'
```

`$CSX_TITLE` uses the same escapes as `$CSX_PROMPT`; the default title is
`\u@\h: \w`.

> Tip: instead of editing this file by hand, use
> [cat_config](#cat_config-tui-configurator), which rewrites the rc for you
> while preserving your other lines.

### Behaviour toggles

All default to **on** (title on, history 1000); set `0`/`off` to disable:

| rc line | Meaning |
|---------|---------|
| `set CSX_SUGGEST=0` | disable autosuggestions |
| `set CSX_HIGHLIGHT=0` | disable syntax highlighting |
| `set CSX_BEEP=0` | disable the bell on errors |
| `set CSX_TITLE_OFF=1` | disable the window title |
| `set CSX_HISTSIZE=200` | history size, 1–1000 |

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
│   ├── title.c        # OSC 0 window title (TerminalX/xterm)
│   ├── completion.c   # tab completion
│   ├── highlight.c    # syntax highlighting
│   ├── prompt.c       # prompt rendering (custom + default)
│   ├── util.c         # strbuf, portable getline/strcasestr
│   ├── shell.h        # public API
│   └── parser.h
├── tools/
│   └── cat_config.c   # interactive TUI configurator
└── tests/
    ├── ptytest.c      # PTY test harness
    ├── *.txt          # scripted keystroke tests
    ├── testrc         # rc file used by config1
    └── sourcerc       # file used by the `source` test
```

## Notes

- **TerminalX compatibility**: CatShellX sets the terminal window title via
  OSC 0 at every prompt and clears it while a foreground job runs. Combined
  with its job control (`tcsetpgrp`), TerminalX's foreground-process
  detection shows the running program (`vim`, `less`, ...) in the title, and
  the shell's own `user@host: cwd` title when idle.
- `make ssh` builds on a remote host over ssh (`ssh 192.168.1.9 "…"`) — used
  during development when the build machine is not the local one.
- The suite is tested against the Tiger toolchain; a plain `gcc` build with
  a modern libc also works for quick checks.

## License

[GNU General Public License v3.0](LICENSE).
