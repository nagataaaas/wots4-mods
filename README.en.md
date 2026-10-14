# wots4-mods

[日本語](README.md)

A ModLoader for the Steam version of Way of the Samurai 4.

Place it in the game folder as `dinput8.dll`, and it loads each Mod's `mods/<name>/mod.dll` when the game starts.

## Mods

| Mod | What it does |
| --- | --- |
| [unofficial_patch](https://github.com/nagataaaas/wots4-unofficial-patch) | Fixes crashes and bugs (an unofficial patch). |
| [harakiri](https://github.com/nagataaaas/wots4-harakiri) | Adds a "Harakiri" (one-hit death) difficulty. |
| [post_fx](https://github.com/nagataaaas/wots4-post-fx) | Adds anti-aliasing, sharpening and motion blur. |
| [tweaks](https://github.com/nagataaaas/wots4-tweaks) | Small "nice to have" tweaks, each switched on or off in the options. |
| [fps_uncap](https://github.com/nagataaaas/wots4-fps-uncap) | Removes the 30 FPS cap so the game runs at 60, 90 or 120 FPS. |
| [debugger](https://github.com/nagataaaas/wots4-debugger) | Provides debugging tools for making Mods. |

## Installation

### Automatic installation (recommended)

1. Close the game.
2. Download and run [WayOfTheSamurai4ModInstaller.exe](https://github.com/nagataaaas/wots4-mods/releases/latest/download/WayOfTheSamurai4ModInstaller.exe).
   - If "Windows protected your PC" appears, click "More info" → "Run anyway".
   - When asked whether to allow it to make changes as administrator, click "Yes" (it writes to the game folder).
3. Follow the screen to choose the language, the game folder and the Mods to install. Move with Up and Down, choose Mods with Space and confirm with Enter.

The same steps update the ModLoader and the Mods. The Mods' settings (`config.ini`) are kept.

<details>
<summary>Manual installation</summary>

1. Close the game.
2. Download `wots4-modloader-<version>.zip` from [Releases](../../releases) and extract it.
3. Copy the extracted `dinput8.dll` into the game's installation folder (the folder containing `WayOfTheSamurai4.exe`). To open the installation folder, right-click the game in your Steam library and choose "Manage" → "Browse local files".
4. Start the game. The results of loading the Mods are written to `mod_loader.log` in the installation folder.

Get each Mod from its repository, linked in the table above, and install it following its README. [docs/INSTALL.md](docs/INSTALL.md) shows the overall layout.

</details>

## Uninstallation

1. Close the game.
2. Delete `dinput8.dll` from the game's installation folder.

Once the ModLoader is removed, the Mods in `mods/` are no longer loaded either. If you no longer need the Mods, remove them following their READMEs. You may also delete `mod_loader.log` if you do not need it.

## For developers

### Building and testing

Requirements: Visual Studio 2022 or later (Desktop development with C++), CMake 3.25 or later

The Mods in the table above are submodules in `mods/`. To build them together, fetch the submodules as well.

```bash
git clone --recursive https://github.com/nagataaaas/wots4-mods.git
# If already cloned: git submodule update --init
```

```bash
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe
```

- The output goes to `build/game/`, laid out the same way as the game folder (`dinput8.dll`, `mods/<name>/mod.dll`). The installer is `build/installer/WayOfTheSamurai4ModInstaller.exe`.
- Tests labeled `needs_exe` use the game's exe, so the example above skips them.
- Put a Mod's source (a folder with a `CMakeLists.txt`) in `mods/<name>/` and it is built along with the ModLoader. Mods other than the submodules are not tracked by Git.

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
