# CatShellX

A fish-inspired interactive shell for **Mac OS X 10.4 Tiger**, built as a
universal **PowerPC/i386** binary in pure C (GNU C99). No runtime
dependencies — every non-portable libc helper (`getline`, `asprintf`,
`strcasestr`) is reimplemented for Tiger.

Version 0.2.0

## Screenshots

See the [gallery](gallery/) (coming soon).

## Features

- **Fish-like line editor** — inline editing, undo/redo, kill ring,
  syntax highlighting, autosuggestions
- **Prompt** — default `user@host cwd ❯` or a fully customizable
  `$CSX_PROMPT` template
- **Terminal title** — sets the window title via OSC 0 and clears it during
  foreground jobs, so TerminalX shows the active program (`vim`, `less`, …)
- **Startup greeting** — customizable multi-line welcome message via
  `$CSX_GREETINGS`
- **`cat_config`** — interactive TUI configurator (like `fish_config`) for
  prompt, window title, greeting, startup commands, aliases and behaviour
  toggles
- **Shell language** — variables, aliases, pipelines, redirections, job
  control, tab completion, globbing and command substitution

Details are on the [wiki](https://github.com/regalf/CatShellX/wiki).

## Quick start

Requirements: Mac OS X 10.4 Tiger with Xcode (GCC 4.0.1) and the 10.4u SDK
at `/Developer/SDKs/MacOSX10.4u.sdk`.

```sh
make                  # builds catshellx + cat_config + ptytest in Build/
./Build/catshellx     # start the shell
./Build/catshellx -v  # version
```

## Install

```sh
make install                          # catshellx + cat_config into /usr/local/bin
make pkg                              # Packages/CatShellX-<ver>.pkg (Installer)
make dmg                              # ... and a .dmg wrapping the .pkg
```

> Tiger note: `/usr/local/bin` is not on the default `PATH`. The shell
> defines `cat_config='/usr/local/bin/cat_config'` automatically when the
> binary is installed but not reachable. To also run `catshellx` from bash,
> add `export PATH="/usr/local/bin:$PATH"` to `~/.bash_profile`.

## Documentation

| Topic | Where |
|-------|-------|
| [Features](https://github.com/regalf/CatShellX/wiki/Features) | full feature list |
| [Configuration](https://github.com/regalf/CatShellX/wiki/Configuration) | `~/.catshellxrc`, `$CSX_PROMPT` escapes, toggles |
| [cat_config (TUI)](https://github.com/regalf/CatShellX/wiki/Cat-config) | the interactive configurator |
| [Builtins](https://github.com/regalf/CatShellX/wiki/Builtins) | builtin commands |
| [Key bindings](https://github.com/regalf/CatShellX/wiki/Key-bindings) | editor keys |
| [Building](https://github.com/regalf/CatShellX/wiki/Building) | flags, targets, troubleshooting |
| [Project layout](https://github.com/regalf/CatShellX/wiki/Project-layout) | source tree |
| [Testing](https://github.com/regalf/CatShellX/wiki/Testing) | PTY test suite (`make test`) |

## License

[GNU General Public License v3.0](LICENSE).
