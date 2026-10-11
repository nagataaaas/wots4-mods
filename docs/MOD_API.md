# Mod API (v1)

The contract between the ModLoader (`dinput8.dll`) and Mods (`mod.dll`). The header is [include/wots4/mod_api.h](../include/wots4/mod_api.h).

## Loading sequence

1. The game loads `dinput8.dll`. The ModLoader does nothing at this point (inside DllMain).
2. When the game first calls `DirectInput8Create`, the ModLoader loads Mods as follows.
   1. It looks for `mods/*/mod.dll` in the ModLoader's folder. Only one level is searched, and the results are sorted by folder name (case-insensitive).
   2. It loads all DLLs it found.
   3. Then it calls `Wots4ModInitialize` for each one in turn.
3. Later calls to `DirectInput8Create` are passed straight to the original Windows `dinput8.dll`.

- Loaded Mods are not unloaded until the process exits.
- If a Mod fails to load, the other Mods are still loaded. However, recovery from exceptions raised inside a Mod is not guaranteed.
- The ModLoader does not load DLLs other than `mod.dll` (such as `helper.dll`). Mods that need them must load them themselves.
- The results are written to `mod_loader.log` in the game folder.

| Result       | Meaning                                                        |
| ------------ | -------------------------------------------------------------- |
| accepted     | `Wots4ModInitialize` returned TRUE                             |
| refused      | `Wots4ModInitialize` returned FALSE                            |
| missingEntry | `mod.dll` has no `Wots4ModInitialize`                          |
| failed       | The DLL could not be loaded (the Windows error code is logged) |
| duplicate    | The same module had already been initialized                   |

An example of `mod_loader.log` (inside `[ ]` is either ModLoader or the folder name of the Mod that called `log`). The log is written in English only.

```
2026-10-11 20:02:43.360 [ModLoader] loading Mods from C:\...\Way of the Samurai 4\mods\
2026-10-11 20:02:43.364 [example] example mod initialized
2026-10-11 20:02:43.372 [ModLoader] mods\example\mod.dll: accepted
2026-10-11 20:02:43.373 [ModLoader] mods\broken\mod.dll: failed (could not load the DLL; Windows error 193: %1 is not a valid Win32 application.)
```

Please write the messages your Mod logs with `log` in English as well.

## Entry point

```cpp
extern "C" BOOL WINAPI Wots4ModInitialize(const wots4::ModContext* context);
```

Export a stdcall function named `Wots4ModInitialize`. A `.def` file makes this easy.

```
LIBRARY mod
EXPORTS
    Wots4ModInitialize=_ModInitialize@4
```

Contents of `ModContext`:

| Member            | Description                                                                                                    |
| ----------------- | -------------------------------------------------------------------------------------------------------------- |
| `size`, `version` | Size of the struct and API version (`wots4::modApiVersion` = 1). Return FALSE if they do not match             |
| `loader`          | The ModLoader's module                                                                                         |
| `module`          | This Mod's module                                                                                              |
| `game`            | The game exe's module (ASLR is enabled, so treat addresses as relative to this)                                |
| `directory`       | Absolute path of this Mod's folder (ends with `\`). Valid only during initialization. Copy it if you need it later |
| `host`            | ModLoader functions (below)                                                                                    |

## ModLoader functions (`HostApi`)

| Function                        | Description                                                                              |
| ------------------------------- | ---------------------------------------------------------------------------------------- |
| `log(owner, message)`           | Writes one line to `mod_loader.log`                                                      |
| `claim(owner, address, length)` | Reserves a range of bytes to be patched by a hook. Returns FALSE if it overlaps another Mod's reservation |
| `releaseClaims(owner)`          | Releases all of that Mod's reservations                                                  |

- `claim` only reserves. The Mod does the patching itself (MinHook, etc.).
- Call `claim` and `releaseClaims` from the same thread as initialization (the game's thread).
- This ABI does not pass STL types.

## Adding a Mod

The template is in [templates/mod/](../templates/mod/) (`CMakeLists.txt`, the entry point `src/mod.cpp` and `src/mod.def`, and the CI and Release workflows).

1. Put a `CMakeLists.txt` like the following in `mods/<name>/`. `<name>` is the folder name in the game folder.

   ```cmake
   wots4_add_mod(<name>
       SOURCES src/mod.cpp
       DEF src/mod.def
       LIBS wots4_minhook          # if needed; call wots4_use_minhook() first
       FILES config.ini            # files distributed in the same folder as mod.dll
       LICENSES docs/licenses/...  # files distributed in docs/licenses/
   )
   ```

   - Add `NO_RELEASE` to Mods that are not distributed yet.
   - When building standalone, pass `-DWOTS4_SDK_DIR=<path to wots4-mods>` (see the top of the template's `CMakeLists.txt`).
2. At startup, a Mod should check the exe version and the original bytes at the places it hooks. If they do not match, change nothing and return FALSE.
3. Add tests with `wots4_add_probe()`. Mark tests that need the game exe with `NEEDS_EXE`.
   - For tests that load through the ModLoader, build the ModLoader with `wots4_add_loader()` and use `$<TARGET_FILE:wots4_loader>`.
4. Each Mod is released from its own repository. Copy the template's `.github/workflows/` and change these two:
   - `MOD_NAME`: the Mod's name
   - `WOTS4_SDK_REF`: the version of wots4-mods used for the build
   - Pushing a `v*` tag attaches a zip containing only `mods/<name>/` to the Release.
