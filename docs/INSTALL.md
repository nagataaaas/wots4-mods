# Installing Way of the Samurai 4 Mods

This is for the Steam version of Way of the Samurai 4 (`WayOfTheSamurai4.exe`).

## Installing

The easiest way is `WayOfTheSamurai4ModInstaller.exe` from the ModLoader's GitHub Releases: it finds the game, installs or updates the ModLoader and the Mods you choose, and keeps the Mods' settings. To install by hand instead:

1. Open the game folder. In your Steam library, right-click the game and choose "Manage" → "Browse local files".
2. Close the game, then copy these into the folder that contains `WayOfTheSamurai4.exe`:
   - `dinput8.dll` from the ModLoader zip (`wots4-modloader-<version>.zip`)
   - the `mods` folder from the zip of each Mod you want (`wots4-mod-<name>-<version>.zip`). If a `mods` folder already exists, merge the contents into it.

   The result looks like this:

```
WayOfTheSamurai4.exe
dinput8.dll              ← ModLoader
mods/
  <Mod name>/
    mod.dll
    (config.ini etc., depending on the Mod)
```

3. Start the game. The ModLoader loads each `mods/<name>/mod.dll` in turn.
   - The results of loading are written to `mod_loader.log` in the game folder.

## Uninstalling

Close the game first.

- **To remove some Mods:** delete those Mods' folders in `mods/`.
- **To remove the ModLoader:** delete `dinput8.dll`. The Mods in `mods/` are no longer loaded either.
- **To remove everything:** delete `dinput8.dll` and `mods/`.

Neither the ModLoader nor the Mods modify the game files or save data.
