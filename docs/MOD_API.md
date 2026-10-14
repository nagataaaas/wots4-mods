# Mod API (v2)

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
| `size`, `version` | Size of the struct and API version (`wots4::modApiVersion` = 2). Return FALSE if they do not match             |
| `loader`          | The ModLoader's module                                                                                         |
| `module`          | This Mod's module                                                                                              |
| `game`            | The game exe's module (ASLR is enabled, so treat addresses as relative to this)                                |
| `directory`       | Absolute path of this Mod's folder (ends with `\`). Valid only during initialization. Copy it if you need it later |
| `host`            | ModLoader functions (below)                                                                                    |

## ModLoader functions (`HostApi`)

| Function                                                 | Description                                                                              |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `log(owner, message)`                                    | Writes one line to `mod_loader.log`                                                      |
| `claim(owner, address, length)`                          | Reserves a range of bytes to be patched by a hook. Returns FALSE if it overlaps another reservation |
| `releaseClaims(owner)`                                   | Releases all of that Mod's reservations                                                  |
| `addOptionsPage(owner, title, items, count, callbacks)`  | Adds a page to the game's options (below)                                                |
| `onFrame(owner, callback, context)`                      | Calls `callback(context)` once per game frame, after the game's update                   |
| `onOptionsSaved(owner, callback, context)`               | Calls `callback(context)` after the game wrote its options to `S4-SETTINGS.ini`          |
| `findGameText(number)`                                   | Text number `number` of the game's UI texts (GameTextUI) in the game's language (below)  |

`HostApi` only grows at the end, and `size` is `sizeof(wots4::HostApi)` of the ModLoader. A ModLoader older than the SDK may lack the last members, so check only the members the Mod uses: `host->size >= offsetof(wots4::HostApi, findGameText) + sizeof(host->findGameText)`. Checking `host->size >= sizeof(wots4::HostApi)` refuses older ModLoaders even when the Mod does not use the new members.

`findGameText` can be called at any time on the game's thread. It returns `nullptr` while the game has not loaded its texts (they are loaded before the title screen), for a number past the end of the table, and when this version of the game is not supported. The game owns the string; copy it before keeping it. Unlike `wots4::gameText` on options pages, the text is not copied by the ModLoader. Some UI texts are made to follow a name, so they may begin with a space or a particle.

- `claim` only reserves. The Mod does the patching itself (MinHook, etc.).
- Call `claim` and `releaseClaims` from the same thread as initialization (the game's thread).
- `addOptionsPage`, `onFrame` and `onOptionsSaved` are accepted only inside `Wots4ModInitialize`. They return FALSE if the arguments are outside the limits below, or if this version of the game is not supported (the reason is written to `mod_loader.log`). If the Mod then returns FALSE from `Wots4ModInitialize`, the ModLoader forgets what it registered.
- The ModLoader itself hooks the options menus, the frame update and the options save. It reserves those ranges with `claim` before the Mods initialize, so a Mod cannot hook them directly; use these functions instead. Several Mods can use them at the same time.
- Callbacks run on the game's thread. `onFrame` runs every frame: avoid file I/O, memory allocation and locks there.
- This ABI does not pass STL types.

## Options pages

`addOptionsPage` adds a page after the game's five options pages, both in the title screen's options and in the pause menu's 各種設定. The menus scroll, so the number of pages has no limit.

A page shows 8 items at once (`wots4::optionsItemsPerScreen`). A page with more items scrolls, like the game's 入力の再割り当て page: the rows follow the cursor, an arrow above and below shows that more items are there, and a click on an arrow turns 8 rows.

| Limit                           | Value                                    |
| ------------------------------- | ---------------------------------------- |
| Pages                           | No limit                                 |
| Items per page                  | 1 or more                                |
| Choices of a choice item        | 2 to 10 (`wots4::maxOptionChoices`)      |
| Title                           | Up to 63 characters                      |
| Item label, each choice, notice | Up to 127 characters                     |
| Header label                    | Up to 125 characters                     |

Item kinds (`wots4::OptionItem`):

| `kind`                 | `choices`                                                  | Value                       |
| ---------------------- | ---------------------------------------------------------- | --------------------------- |
| `wots4::optionToggle`  | 2: `choices[0]` is "on", `choices[1]` is "off"             | 1 for on, 0 for off         |
| `wots4::optionChoice`  | 2 to 10; the player moves left and right through them      | The index of the choice     |
| `wots4::optionHeader`  | None (`nullptr`, 0)                                        | Not used                    |
| `wots4::optionNotice`  | Made with `wots4::optionNoticeItem` (below)                | Not used                    |

`defaultValue` is the value of the item that the page's 初期設定に戻す button sets, in the same form as the values above. It must be one of the item's values, and 0 for a header.

A header groups the items below it. It shows only its label in brackets, `【地図】` in the Japanese menus and `[Map]` in the English ones, in the same place and style as the other labels.

A notice shows a text in the value column of a row without a label while another item of the page has one of some values, like the game's notice under 言語. `wots4::optionNoticeItem(text, item, {values...})` makes one: it shows `text` (up to 127 characters) while item number `item`, a toggle or a choice item, has one of `values`. The row stays when the text is hidden. The ModLoader decides when the page is shown and each time the player changes an item, from the values the page shows.

Headers and notices still count as items: `values` has an entry for each, which the ModLoader does not use, and the numbers of the items after them include them. The cursor and the pointer pass over them, but the game's input passes over one only, so two of them next to each other are refused, and so is a page of them only. A ModLoader that does not know a kind refuses the page as having an unknown kind.

Every text (title, label, choice) is a `wots4::LocalizedText` with a Japanese and an English text, both required. When a menu is first built, the ModLoader shows the Japanese text if the game's menus are in Japanese, and the English text otherwise (the game has no other menu languages; a language changed in the game takes effect after a restart, as in the game).

Each of the two is either a string, which the ModLoader copies, or a game text: `wots4::gameText(503)` shows text number 503 of the game's UI texts. A game text already follows the game's language, so `wots4::localizedGameText(503)` uses it for both. Game texts are read when a menu is first built, after the game has loaded them.

`wots4::OptionsPageCallbacks`:

- `read(context, values, count)`: the page is about to be shown. Write the current value of each item to `values[0..count-1]`. A choice value outside the choices shows the first choice.
- `changed(context, item, value)`: the player changed item number `item` (in the order given to `addOptionsPage`). The 初期設定に戻す button calls it once for each item that is not at its `defaultValue`, then `read` shows the values again.

The ModLoader does not store the values. To keep them, save them in the Mod's own file, for example from `onOptionsSaved`.

```cpp
// 有り / Enable, 無し / Disable
const wots4::LocalizedText onOff[] = {wots4::localizedGameText(211), wots4::localizedGameText(212)};
const wots4::LocalizedText sizes[] = {{L"小", L"Small"}, {L"中", L"Medium"}, {L"大", L"Large"}};
const wots4::OptionItem items[] = {
    {wots4::optionHeader, {L"地図", L"Map"}},                               // Item 0
    {wots4::optionToggle, {L"地図を表示", L"Show the Map"}, onOff, 2, 1},  // On by default
    {wots4::optionChoice, {L"地図の大きさ", L"Map Size"}, sizes, 3, 1},     // 中 / Medium by default
    // Shown while item 2 (Map Size) is 大 / Large
    wots4::optionNoticeItem({L"大きい地図は画面の端が隠れます", L"A large map hides the screen's edge"}, 2, {2}),
};
const wots4::OptionsPageCallbacks callbacks{nullptr, readValues, valueChanged};
const wots4::LocalizedText title{L"地図", L"Map"};
if (!context->host->addOptionsPage(context->module, &title, items, 4, &callbacks)) return FALSE;
```

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
