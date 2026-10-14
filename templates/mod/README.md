# {Mod の名前}

[English](README.en.md)

> [!NOTE]
> この Mod は [Steam 版『侍道4』](https://store.steampowered.com/app/312780/4/?l=japanese)専用です。

{何をどう変える Mod なのかを、短く説明してください。}

<!-- Optional: add a screenshot or a before-and-after comparison here, with descriptive alt text. -->

## 機能

<!-- Describe changes players can experience, rather than implementation details. For a single feature, expand on its behavior here. For multiple features, use a short list or a heading for each feature. -->

- {ゲーム内で体験できる変化を説明してください。}

## インストール

> [!WARNING]
> 事前に [wots4-mods（ModLoader）](https://github.com/nagataaaas/wots4-mods) を導入してください。

<!-- Replace {mod-zip-name} with the Mod's folder name, replacing underscores with hyphens. The release workflow uses a version tag such as v0.1.0. -->

1. ゲームを終了してください。
2. [Releases](../../releases) から `wots4-mod-{mod-zip-name}-<バージョン>.zip` をダウンロードし、解凍してください。
3. 解凍して得られた `mods` フォルダを、侍道4のインストールフォルダ（`WayOfTheSamurai4.exe` がある場所）へコピーしてください。すでに `mods` フォルダがある場合は、その中にまとめてください。
4. `{侍道4インストールフォルダ}/mods/{mod-dir}/mod.dll` があることを確認し、ゲームを起動してください。

<!-- If the release includes config.ini, explain how to preserve existing settings when updating. -->

## アンインストール

1. ゲームを終了してください。
2. `{侍道4インストールフォルダ}/mods/{mod-dir}/` フォルダを削除してください。

<!-- If removing the Mod leaves effects behind (for example, saves that need the Mod to load), explain them here. If the folder holds settings, mention that they are deleted too. -->

## 設定

<!-- Remove this entire section if the Mod has no settings. Replace the placeholders with actual sections, keys, defaults, and accepted values. Match the path and restart instructions to the Mod's behavior. -->

ゲームを終了し、`{侍道4インストールフォルダ}/mods/{mod-dir}/config.ini` を編集してください。保存してからゲームを起動すると、設定が反映されます。

| セクション | 項目 | 既定値 | 説明 |
| --- | --- | --- | --- |
| `{section}` | `{key}` | `{default}` | {プレイへの影響と、指定できる値や範囲を説明してください。} |
