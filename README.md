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

### ゲームのフォルダへ入れる（make）

`local.mk.example` を `local.mk` にコピーし、`GAME_DIR` にゲームのフォルダを書きます（`local.mk` は Git で追跡しません）。

```bash
make install               # ModLoader と mods/ のすべての Mod をビルドして GAME_DIR へコピー
make install <名前> ...    # mods/<名前>/ の Mod だけをビルドして GAME_DIR/mods/<名前>/ へコピー
make loader                # ModLoader だけをビルドして GAME_DIR へコピー
```

- `make install <名前>` の名前には `loader` も書けます（例：`make install loader <名前>`）。

- DLL は毎回上書きします。`config.ini` などは、ゲームのフォルダにすでにあれば上書きしません（ゲーム内で変えた設定を消さないため）。
- ゲームを起動したままだと、読み込まれている DLL は上書きできません。

### Mod の作り方

[docs/MOD_API.md](docs/MOD_API.md) を見てください。

### リリース

`v*` のタグを push すると、GitHub Actions がビルドとテストを行い、`wots4-modloader-<版>.zip`（`dinput8.dll` と README）を Release に添付します。

各 Mod は、それぞれのリポジトリでリリースします（`wots4-mod-<名前>-<版>.zip`）。
