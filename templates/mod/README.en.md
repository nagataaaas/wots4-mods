# {Mod name}

[日本語](README.md) | [Changelog](CHANGELOG.en.md)

> [!NOTE]
> This Mod is for the [Steam version of Way of the Samurai 4](https://store.steampowered.com/app/312780/4/?l=english) only.

{Briefly explain what the Mod changes.}

<!-- Optional: add a screenshot or a before-and-after comparison here, with descriptive alt text. -->

## Features

<!-- Describe changes players can experience, rather than implementation details. For a single feature, expand on its behavior here. For multiple features, use a short list or a heading for each feature. -->

- {Describe a change players can experience in the game.}

## Installation

<!-- The installer lists a Mod only after it is added to wots4-mods as a submodule under mods/. Until then, remove the automatic installation part. -->

### Automatic installation (recommended)

1. Close the game.
2. Download and run [WayOfTheSamurai4ModInstaller.exe](https://github.com/nagataaaas/wots4-mods/releases/latest/download/WayOfTheSamurai4ModInstaller.exe).
   - If "Windows protected your PC" appears, click "More info" → "Run anyway".
   - When asked whether to allow it to make changes as administrator, click "Yes" (it writes to the game folder).
3. Follow the screen and choose `{mod-dir}` in the list of Mods. Move with Up and Down, choose with Space and confirm with Enter.

<!-- Keep the sentence about config.ini only if the release includes config.ini. -->

The ModLoader is installed along with it. The same steps update the Mod, and your settings (`config.ini`) are kept.

<details>
<summary>Manual installation</summary>

> [!WARNING]
> Install [wots4-mods (ModLoader)](https://github.com/nagataaaas/wots4-mods) first.

<!-- Replace {mod-zip-name} with the Mod's folder name, replacing underscores with hyphens. The release workflow uses a version tag such as v0.1.0. -->

1. Close the game.
2. Download `wots4-mod-{mod-zip-name}-<version>.zip` from [Releases](../../releases) and extract it.
3. Copy the extracted `mods` folder into the game's installation folder (the folder containing `WayOfTheSamurai4.exe`). If a `mods` folder already exists, merge the contents into it.
4. Confirm that `{game installation folder}/mods/{mod-dir}/mod.dll` exists, then start the game.

<!-- If the release includes config.ini, explain how to preserve existing settings when updating. -->

</details>

## Uninstallation

1. Close the game.
2. Delete the `{game installation folder}/mods/{mod-dir}/` folder.

<!-- If removing the Mod leaves effects behind (for example, saves that need the Mod to load), explain them here. If the folder holds settings, mention that they are deleted too. -->

## Settings

<!-- Remove this entire section if the Mod has no settings. Replace the placeholders with actual sections, keys, defaults, and accepted values. Match the path and restart instructions to the Mod's behavior. -->

Close the game and edit `{game installation folder}/mods/{mod-dir}/config.ini`. Save the file and start the game to apply the settings.

| Section | Key | Default | Description |
| --- | --- | --- | --- |
| `{section}` | `{key}` | `{default}` | {Explain the effect on gameplay and the accepted values or range.} |
