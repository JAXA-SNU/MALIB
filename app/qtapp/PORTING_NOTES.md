# VCL → Qt 移植ノート

Phase 1 スパイク（tcpoptdlg, 2026-07-27）で確立した変換規約。以降のダイアログ移植はこれに従う。

## 実測（tcpoptdlg）

| | VCL | Qt |
|---|---|---|
| 実装 | 181行 (.cpp) + 48行 (.h) + 145行 (.dfm) | 232行 (.cpp) + 50行 (.h)、.ui なし |
| ロジック | パース・履歴・有効化制御 | **そのまま流用**（C文字列処理は無変更で移植） |
| 所要 | — | 約30分/本（この規模なら） |

ビルド実績: malib_core（rtkrcv 相当の C ソース41本、NFREQ=10, -mcmodel=large）が
Linux gcc で警告1件のみでコンパイル、Qt 6.4 と静的リンク、WSLg で表示・操作・
NTRIP 実通信まで確認。

## クラス・型の対応

| VCL | Qt |
|---|---|
| `TForm`（モーダルダイアログ） | `QDialog` |
| `TLabel` | `QLabel` |
| `TEdit` | `QLineEdit` |
| `TEdit` + `PasswordChar='*'` | `QLineEdit` + `setEchoMode(QLineEdit::Password)` |
| `TComboBox` | `QComboBox` + `setEditable(true)` + `setInsertPolicy(NoInsert)` |
| `TButton` | `QPushButton` |
| `TCheckBox` | `QCheckBox` |
| `AnsiString` | `QString` |
| `AnsiString.c_str()` | `qPrintable(qs)`（読み取りの一時利用のみ）|
| `s.sprintf("%s:%s",a,b)` | `QString("%1:%2").arg(a,b)` |

## イベント・挙動の対応

| VCL | Qt |
|---|---|
| `OnShow` (FormShow) | `showEvent()` override（`event->spontaneous()` は無視すること） |
| `OnClick` + `__published` | `connect(btn, &QPushButton::clicked, this, &Cls::slot)` |
| `ModalResult=mrOk/mrCancel` | `accept()` / `reject()`（Cancel は `&QDialog::reject` に直結でよい） |
| `ShowModal()!=mrOk` | `exec()!=QDialog::Accepted` |
| `->Caption` / `->Text` | `setText()` / `text()` |
| `TComboBox->Text` | `currentText()` ※`text()` ではない |
| `TComboBox->Items->Add` | `addItem()` |
| `->Enabled` / `->Visible` | `setEnabled()` / `setVisible()` |
| `.dfm` の絶対座標 | `QGridLayout` + `QHBoxLayout`（座標は捨てて構造だけ移す） |
| `CreateProcess()` | `QProcess::startDetached()` |
| ブロッキングループ中の UI 凍結 | ループ内に `QApplication::processEvents()`（VCL からの意図的改善） |

## 移植スタイル規約

- **公開メンバは VCL と同名のまま**（`Opt`, `Path`, `History[]` 等）。呼び出し側
  (navimain 等) の移植を機械的にするため。Qt 流の setter/getter への整理は
  全アプリ移植完了後に検討。
- rtklib C API はそのまま呼ぶ。`rtklib.h` に `extern "C"` ガード有り。
- **コンパイル定義 (NFREQ 等) は malib_core の PUBLIC 定義から伝播させる**。
  GUI 側で個別定義しない（構造体サイズ不一致で壊れるため）。
- レイアウトは .ui を使わず C++ コードで構築（差分レビューしやすい）。
  naviopt のような大規模フォームで .ui を使うかは Phase 3 で判断。

## 既知の注意点

- `showEvent` は最小化復帰でも来る → `event->spontaneous()` で弾く
- `QComboBox::clear()` は editText も消す → クリア後に `setCurrentText()` し直す
- `qPrintable()` の戻りは一時オブジェクト。保持するなら `toLocal8Bit()` で受ける
- WSLg の libEGL 警告はソフトウェア描画フォールバック。実害なし
