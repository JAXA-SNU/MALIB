//------------------------------------------------------------------------------
// RTKNAVI-Qt command line checker
//   usage: rtknavi_cli --selftest | --coverage
//
// This is the same option self check that rtknavi_qt exposes, built as a
// console subsystem program on Windows.
//
// rtknavi_qt.exe is a GUI subsystem binary so that no console window flashes
// when it is started from Explorer.  A side effect is that a shell does not
// wait for it and does not always connect its standard output:
//
//   cmd         rtknavi_qt.exe --coverage > out.txt   works, but the prompt
//                                                     returns immediately and
//                                                     %ERRORLEVEL% is not the
//                                                     program's exit code
//   PowerShell  .\rtknavi_qt.exe --coverage > out.txt writes an empty file
//
// Redirecting or piping the check output, and reading the number of failures
// from the exit code, therefore needs a console subsystem binary: this one.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QApplication>
#include <QStringList>
#include <cstdio>

#include "optdlg.h"
#include "rtklib.h"

static void usage(void)
{
    printf("usage: rtknavi_cli --selftest | --coverage\n"
           "  --selftest  drive every Options widget and check that it writes\n"
           "              the intended option and nothing else.\n"
           "              exit code = number of failures\n"
           "  --coverage  list every option and where it is edited\n"
           "              (widget / subdialog / UNPLACED)\n");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    strinitcom();

    QString mode;
    for (int i = 1; i < argc; i++) {
        QString a = argv[i];
        if (a == "--selftest" || a == "--coverage") mode = a;
        else { usage(); return 2; }
    }
    if (mode.isEmpty()) { usage(); return 2; }

    static prcopt_t p;
    static solopt_t s[2];
    static filopt_t fo;
    resetsysopts();
    getsysopts(&p, s, &fo);
    OptDialog dlg(nullptr, &p, s, &fo);

    if (mode == "--coverage") {
        QStringList out;
        dlg.coverage(out);
        for (const QString &l : out) printf("%s\n", qPrintable(l));
        return 0;
    }
    QStringList ng;
    int nfail = dlg.selfTest(ng);
    for (const QString &l : ng) printf("%s\n", qPrintable(l));
    printf("--- selftest: %d failure(s)\n", nfail);
    return nfail;
}
