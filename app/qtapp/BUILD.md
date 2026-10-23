# RTKNAVI-Qt / RTKPLOT-Qt のビルド手順

Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.

このフォルダ（`main-1.3.0`）から Qt 版 GUI を
ビルドする手順。**Ubuntu と Windows のどちらでも同じソースからビルドできる。**

---

## 0. 何ができるか

| 実行ファイル | 内容 |
|---|---|
| `rtknavi_qt` | リアルタイム測位 GUI（rtkrcv の GUI 版） |
| `rtkplot_qt` | 解の描画 GUI |
| `rtknavi_cli` | 設定点検用のコマンドライン版（`--selftest` / `--coverage`） |

演算部（`src/`）は**静的リンク**され実行ファイルに取り込まれる。
Qt は**動的リンク**で、実行時に DLL / .so が必要（理由は
`docs/OSS公開_権利構成と貢献率.md` 第 2.2 節）。

---

## 1. Ubuntu でのビルド

### 1.1 必要なもの

```sh
sudo apt install build-essential cmake ninja-build \
                 qt6-base-dev libgl1-mesa-dev
```

**Qt 6.4 以降**が必要（開発時の実績は Qt 6.4.2 / GCC 13 / Ubuntu 24.04）。

### 1.2 ビルド

```sh
cd app/qtapp
cmake -B build -G Ninja
cmake --build build
```

`build/rtknavi_qt` `build/rtkplot_qt` `build/rtknavi_cli` ができる。

### 1.3 動作確認

```sh
cd app/qtapp
# 画面を出さずに設定の全数検査（数秒）
QT_QPA_PLATFORM=offscreen ./build/rtknavi_cli --selftest
#   --- 305 ケース実行 / 0 失敗

# GUI を起動（WSL の場合は WSLg が必要）
./build/rtknavi_qt
```

> **WSL で使う場合**：WSLg があれば GUI はそのまま表示される。ただし
> **WSL から Windows の COM ポートは見えない**ため、シリアル接続の受信機は
> 扱えない。実機を繋ぐ試験は Windows 側で行うこと。

---

## 2. Windows でのビルド

### 2.1 必要なもの

Qt のインストーラ（<https://www.qt.io/download-qt-installer>）で下記を入れる。
オープンソース版で構わない。

| 選択項目 | 例 |
|---|---|
| Qt 本体 | **Qt 6.11.0** の `MinGW 13.1.0 64-bit` |
| ツール | **MinGW 13.1.0 64-bit**（コンパイラ） |
| ツール | **CMake**、**Ninja** |

開発時の実績は Qt 6.11.0 / MinGW 13.1.0。既定のインストール先は `C:\Qt`。

> **Visual Studio (MSVC) ではビルドしていない。** 演算部は C99 で書かれており、
> MSVC では通らない箇所がある。**MinGW を使うこと。**

### 2.2 ビルド

「Qt 6.11.0 (MinGW 13.1.0 64-bit)」のコマンドプロンプトを開き、

```bat
cd <このフォルダ>\app\qtapp
cmake -B build_win -G Ninja
cmake --build build_win
```

`build_win\rtknavi_qt.exe` などができる。

### 2.3 実行に必要な DLL を集める

ビルドしただけでは Qt の DLL が無いため、そのままでは他の PC で動かない。

```bat
cd build_win
C:\Qt\6.11.0\mingw_64\bin\windeployqt.exe --release --no-translations ^
    rtknavi_qt.exe rtkplot_qt.exe rtknavi_cli.exe
```

これで同じフォルダに `Qt6Core.dll` などと `platforms\` 等のサブフォルダが
展開される。**このフォルダごと配れば、Qt を入れていない PC でも動く。**

### 2.4 配布フォルダを自動生成する（WSL から）

WSL 側から Windows のビルドと配布フォルダ作成までを一括で行うスクリプトがある。

```sh
bash app/qtapp/tools/deploy_win.sh
```

**このスクリプトは Windows 側の作業コピーで `git pull` してからビルドする。**
未コミットの変更は Windows のバイナリに入らないので注意（スクリプト冒頭の
パス定数を環境に合わせて書き換えて使うこと）。

---

## 3. 移植にあたっての注意点（ハマりどころ）

過去に踏んだものを記録しておく。ビルドが通らない・挙動が違うときの参考に。

| 事象 | 原因と対処 |
|---|---|
| Windows で `relocation truncated to fit` | `rtksvr_t` が 2.3 GB あり、静的変数にすると PE の 2 GB 制限を超える。**ヒープに確保する**（`calloc`）。`nav_t` も 134 MB あり、スタックに置くと即クラッシュする |
| `-mcmodel=large` が MinGW で通らない | ELF 専用のオプション。`if (NOT WIN32)` で囲ってある |
| MinGW で `WIN32` が未定義 | `rtklib.h` は `WIN32`（アンダースコアなし）を見るが MinGW は自動定義しない。`target_compile_definitions(malib_core PUBLIC WIN32)` で明示している |
| 構造体サイズの不一致でクラッシュ | `NFREQ=10` `NEXOBS=5` 等はライブラリと GUI で**完全に一致**させる必要がある。CMake で `PUBLIC` 指定にしてある |
| ソケットが全て失敗する（Windows） | `strinitcom()`（`WSAStartup`）を `main()` で呼ぶ必要がある。呼ばないと `socket error (10093)` で NTRIP/TCP が全滅する |
| 日本語パスが化ける（Windows） | `char` 配列との往復は**ローカル 8bit で統一**すること。`appcmn_qt/textconv.h` の `fromLib()` / `toLib()` を使う。Qt 6 の暗黙変換は UTF-8 固定なので混ぜると壊れる |
| PowerShell でリダイレクトが空になる | GUI サブシステムの実行ファイルはシェルが終了を待たない。点検出力が要るときは**コンソール版の `rtknavi_cli`** を使う |

---

## 4. `src/`（演算部）を差し替えたとき

**ファイルを置き換えただけでは実行ファイルは変わらない。**静的リンクのため
必ず再ビルドが必要。手順は `docs/PPP_C_SWAP_20260803.md` 第 6 章を参照。

要点だけ再掲する。

1. `src/rtklib.h` と `src/options.c` が変わっていないか確認する
2. 再ビルドする
3. `rtknavi_cli --selftest` を流す（305 ケース 0 失敗）
4. **`rtkrcv` も建て直す**（`app/consapp/rtkrcv/gcc` で `make clean && make`）
5. CUI と GUI を並走させ `tools/cmp_pos.py` で突き合わせる

---

## 5. CUI 版（rtkrcv / rnx2rtkp）のビルド

Qt は不要。従来どおり makefile でビルドする。

```sh
cd app/consapp/rtkrcv/gcc && make
cd app/consapp/rnx2rtkp/gcc && make
```

---

## 6. 文書の案内

| ファイル | 内容 |
|---|---|
| `PORTING_NOTES.md` | 移植の設計方針と経緯 |
| `BASELIB_ISSUES.md` | 演算部（業者提供）の既知不具合 4 件と GUI 側の回避 |
| `tools/README.md` | 検証ツール（`cmp_pos.py` 等）の使い方 |
