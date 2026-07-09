# afrim-fcitx5

A native [fcitx5](https://fcitx-im.org/wiki/Fcitx_5) input-method plugin
backed by the [afrim](https://github.com/fodydev/afrim) IME framework.

---

### Requirements

| Tool | Version |
|---|---|
| Rust / Cargo | >= 1.75 |
| CMake | >= 3.22 |
| corrosion | >= 0.6 |
| fcitx5 | >= 5.0 |
| KDE Extra CMake Modules (ECM) | >= 1.0 |
| C++ compiler | C++20 |

On Debian/Ubuntu:
```bash
sudo apt install fcitx5-dev extra-cmake-modules cmake build-essential corrosion
```

On Arch Linux:
```bash
sudo pacman -S fcitx5 extra-cmake-modules cmake base-devel rust corrosion
```

---

### Build

```bash
cmake -Bbuild
cd build
sudo make install

# Restart fcitx5
fcitx5 -r
```

#### Build options

| CMake option | Default | Effect |
|---|---|---|
| `-DAFRIM_FEATURES="rhai"` | `rhai,strsim` | Override the extra Cargo feature list. |

Example for an Amharic IME:
```bash
cmake -Bbuild -DAFRIM_FEATURES="rhai,strsim"
```

---

### Configuration

See the [afrim-config docs](https://fodydev.github.io/afrim-man/configuration/index.html)
for the manual reference.

Additionally, you can download ready to use dataset at [afrim-data](https://github.com/fodydev/afrim-data)

### License

This code is licensed under the [LGPL-2.1](LICENSE).
