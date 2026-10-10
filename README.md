# wots4-mods

Steam 版『侍道4』（Way of the Samurai 4）向けの ModLoader です。

ゲームのフォルダに置く `dinput8.dll` で、ゲームが起動すると `mods/<名前>/mod.dll` を読み込みます。

公開している Mod は、まだありません。

## 使う人向け

[Releases](../../releases) から `wots4-modloader-<版>.zip` をダウンロードし、[docs/INSTALL.md](docs/INSTALL.md) の手順で入れてください。
Mod は、それぞれの Mod のリポジトリで配ります。

## 作る人向け

### ビルドとテスト

必要なもの：Visual Studio 2022 以降（C++ によるデスクトップ開発）、CMake 3.25 以降

```bash
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe
```

- 出力は `build/game/` に、ゲームのフォルダと同じ並びで置かれます（`dinput8.dll`、`mods/<名前>/mod.dll`）。
- `mods/<名前>/` に Mod のソース（`CMakeLists.txt`）を置くと、一緒にビルドします。`mods/` は Git で追跡しません。

### Mod の作り方

[docs/MOD_API.md](docs/MOD_API.md) を見てください。

### リリース

`v*` のタグを push すると、GitHub Actions がビルドとテストを行い、`wots4-modloader-<版>.zip`（`dinput8.dll` と README）を Release に添付します。

各 Mod は、それぞれのリポジトリでリリースします（`wots4-mod-<名前>-<版>.zip`）。
