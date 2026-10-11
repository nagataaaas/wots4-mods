# Mod API（v1）

ModLoader（`dinput8.dll`）と Mod（`mod.dll`）のあいだの決まりです。ヘッダーは [include/wots4/mod_api.h](../include/wots4/mod_api.h) です。

## 読み込みの流れ

1. ゲームが `dinput8.dll` を読み込みます。ModLoader は、この時点（DllMain の中）では何もしません。
2. ゲームが最初に `DirectInput8Create` を呼んだとき、ModLoader は次のように Mod を読み込みます。
   1. ModLoader と同じフォルダにある `mods/*/mod.dll` を探します。探すのは1階層だけで、フォルダ名の順（大文字と小文字は区別しない）に並べます。
   2. 見つけた DLL をすべて読み込みます。
   3. その後で、`Wots4ModInitialize` を1つずつ呼びます。
3. その後の `DirectInput8Create` は、Windows 本来の `dinput8.dll` へそのまま渡します。

- 読み込んだ Mod は、プロセスが終わるまで外しません。
- Mod の読み込みにが失敗しても、ほかの Mod は続けて読み込みます。ただし、Mod の中で起きた例外から回復できるとは限りません。
- `mod.dll` 以外の DLL（`helper.dll` など）を ModLoader は読み込みません。必要な Mod が、自分で読み込んでください。
- 結果は、ゲームのフォルダの `mod_loader.log` に書き出されます。

| 結果         | 意味                                                     |
| ------------ | -------------------------------------------------------- |
| accepted     | `Wots4ModInitialize` が TRUE を返した                    |
| refused      | `Wots4ModInitialize` が FALSE を返した                   |
| missingEntry | `mod.dll` に `Wots4ModInitialize` がない                 |
| failed       | DLL を読み込めなかった（Windows のエラー番号を記録する） |
| duplicate    | 同じモジュールがすでに初期化されていた                   |

`mod_loader.log` の例（`[ ]` の中は、ModLoader か、`log` を呼んだ Mod のフォルダ名）。ログは英語だけで書きます。

```
2026-10-11 20:02:43.360 [ModLoader] loading Mods from C:\...\Way of the Samurai 4\mods\
2026-10-11 20:02:43.364 [example] example mod initialized
2026-10-11 20:02:43.372 [ModLoader] mods\example\mod.dll: accepted
2026-10-11 20:02:43.373 [ModLoader] mods\broken\mod.dll: failed (could not load the DLL; Windows error 193: %1 is not a valid Win32 application.)
```

Mod から `log` で書く文も、英語にしてください。

## 入口

```cpp
extern "C" BOOL WINAPI Wots4ModInitialize(const wots4::ModContext* context);
```

stdcall の関数を、`Wots4ModInitialize` という名前で export します。`.def` ファイルを使うと簡単です。

```
LIBRARY mod
EXPORTS
    Wots4ModInitialize=_ModInitialize@4
```

`ModContext` の中身：

| メンバー          | 内容                                                                                                           |
| ----------------- | -------------------------------------------------------------------------------------------------------------- |
| `size`, `version` | 構造体の大きさと API の版（`wots4::modApiVersion` = 1）。合わなければ FALSE を返してください                   |
| `loader`          | ModLoader のモジュール                                                                                         |
| `module`          | この Mod のモジュール                                                                                          |
| `game`            | ゲームの exe のモジュール（ASLR が有効なので、アドレスはここからの相対で扱います）                             |
| `directory`       | この Mod のフォルダの絶対パス（末尾に `\` が付きます）。初期化の間だけ有効です。後で使うならコピーしてください |
| `host`            | ModLoader の機能（下記）                                                                                       |

## ModLoader の機能（`HostApi`）

| 関数                            | 内容                                                                                     |
| ------------------------------- | ---------------------------------------------------------------------------------------- |
| `log(owner, message)`           | `mod_loader.log` に1行書きます                                                           |
| `claim(owner, address, length)` | フックで書き換えるバイトの範囲を予約します。ほかの Mod の予約と重なると FALSE を返します |
| `releaseClaims(owner)`          | その Mod の予約をすべて外します                                                          |

- `claim` は予約するだけです。書き換えは Mod が自分で行います（MinHook など）。
- `claim` と `releaseClaims` は、初期化と同じスレッド（ゲームのスレッド）から呼んでください。
- この ABI では STL の型を渡しません。

## Mod を足す

雛形は [templates/mod/](../templates/mod/) にあります（`CMakeLists.txt`、入口の `src/mod.cpp`・`src/mod.def`、CI と Release のワークフロー）。

1. `mods/<名前>/` に、次のような `CMakeLists.txt` を置きます。`<名前>` はゲームのフォルダでのフォルダ名です。

   ```cmake
   wots4_add_mod(<名前>
       SOURCES src/mod.cpp
       DEF src/mod.def
       LIBS wots4_minhook          # 必要なら。先に wots4_use_minhook() を呼ぶ
       FILES config.ini            # mod.dll と同じフォルダへ配布するファイル
       LICENSES docs/licenses/...  # docs/licenses/ へ配布するファイル
   )
   ```

   - まだ配布しない Mod には、`NO_RELEASE` を付けます。
   - 単体でビルドするときは、`-DWOTS4_SDK_DIR=<wots4-mods のパス>` を渡します（雛形の `CMakeLists.txt` の先頭を見てください）。
2. Mod は、起動時に exe の版とフックする場所の元のバイト列を照合してください。合わなければ何も変えずに FALSE を返します。
3. テストは `wots4_add_probe()` で足します。ゲームの exe が必要なテストには `NEEDS_EXE` を付けます。
   - ModLoader 経由で読み込むテストには、`wots4_add_loader()` で ModLoader を作り、`$<TARGET_FILE:wots4_loader>` を使います。
4. Mod は、自分のリポジトリでリリースします。雛形の `.github/workflows/` を写し、次の2つを書き換えてください。
   - `MOD_NAME`：Mod の名前
   - `WOTS4_SDK_REF`：ビルドに使う wots4-mods の版
   - `v*` のタグを push すると、`mods/<名前>/` だけを入れた zip が Release に添付されます。
