#!/usr/bin/env python3
"""
cmp_pos.py : 2つの .pos ファイルを突き合わせ、差分の分布を出す

用途: CUI rtkrcv と GUI、あるいは Linux ビルドと Windows ビルドの
      測位結果が同一 conf・同一入力で一致するかの検証。

usage: cmp_pos.py A.pos B.pos [--tol-mm 1.0]
exit  : 0=許容内 / 1=許容超え / 2=エポック不一致
"""
import math
import sys


def load(path):
    sols = {}
    with open(path, errors="ignore") as fp:
        for line in fp:
            if line.startswith("%") or not line.strip():
                continue
            p = line.split()
            if len(p) < 7:
                continue
            key = p[0] + " " + p[1]
            sols[key] = dict(lat=float(p[2]), lon=float(p[3]), hgt=float(p[4]),
                             q=int(p[5]), ns=int(p[6]))
    return sols


def pct(vals, q):
    if not vals:
        return 0.0
    s = sorted(vals)
    i = min(int(len(s) * q), len(s) - 1)
    return s[i]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    tol_mm = 1.0
    if "--tol-mm" in sys.argv:
        tol_mm = float(sys.argv[sys.argv.index("--tol-mm") + 1])

    a, b = load(sys.argv[1]), load(sys.argv[2])
    common = sorted(set(a) & set(b))
    print(f"A: {len(a)} 解  B: {len(b)} 解  共通: {len(common)}")
    print(f"Aのみ: {len(set(a) - set(b))}  Bのみ: {len(set(b) - set(a))}")
    if not common:
        print("共通エポックなし")
        return 2

    dists, dh_all = [], []
    qdiff = nsdiff = 0
    worst = (0.0, None)
    for t in common:
        x, y = a[t], b[t]
        dlat = (x["lat"] - y["lat"]) * 111320.0
        dlon = (x["lon"] - y["lon"]) * 111320.0 * math.cos(math.radians(x["lat"]))
        dh = x["hgt"] - y["hgt"]
        d = math.sqrt(dlat ** 2 + dlon ** 2 + dh ** 2)
        dists.append(d)
        dh_all.append(abs(dh))
        if d > worst[0]:
            worst = (d, t)
        qdiff += x["q"] != y["q"]
        nsdiff += x["ns"] != y["ns"]

    mm = [d * 1000 for d in dists]
    exact = sum(1 for d in mm if d == 0.0)
    print()
    print("3D 位置差の分布 (mm)")
    print(f"  完全一致 : {exact}/{len(mm)} ({exact / len(mm) * 100:.2f}%)")
    print(f"  平均     : {sum(mm) / len(mm):.6f}")
    print(f"  50%tile  : {pct(mm, 0.50):.6f}")
    print(f"  95%tile  : {pct(mm, 0.95):.6f}")
    print(f"  99%tile  : {pct(mm, 0.99):.6f}")
    print(f"  最大     : {max(mm):.6f}   @ {worst[1]}")
    print(f"  高さのみ最大: {max(dh_all) * 1000:.6f} mm")
    print()
    print(f"Q(解ステータス)不一致: {qdiff}   ns(衛星数)不一致: {nsdiff}")

    ok = max(mm) <= tol_mm and qdiff == 0 and nsdiff == 0
    if max(mm) == 0.0 and qdiff == 0 and nsdiff == 0:
        print("\n判定: 完全一致 (bit-identical)")
    elif ok:
        print(f"\n判定: 一致 (最大差 {max(mm):.4f} mm <= 許容 {tol_mm} mm)")
    else:
        print(f"\n判定: 要調査 (最大差 {max(mm):.4f} mm / 許容 {tol_mm} mm)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
