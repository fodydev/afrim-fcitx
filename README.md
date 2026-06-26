# afrim-fcitx5

A native [fcitx5](https://fcitx-im.org/wiki/Fcitx_5) input-method plugin
backed by the [afrim](https://github.com/fodydev/afrim) IME framework.

Three Rust crates do the heavy lifting:

| Crate | Role |
|---|---|
| `afrim-config 0.4.7` | Load and parse TOML config files |
| `afrim-preprocessor 0.6.3` | Turn keystrokes into commit commands |
| `afrim-translator 0.2.3` | Produce candidate translations |

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  fcitx5 (C++)                                                │
│  ┌───────────────────────────────────────────────────────┐   │
│  │  AfrimInputMethodEngine  (AfrimEngine.cpp)            │   │
│  │  • keyEvent()   → afrim_engine_process_key()          │   │
│  │  • preedit      ← afrim_engine_get_input()            │   │
│  │  • candidates   ← afrim_engine_get_candidates()       │   │
│  │  • select cand  → afrim_engine_commit_candidate()     │   │
│  └────────────────────┬──────────────────────────────────┘   │
│                       │  C FFI  (afrim_ffi.h)                │
└───────────────────────┼──────────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────────┐
│  libafrim_fcitx5.a  (Rust)                                   │
│  ┌─────────────────────────────────────────────────────┐     │
│  │  AfrimEngine  (src/engine.rs)                       │     │
│  │  ┌──────────────────┐  ┌──────────────────────────┐ │     │
│  │  │  Preprocessor    │  │  Translator              │ │     │
│  │  │  afrim-preprocessor  afrim-translator           │ │     │
│  │  └──────────────────┘  └──────────────────────────┘ │     │
│  └─────────────────────────────────────────────────────┘     │
│  ┌─────────────────────────────────────────────────────┐     │
│  │  Config  (afrim-config)                             │     │
│  └─────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────┘
```

**Preedit model** — raw keystrokes are never forwarded to the application.
They accumulate in an inline underlined preedit string.  When a transliteration
sequence completes, the preedit is cleared and the translated text is committed.

---

## Requirements

| Tool | Version |
|---|---|
| Rust / Cargo | ≥ 1.75 |
| CMake | ≥ 3.19 |
| fcitx5 | ≥ 5.0 |
| KDE Extra CMake Modules (ECM) | ≥ 1.0 |
| C++ compiler | C++17 |

On Debian/Ubuntu:
```bash
sudo apt install fcitx5-dev extra-cmake-modules cmake build-essential
```

On Arch Linux:
```bash
sudo pacman -S fcitx5 extra-cmake-modules cmake base-devel rust
```

---

## Build

```bash
# Standard build (rhai + strsim enabled)
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install

# Restart fcitx5
fcitx5 -r
```

### Build options

| CMake option | Default | Effect |
|---|---|---|
| `-DAFRIM_INHIBIT=ON` | OFF | Enable the `inhibit` feature for non-Latin scripts (Amharic, etc.). Suppresses intermediate Latin chars from appearing in the application. |
| `-DAFRIM_FEATURES="rhai"` | `rhai,strsim` | Override the extra Cargo feature list. |

Example for an Amharic IME:
```bash
cmake .. -DAFRIM_INHIBIT=ON
```

---

## Configuration

The plugin looks for a TOML config file at:

```
~/.config/fcitx5/afrim/config.toml
```

Override the path with the `$AFRIM_CONFIG` environment variable.

### Minimal config example

```toml
[core]
auto_commit    = false   # auto-commit exact dictionary matches
auto_capitalize = true   # automatically add uppercase variants to [data]

[data]
# Transliteration sequences → output characters
"cc"  = "ç"
"ae"  = "æ"
"n*"  = "ŋ"
"N*"  = "Ŋ"

[translation]
# Word dictionary (key = input sequence, value = translation(s))
jump   = "sauter"
hello  = ["bonjour", "salut"]

[translators]
# Optional Rhai scripted translators
date = "~/.config/fcitx5/afrim/scripts/date.rhai"
```

See the [afrim-config docs](https://docs.rs/afrim-config/0.4.7/afrim_config/)
for the full config reference including nested includes and alias support.

---

## Wire protocol (C ↔ Rust)

### Commands  (`afrim_engine_process_key` / `afrim_engine_commit_candidate`)

Newline-separated lines, one command per line:

| Line | Meaning | C++ action |
|---|---|---|
| `pause` | Start of a bracketed edit block | no-op (preedit model) |
| `resume` | End of a bracketed edit block | no-op |
| `delete` | Delete one char from the app text | no-op (preedit model) |
| `clean_delete` | Internal-only deletion (`inhibit` mode) | no-op |
| `commit:<text>` | Commit `<text>` to the application | `ic->commitString(text)` |

> **Why are `delete` / `clean_delete` no-ops?**  In the preedit model the
> application never sees raw keystrokes, so there is nothing to delete from
> its text buffer.  Preedit updates happen automatically via
> `afrim_engine_get_input()`.

### Candidates  (`afrim_engine_get_candidates`)

One line per `Predicate`, tab-separated:

```
<code>\t<remaining_code>\t<can_commit:0|1>\t<text1>|<text2>|…
```

---

## Project layout

```
afrim-fcitx5/
├── Cargo.toml           Rust staticlib manifest
├── CMakeLists.txt       Builds both Rust and C++
├── addon/
│   └── afrim.conf       fcitx5 addon metadata
├── src/
│   ├── lib.rs           Crate root
│   ├── engine.rs        AfrimEngine (Preprocessor + Translator)
│   └── ffi.rs           #[no_mangle] C exports
└── fcitx5/
    ├── afrim_ffi.h      C header declaring the Rust API
    ├── AfrimEngine.h    C++ class declaration
    └── AfrimEngine.cpp  Full fcitx5 InputMethodEngineV2 implementation
```
