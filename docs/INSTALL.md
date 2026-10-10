# 侍道4 Mod の入れ方

対象は Steam 版の『侍道4』（`WayOfTheSamurai4.exe`）です。

## 入れ方

1. ゲームのフォルダを開きます。Steam のライブラリでゲームを右クリックし、「管理」→「ローカルファイルを閲覧」を選ぶと開けます。
2. ModLoader の zip（`wots4-modloader-<版>.zip`）と、使いたい Mod の zip（`wots4-mod-<名前>-<版>.zip`）の中身を、`WayOfTheSamurai4.exe` と同じフォルダへコピーします。すると、次の並びになります。

```
WayOfTheSamurai4.exe
dinput8.dll              ← ModLoader
mods/
  <Mod の名前>/
    mod.dll
    （Mod によっては config.ini など）
```

3. ゲームを起動します。ModLoader は `mods/<名前>/mod.dll` を順に読み込みます。
   - 読み込んだ結果は、ゲームのフォルダの `mod_loader.log` に書き出されます。

## 外し方

- **一部の Mod だけ外す:** `mods/` の中の、その Mod のフォルダを消します。
- **すべて外す:** `dinput8.dll` と `mods/` を消します。

ModLoader も Mod も、ゲームのファイルとセーブデータは書き換えません。
