# ベースライブラリ側の既知不具合（業者コード・未修正）

GUI 開発中に発見した `src/` 側の不具合を記録する。**業者コードは原則いじらない**方針のため、
GUI 側で回避し、ここに残して業者への報告材料とする。

---

## 1. `str2enum()` が負値 enum の符号を落とす

| | |
|---|---|
| 発見日 | 2026-07-28 |
| 該当 | `src/options.c` `str2enum()` |
| 影響 | **rtkrcv（CUI）と GUI の両方**。conf 読み込み時のみ（書き出しは正常） |
| 深刻度 | 中（設定が黙って別の値になる。エラーも警告も出ない） |
| GUI 側の回避 | `app/qtapp/rtknavi_qt/ssrfix.h` — conf のテキストから該当行を直接読み直す |

### 症状

`inpstrN-ssr = off` が `sub`(=1) として読み込まれる。`-1` の符号が落ちて `1` になる。

### 原因

```c
/* src/options.c str2enum() */
for (p-=2;'0'<=*p&&*p<='9';p--) ;   /* 数字を逆走査 */
return sscanf(p+1,"%d",val)==1;      /* ← マイナス記号の手前で止まる */
```

`SSROPT = "0:main,1:sub,-1:off"` から `off` を探すと、`-1` の `1` まで戻ったところで
`-` が「数字でない」ためループが止まり、`p+1` が `1` を指す。結果 `-1` → `1`。

### 再現（GUI 不使用・ライブラリ直叩き）

```c
static int ssr[2];
static opt_t o[]={{"inpstr1-ssr",3,(void*)&ssr[0],"0:main,1:sub,-1:off"},{"",0,NULL,""}};
/* conf に inpstr1-ssr=off と書いて loadopts() */
loadopts("ssr_in.conf",o);
printf("%d\n",ssr[0]);   /* → 1  (期待 -1) */
```

実測結果:
```
読み込み: inpstr1-ssr(off) -> 1   (期待 -1=STRSSR_OFF)   ← 化ける
読み込み: inpstr2-ssr(main)-> 0   (期待  0=STRSSR_MAIN)  ← 正常
書き出し: -1 -> "off"              (期待 "off")           ← 正常
```

### 実害

`src/rtksvr.c:332`:
```c
if (svr->ssrtype[index]<0) continue;                     /* off ならスキップするはず */
svr->nav.ssr[svr->ssrtype[index]][i]=svr->rtcm[index].ssr[i];
```
`off` が `sub`(1) に化けるとスキップされず、**そのストリームの SSR が SUB スロットへ書き込まれる**。

**業者同梱の `bin/rtkrcv_sample.conf` がそのまま踏む**:
`inpstr1/2/4-ssr = off` の3本が全て SUB 扱いになり、ローバ観測ストリームの
空 SSR が毎エポック SUB スロットを上書きする。

### 影響範囲の調査結果

コードベース全体で**負値を持つ enum は `SSROPT` のみ**（`options.c` / `rtkrcv.c` を全検索）。
`SSROPT` は `inpstr1〜4-ssr` の4オプションで使われる。いずれも **1.3.0 で新設**。
したがって本不具合の影響は `inpstrN-ssr` に限定される。

### 修正案（業者向け・未適用）

```c
/* 現状 */
for (p-=2;'0'<=*p&&*p<='9';p--) ;
return sscanf(p+1,"%d",val)==1;

/* 案: 符号を含め、範囲外読みも防ぐ */
for (q=p-2;q>comment&&'0'<=*(q-1)&&*(q-1)<='9';q--) ;
if (q>comment&&*(q-1)=='-') q--;
return sscanf(q,"%d",val)==1;
```
（`p==comment` の場合に `*(p-1)` を読む既存の範囲外アクセスも併せて手当てできる）

---

## 2. `opt2str()` に format 4 の分岐が無く、SNR重みしきい値が保存できない

| | |
|---|---|
| 発見日 | 2026-07-28 |
| 該当 | `src/options.c` `opt2str()` |
| 影響 | `saveopts()` を使う側すべて。**設定が黙って消える** |
| 深刻度 | 高（データ消失。警告もエラーも出ない） |
| GUI 側の回避 | `app/qtapp/rtknavi_qt/basefix.h` の `keepRawOpts()` / `restoreRawOpts()` — 元 conf の該当行を退避し、保存後に書き戻す |

### 症状

`pos1-snrwthres_*`（format 4）が `saveopts()` で**空欄として書き出される**。

```
pos1-snrwthres_gps =        ← 値が消えている
pos1-snrwthres_glo =
...
```

業者同梱の `bin/rtkrcv_sample.conf` は全19行・各17個＝**323個の値**を持つが、
一度 `saveopts()` を通すと全部失われる。

### 原因

`str2opt()` には `case 4: return snrwthresopt(...)` があるが、`opt2str()` の
switch は `case 0..3` しかない。したがって読み込みは出来るが書き出しが出来ない。

そもそも `pos1-snrwthres_gps` は**同じキーが信号ごとに複数行**現れる形式
（`pos1-snrwthres_gps= 1C= ...` / `= 2P= ...`）で、1オプション＝1文字列を返す
`opt2str()` の枠組みでは表現できない。

### データ構造（参考）

```c
double snrwthres[CLKNSYS][MAXFREQ][SNRWTHRESNUM];  /* rtklib.h:1402 */
#define SNRWTHRESNUM 17    /* 仰角 5〜85 deg を 5 deg 刻み */
```
`src/satqual.c:374-379` で仰角に応じて線形補間して使用。

### 実測（回避策の検証）

```
退避した snrwthres 行: 19
回避なしの保存結果   : 5 行（空欄）   ← 323個の値が消失
回避ありの保存結果   : 19 行
内容一致             : OK
```

### 修正案（業者向け・未適用）

`opt2str()` は 1 オプション 1 行しか返せないため、単純な `case 4:` の追加では
解決しない。`saveopts()` 側に「1オプションが複数行を出力しうる」経路
（例: `opt2buf()` 相当のコールバック）を設けるのが筋。

---

## 3. `str2enum()` が前方一致で別の選択肢を拾う

| | |
|---|---|
| 発見日 | 2026-07-28（Options ダイアログの自己検査 `--selftest` が検出） |
| 該当 | `src/options.c` `str2enum()` |
| 影響 | **rtkrcv（CUI）と GUI の両方**。conf 読み込み時 |
| 深刻度 | 中（GPS の信号選択が黙って別の組み合わせになる） |
| GUI 側の回避 | `app/qtapp/rtknavi_qt/optdlg.cpp` — コンボの各項目に数値を持たせ、`str2enum()` を通さず `*(int*)opt->var` に直接書く |

### 症状

```
SIGOPT1 = "0:L1C/A-L2P,1:L1C/A-L2C,3:L1C/A-L2"
```
`pos2-siggpsIIR-M = L1C/A-L2`（値 3）が **0（L1C/A-L2P）** として読まれる。

### 原因

```c
if (!(p=strstr(p,str))) break;      /* 部分一致 */
if (*(p-1)!=':') continue;
```
`"L1C/A-L2"` を探すと、先に並んでいる `"0:L1C/A-L2P"` の中で一致してしまう。
直前が `':'` なので妥当な一致と判定され、値 0 を返す。

### 再現（GUI 不使用）

```
conf "L1C/A-L2P " -> 0  (期待 0) OK
conf "L1C/A-L2C " -> 1  (期待 1) OK
conf "L1C/A-L2  " -> 0  (期待 3) <-- 化けた
```

### 影響範囲の調査結果

`options.c` / `rtkrcv.c` の enum 定義 **全40種を総当たり**した結果、
「短いラベルが、先に並ぶ長いラベルの前方一致部分になっている」組み合わせは
**SIGOPT1 / SIGOPT2 の `L1C/A-L2` のみ**。

該当オプション: `pos2-siggpsIIR-M` / `pos2-siggpsIIF` / `pos2-siggpsIIIA`
（いずれも値 `L1C/A-L2` を選んだ場合のみ）。

### 修正案（業者向け・未適用）

一致した候補の直後が区切り（`,` または終端）であることも確認する:

```c
if (!(p=strstr(p,str))) break;
if (p==comment||*(p-1)!=':') continue;
if (p[strlen(str)]!='\0'&&p[strlen(str)]!=',') continue;   /* 追加 */
```

---

## 4. シリアル切断が上位から検知できず、再接続もされない

| | |
|---|---|
| 発見日 | 2026-07-31 |
| 該当 | `src/stream.c` `readserial()` / `stateserial()` |
| 影響 | シリアル接続を使う側すべて（CUI・GUI 共通） |
| 深刻度 | 高（実機運用で受信が止まったまま復帰しない） |
| 検出 | 実機動作試験 B-08（USB シリアルケーブルの抜線と復旧） |
| GUI 側の回避 | `app/qtapp/rtknavi_qt/navimain.cpp` `MainWindow::checkSerialStreams()` — 受信バイト数の停止で切断を判定し、ストリームを開き直す |

### 症状

受信中の USB シリアル受信機のケーブルを抜くと、挿し直しても受信が再開しない。
アプリの Stop → Start が必要になる。

### 原因

`readserial()` は読み出しに失敗しても **`serial->error` を立てずに `0` を返す**だけである。

```c
static int readserial(serial_t *serial, uint8_t *buff, int n, char *msg)
{
    ...
#ifdef WIN32
    if (!ReadFile(serial->dev,buff,n,&nr,NULL)) return 0;   /* error を立てない */
#else
    if ((nr=read(serial->dev,buff,n))<0) return 0;          /* 同上 */
#endif
```

一方 `stateserial()` は `serial->error` だけを見て状態を返す。

```c
static int stateserial(serial_t *serial)
{
    return !serial?0:(serial->error?-1:2);   /* 常に 2 (正常) を返し続ける */
}
```

したがってデバイスが消えても `strstat()` は正常を返し続け、**上位のアプリには
切断を知る手段がない**。加えて `STR_SERIAL` には TCP のような再接続処理
（`tirecon`）が実装されていない。

### 修正案（業者向け・未適用）

読み出し失敗時にエラーを記録する。

```c
#ifdef WIN32
    if (!ReadFile(serial->dev,buff,n,&nr,NULL)) {
        serial->error=1;                                    /* 追加 */
        sprintf(msg,"serial read error (%d)",(int)GetLastError());
        return 0;
    }
#else
    if ((nr=read(serial->dev,buff,n))<0) {
        serial->error=1;                                    /* 追加 */
        sprintf(msg,"serial read error (%d)",errno);
        return 0;
    }
#endif
```

さらに `tcpcli` と同様の再オープン処理を `STR_SERIAL` にも設けることが望ましい。

### GUI 側の回避（実装済み）

ライブラリが状態を教えてくれないため、**受信バイト数が動いているか**で判断する。
`strsum()` で入力バイト数を監視し、`misc-timeout`（既定 10 秒）のあいだ 1 バイトも
増えなければ、`rtksvrlock()` で保護しつつ `strclose()` → `stropen()` で開き直す。

誤検知を避けるため、**一度でもデータが届いたストリームだけ**を対象とする。
一度も受信していないポートは設定誤りであり、開き直しても直らないうえ
ログを埋めるだけであるため対象外とする。

---

## 検出方法

`rtknavi_qt --selftest`（`QT_QPA_PLATFORM=offscreen` で GUI 不要）が
Options ダイアログの全ウィジェットを機械的に動かし、
「意図した 1 個だけが変わり、他が動いていない」ことを全数照合する。
不具合 3 はこれで発見した。回帰確認にも使える。

```
$ QT_QPA_PLATFORM=offscreen ./build/rtknavi_qt --selftest
--- 315 ケース実行 / 0 失敗
```
