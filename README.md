# wots4-mods

[English](README.en.md)

Steam 版『侍道4』（Way of the Samurai 4）向けの ModLoader です。

`dinput8.dll` としてゲームのフォルダに置くと、ゲームの起動時に `mods/<名前>/mod.dll`（各 Mod）を読み込みます。

## Mod 一覧

| Mod | 内容 |
| --- | --- |
| [unofficial_patch](https://github.com/nagataaaas/wots4-unofficial-patch) | クラッシュやバグを直す非公式パッチです。 |
| [harakiri](https://github.com/nagataaaas/wots4-harakiri) | 難易度「一撃死」を追加します。 |
| [debugger](https://github.com/nagataaaas/wots4-debugger) | Mod を作るときに使うデバッグツールです。 |

## 使う人向け

[Releases](../../releases) から `wots4-modloader-<バージョン>.zip` をダウンロードし、[docs/INSTALL.md](docs/INSTALL.md) の手順で入れてください。
Mod は、上の表から各 Mod のリポジトリを開いて入手してください。

## 作る人向け

### ビルドとテスト

必要なもの：Visual Studio 2022 以降（C++ によるデスクトップ開発）、CMake 3.25 以降

上の表の Mod は `mods/` にサブモジュールとして入っています。一緒にビルドするときは、サブモジュールも取得してください。

```bash
git clone --recursive https://github.com/nagataaaas/wots4-mods.git
# クローン済みなら：git submodule update --init
```

```bash
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe
```

- 出力は `build/game/` に、ゲームのフォルダと同じ並びで置かれます（`dinput8.dll`、`mods/<名前>/mod.dll`）。
- `needs_exe` のテストはゲームの exe を使うので、上の例では除いています。
- Mod のソース（`CMakeLists.txt` を含むフォルダ）を `mods/<名前>/` に置くと、ModLoader と一緒にビルドされます。サブモジュール以外の Mod は Git で追跡しません。

### ゲームのフォルダへ入れる（make）

`local.mk.example` を `local.mk` にコピーし、`GAME_DIR` にゲームのフォルダ（`WayOfTheSamurai4.exe` のある場所。区切りは `/`）を書きます（`local.mk` は Git で追跡しません）。

```bash
make install               # ModLoader と mods/ のすべての Mod をビルドして GAME_DIR へコピー
make install <名前> ...    # mods/<名前>/ の Mod だけをビルドして GAME_DIR/mods/<名前>/ へコピー
make loader                # ModLoader だけをビルドして GAME_DIR へコピー
```

- `make install <名前>` の名前には `loader` も書けます（例：`make install loader <名前>`）。
- DLL は毎回上書きします。`config.ini` などは、ゲームのフォルダにすでにあれば上書きしません（ゲーム内で変えた設定を消さないため）。
- ゲームを起動している間は DLL を上書きできないので、ゲームを終了してから実行してください。

### Mod の作り方

[docs/MOD_API.md](docs/MOD_API.md) を見てください。
