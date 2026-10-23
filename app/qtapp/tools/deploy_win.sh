#!/bin/bash
#-------------------------------------------------------------------------------
# deploy_win.sh : Windows 配布フォルダを Desktop に生成・同期する
#
# Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
# Distributed under the BSD 2-clause license (see LICENSE.txt).
#
# WSL から実行する。以下を行う:
#   1. Windows 側の作業コピーを最新にして再ビルド
#   2. windeployqt で必要な DLL 一式を集める
#   3. ライセンス・文書・サンプル設定を同梱
#   4. Desktop の配布フォルダへ rsync
#
# 使い方:  bash app/qtapp/tools/deploy_win.sh
#-------------------------------------------------------------------------------
set -u

REPO="/home/jaxa_snu/MALIB/main-1.3.0"
WIN_SRC="/mnt/c/Users/jaxa_snu/MALIB_qt_win"          # Windows 側の作業コピー
WIN_BUILD="$WIN_SRC/app/qtapp/build_win"
DEST="/mnt/c/Users/jaxa_snu/Desktop/MALIB_RTKNAVI_Qt" # 配布フォルダ
QT="/mnt/c/Qt/6.11.0/mingw_64"
CMAKE="/mnt/c/Qt/Tools/CMake_64/bin/cmake.exe"
CMD="/mnt/c/Windows/System32/cmd.exe"

say() { printf "\n\033[1m== %s\033[0m\n" "$*"; }

#-- 1. Windows 側を最新にして再ビルド -----------------------------------------
say "Windows 側を更新してビルド"
"$CMD" /c "taskkill /IM rtknavi_qt.exe /F" >/dev/null 2>&1   # exe ロック解除
"$CMD" /c "taskkill /IM rtkplot_qt.exe /F" >/dev/null 2>&1
"$CMD" /c "taskkill /IM rtknavi_cli.exe /F" >/dev/null 2>&1
git -C "$WIN_SRC" pull -q origin feature/qt-gui || {
    echo "!! git pull 失敗。Desktop リモートへ push 済みか確認してください"; exit 1; }
"$CMAKE" --build "$(wslpath -w "$WIN_BUILD")" 2>&1 | grep -E "error|Linking CXX executable" | tail -3
for e in rtknavi_qt.exe rtkplot_qt.exe rtknavi_cli.exe; do
    [ -f "$WIN_BUILD/$e" ] || { echo "!! $e が生成されていません"; exit 1; }
done

#-- 2. 一時領域に配布物を組み立てる --------------------------------------------
say "配布物を組み立て"
STAGE=$(mktemp -d)
mkdir -p "$STAGE/bin" "$STAGE/data" "$STAGE/doc" "$STAGE/license"
cp "$WIN_BUILD/rtknavi_qt.exe" "$WIN_BUILD/rtkplot_qt.exe" \
   "$WIN_BUILD/rtknavi_cli.exe" "$STAGE/bin/"

# windeployqt は Windows パスで動かす必要があるため一時的に Desktop 下で実行
TMPWIN="/mnt/c/Users/jaxa_snu/Desktop/.malib_deploy_tmp"
rm -rf "$TMPWIN"; mkdir -p "$TMPWIN"
cp "$WIN_BUILD/rtknavi_qt.exe" "$WIN_BUILD/rtkplot_qt.exe" \
   "$WIN_BUILD/rtknavi_cli.exe" "$TMPWIN/"
"$CMD" /c "cd /d C:\\Users\\jaxa_snu\\Desktop\\.malib_deploy_tmp && \
    $(wslpath -w "$QT/bin/windeployqt.exe") --release --no-translations \
    --no-system-d3d-compiler --no-opengl-sw rtknavi_qt.exe rtkplot_qt.exe \
    rtknavi_cli.exe" >/dev/null 2>&1
cp -r "$TMPWIN"/* "$STAGE/bin/"
rm -rf "$TMPWIN"

#-- 3. 付属ファイル -------------------------------------------------------------
cp "$REPO/LICENSE.txt"                        "$STAGE/license/LICENSE_MALIB.txt"
# LGPLv3 / GPLv3 の条文 (Qt のインストールには平文で含まれないため同梱)
cp "$REPO/app/qtapp/license/LICENSE.LGPLv3.txt" "$STAGE/license/"
cp "$REPO/app/qtapp/license/LICENSE.GPLv3.txt"  "$STAGE/license/"
cp /mnt/c/Qt/Licenses/LICENSE                 "$STAGE/license/Qt_LICENSE_overview.txt" 2>/dev/null
cp /mnt/c/Qt/Licenses/COPYING.txt             "$STAGE/license/Qt_COPYING.txt"          2>/dev/null
cp /mnt/c/Qt/Licenses/Copyright.txt           "$STAGE/license/Qt_Copyright.txt"        2>/dev/null
cp /mnt/c/Qt/Docs/Qt-6.11.0/qtdoc/lgpl.html   "$STAGE/license/Qt_lgpl.html"            2>/dev/null
cp "$REPO/app/qtapp/docs/"*.md                "$STAGE/doc/"                            2>/dev/null
cp "$REPO/app/qtapp/docs/"*.csv               "$STAGE/doc/"                            2>/dev/null
cp "$REPO/app/qtapp/BASELIB_ISSUES.md"        "$STAGE/doc/"                            2>/dev/null
cp "$REPO/bin/rtkrcv_sample.conf"             "$STAGE/data/"                           2>/dev/null
# NOTICE は repo 側 (app/qtapp/NOTICE.txt) を正とする。以前は配布先の既存
# ファイルを維持していたが、repo 側の更新が同期されない事故のもとだった
cp "$REPO/app/qtapp/NOTICE.txt"               "$STAGE/NOTICE.txt"

#-- 4. Desktop へ同期 -----------------------------------------------------------
say "Desktop へ同期: $DEST"
mkdir -p "$DEST"
rsync -a --delete "$STAGE/" "$DEST/"
rm -rf "$STAGE"

say "完了"
printf "  場所  : %s\n" "$DEST"
printf "  サイズ: %s / %s ファイル\n" "$(du -sh "$DEST" | cut -f1)" "$(find "$DEST" -type f | wc -l)"
echo   "  起動  : C:\\Users\\jaxa_snu\\Desktop\\MALIB_RTKNAVI_Qt\\bin\\rtknavi_qt.exe"
echo   "          C:\\Users\\jaxa_snu\\Desktop\\MALIB_RTKNAVI_Qt\\bin\\rtkplot_qt.exe"
