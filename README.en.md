# wots4-mods

[日本語](README.md)

A ModLoader for the Steam version of Way of the Samurai 4.

It is a `dinput8.dll` placed in the game folder, and loads `mods/<name>/mod.dll` when the game starts.

## Mods

| Mod | What it does |
| --- | --- |
| [unofficial_patch](https://github.com/nagataaaas/wots4-unofficial-patch) | An unofficial patch that fixes crashes and bugs |
| [harakiri](https://github.com/nagataaaas/wots4-harakiri) | Adds a "Harakiri" (one-hit death) difficulty |
| [debugger](https://github.com/nagataaaas/wots4-debugger) | A debugging tool for making Mods |

## For users

Download `wots4-modloader-<version>.zip` from [Releases](../../releases) and install it following [docs/INSTALL.md](docs/INSTALL.md).
Each Mod is distributed from its own repository in the table above.

## For developers

### Building and testing

Requirements: Visual Studio 2022 or later (Desktop development with C++), CMake 3.25 or later

```bash
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe
```

- The output goes to `build/game/`, laid out the same way as the game folder (`dinput8.dll`, `mods/<name>/mod.dll`).
- If you put a Mod's source (`CMakeLists.txt`) in `mods/<name>/`, it is built together. `mods/` is not tracked by Git.

### Installing into the game folder (make)

Copy `local.mk.example` to `local.mk` and set `GAME_DIR` to the game folder (`local.mk` is not tracked by Git).

```bash
make install               # Build the ModLoader and every Mod in mods/, and copy them to GAME_DIR
make install <name> ...    # Build only the Mod in mods/<name>/ and copy it to GAME_DIR/mods/<name>/
make loader                # Build only the ModLoader and copy it to GAME_DIR
```

- `make install <name>` also accepts `loader` as a name (e.g. `make install loader <name>`).

- DLLs are always overwritten. Files such as `config.ini` are not overwritten if they already exist in the game folder (so settings changed in game are kept).
- DLLs that are loaded cannot be overwritten while the game is running.

### Making a Mod

See [docs/MOD_API.md](docs/MOD_API.md).
