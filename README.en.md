# wots4-mods

[日本語](README.md)

A ModLoader for the Steam version of Way of the Samurai 4.

Place it in the game folder as `dinput8.dll`, and it loads each Mod's `mods/<name>/mod.dll` when the game starts.

## Mods

| Mod | What it does |
| --- | --- |
| [unofficial_patch](https://github.com/nagataaaas/wots4-unofficial-patch) | Fixes crashes and bugs (an unofficial patch). |
| [harakiri](https://github.com/nagataaaas/wots4-harakiri) | Adds a "Harakiri" (one-hit death) difficulty. |
| [debugger](https://github.com/nagataaaas/wots4-debugger) | Provides debugging tools for making Mods. |

## For users

Download `wots4-modloader-<version>.zip` from [Releases](../../releases) and install it following [docs/INSTALL.md](docs/INSTALL.md).
Get each Mod from its repository, linked in the table above.

## For developers

### Building and testing

Requirements: Visual Studio 2022 or later (Desktop development with C++), CMake 3.25 or later

```bash
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe
```

- The output goes to `build/game/`, laid out the same way as the game folder (`dinput8.dll`, `mods/<name>/mod.dll`).
- Tests labeled `needs_exe` use the game's exe, so the example above skips them.
- Put a Mod's source (a folder with a `CMakeLists.txt`) in `mods/<name>/` and it is built along with the ModLoader. `mods/` is not tracked by Git.

### Installing into the game folder (make)

Copy `local.mk.example` to `local.mk` and set `GAME_DIR` to the game folder, where `WayOfTheSamurai4.exe` is, using forward slashes (`local.mk` is not tracked by Git).

```bash
make install               # Build the ModLoader and every Mod in mods/, and copy them to GAME_DIR
make install <name> ...    # Build only the Mod in mods/<name>/ and copy it to GAME_DIR/mods/<name>/
make loader                # Build only the ModLoader and copy it to GAME_DIR
```

- `make install <name>` also accepts `loader` as a name (e.g. `make install loader <name>`).
- DLLs are always overwritten. Files such as `config.ini` are not overwritten if they already exist in the game folder (so settings changed in game are kept).
- Close the game first; DLLs cannot be overwritten while the game has them loaded.

### Making a Mod

See [docs/MOD_API.md](docs/MOD_API.md).
