//------------------------------------------------------------------------------
// basefix.h : workarounds for known defects in the vendor base library (src/)
//
// The base library is vendor code and must stay untouched, so the GUI works
// around these at the conf-text level. See app/qtapp/BASELIB_ISSUES.md for
// reproduction steps and impact.
//
//  1. str2enum() drops the sign of negative enum values
//     -> "inpstrN-ssr = off" (-1) is read back as 1 (STRSSR_SUB).
//        fixSsrFromConf() re-reads those lines from the conf text.
//
//  2. opt2str() has no case for format 4 (pos1-snrwthres_*)
//     -> saveopts() writes them empty, silently dropping the per-signal SNR
//        weighting thresholds. keepRawOpts()/restoreRawOpts() carry the
//        original lines across a save.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef BASEFIX_H
#define BASEFIX_H

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "rtklib.h"

//------------------------------------------------------------------------------
// (1) re-read inpstr1..4-ssr from a conf file and correct the loaded values
inline void fixSsrFromConf(const QString &file, int *strssr)
{
    QFile fp(file);
    if (!fp.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&fp);
    QRegularExpression re("^\\s*inpstr([1-9])-ssr\\s*=\\s*([A-Za-z0-9_-]+)");
    while (!in.atEnd()) {
        QRegularExpressionMatch m = re.match(in.readLine());
        if (!m.hasMatch()) continue;
        int idx = m.captured(1).toInt() - 1;
        if (idx < 0 || idx >= NSTRIN) continue;
        QString v = m.captured(2).toLower();
        if      (v == "off"  || v == "-1") strssr[idx] = STRSSR_OFF;
        else if (v == "main" || v == "0")  strssr[idx] = STRSSR_MAIN;
        else if (v == "sub"  || v == "1")  strssr[idx] = STRSSR_SUB;
    }
}
//------------------------------------------------------------------------------
// (2) options that opt2str() cannot serialize; carried verbatim across saves
inline QRegularExpression rawOptRe()
{
    return QRegularExpression("^\\s*(pos1-snrwthres_[a-z]+)\\s*=");
}
//------------------------------------------------------------------------------
// collect the raw text of unserializable options from a conf file
inline QStringList keepRawOpts(const QString &file)
{
    QStringList kept;
    QFile fp(file);
    if (!fp.open(QIODevice::ReadOnly | QIODevice::Text)) return kept;

    QTextStream in(&fp);
    QRegularExpression re = rawOptRe();
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (re.match(line).hasMatch()) kept.append(line);
    }
    return kept;
}
//------------------------------------------------------------------------------
// rewrite a just-saved conf, replacing the empty entries with the kept lines
inline void restoreRawOpts(const QString &file, const QStringList &kept)
{
    if (kept.isEmpty()) return;

    QFile fp(file);
    if (!fp.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QStringList out;
    QRegularExpression re = rawOptRe();
    bool inserted = false;
    {
        QTextStream in(&fp);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (re.match(line).hasMatch()) {
                if (!inserted) { out += kept; inserted = true; }
                continue;                       // drop the empty entry
            }
            out.append(line);
        }
    }
    fp.close();
    if (!inserted) return;                      // nothing to replace

    if (!fp.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream os(&fp);
    for (const QString &l : out) os << l << "\n";
}

#endif // BASEFIX_H
