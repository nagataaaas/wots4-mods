# Installing Way of the Samurai 4 Mods

This is for the Steam version of Way of the Samurai 4 (`WayOfTheSamurai4.exe`).

## Installing

1. Open the game folder. In your Steam library, right-click the game and choose "Manage" → "Browse local files".
2. Copy the contents of the ModLoader zip (`wots4-modloader-<version>.zip`) and of the zips of the Mods you want (`wots4-mod-<name>-<version>.zip`) into the folder that contains `WayOfTheSamurai4.exe`. The result looks like this:

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

- **To remove some Mods:** delete those Mods' folders in `mods/`.
- **To remove everything:** delete `dinput8.dll` and `mods/`.

Neither the ModLoader nor the Mods modify the game files or save data.
