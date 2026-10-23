//------------------------------------------------------------------------------
// textconv.h : text conversion at the base library boundary
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
//
// Paths, receiver commands and option strings are handed to the base library
// as plain char buffers and end up in fopen() / CreateFile(), which on Windows
// interpret them in the ANSI code page (CP932 on a Japanese system).  So every
// conversion at that boundary has to use the local 8-bit encoding.
//
// Writing already does: qPrintable() is toLocal8Bit().  Reading back did not:
// the implicit QString(const char *) is fixed to UTF-8 in Qt 6, so a path with
// Japanese characters came back mangled once it had been through a char buffer
// (field test A-07).  On Linux the local encoding is UTF-8 and both directions
// agree, which is why it only showed up on Windows.
//
// Use fromLib() whenever a char buffer owned by the library becomes a QString.
//
#ifndef TEXTCONV_H
#define TEXTCONV_H

#include <QByteArray>
#include <QString>

inline QString fromLib(const char *s)
{
    return QString::fromLocal8Bit(s ? s : "");
}

inline QByteArray toLib(const QString &s)
{
    return s.toLocal8Bit();
}

#endif // TEXTCONV_H
