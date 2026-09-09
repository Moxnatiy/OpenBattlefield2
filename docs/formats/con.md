# The `.con` / `.tweak` language (Refractor 2)

Status: **implemented** — `src/con`, verified over the whole BF2 1.5
corpus (4777 files, 497 195 commands, 0 errors).

## What the language is

It is not a config and not a tree but **a stream of commands** executed by
the engine's interpreter. That is why there is no AST in `src/con`: every
line becomes a `Command` and is handed to a callback at once. State (which
ObjectTemplate is currently "active") is kept by the consumer of the
commands, not by the parser — the same as in Refractor itself.

## Syntax

```
ObjectTemplate.fire.projectileStartPosition 0.06/-0.12/0
```

- The first token is a dotted path: `<target>.<sub-object...>.<method>`.
- Arguments are separated by spaces and tabs, any number of them.
- Lines are CRLF (we trim the `\r`).
- `"text in quotes"` is a single argument. **There are no escape
  sequences**: `"Ingame\Kits\kit.tga"` is a path with backslashes, not
  escaping.
- A vector is numbers separated by slashes: `0.06/-0.12/0`.
- Case never matters, neither in commands nor in paths.

## Directives

| Directive | Behaviour |
|---|---|
| `rem ...` | comment to the end of the line |
| `beginRem` / `endRem` | block comment, **nesting works** |
| `var v_name = value` | a file-local variable |
| `if a == b` / `endIf` | the corpus has only `==` and `!=`, only compared against `v_arg1`; `else` never occurs |
| `include <file>` | execute a file in the same context |
| `run <file> [args]` | the same, but the arguments become `v_arg1`, `v_arg2`, … |

A bare token matching a variable's name is substituted with its value
(`GeometryTemplate.setSubGeometryLodDistance 0 0 v_dist`). Variables are
file-local — there is not a single case in the corpus where a `var` is
expected to "leak" into an include.

## Behaviour taken from the real data

**A missing `include` is not an error.** BF2 1.5 has 159 references to
files that exist in no archive (e.g. `objects/kits/ch/ch_kits.tweak`,
which includes `Kits/ch/ch_kits.con`). The game loads fine, so Refractor
silently ignores them. For us that is a `Severity::Warning`; were it an
error, not a single original mod would load.

**Target frequency** (the whole bf2 corpus, 497k commands) — shows where
work pays off next:

| Target | Commands |
|---|---:|
| `ObjectTemplate` | 433 778 |
| `hudBuilder` | 25 060 |
| `GeometryTemplate` | 14 673 |
| `MaterialManager` | 12 819 |
| `Material` | 2 038 |
| `sound` | 1 970 |
| `swiffHost` | 1 873 |
| `CollisionManager` | 1 318 |
| `gameLogic` | 1 493 |
| `ControlMap` | 1 020 |

87 % of all commands are `ObjectTemplate`. The next step for the language
is not to extend the parser but to build the `ObjectTemplate` registry
(creating templates, components, inheritance through `.activeSafe`).

## Checking

```bash
./build/macos-arm64-debug/tools/con_dump/con_dump "Game Files/mods/bf2" --all
```

Reads the archives in place (unpacking nothing) and mounts them according
to the game's own `ServerArchives.con` / `ClientArchives.con`.
