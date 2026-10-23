//------------------------------------------------------------------------------
// RTKNAVI-Qt entry point
//   usage: rtknavi_qt [-o conffile] [-s] [-m port]
//     -o file : load rtkrcv-style options file
//     -s      : start the rtk server on startup (same as rtkrcv -s)
//     -m port : monitor port for the Plot button (same as rtkrcv -m)
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/rtknavi.cpp.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include <QStringList>
#include <cstdio>

#ifdef WIN32
#include <windows.h>
// GUI subsystem apps get no console. When launched from a terminal, attach to
// it so --selftest / --coverage output is visible; otherwise stay silent.
static void attachParentConsole(void)
{
    // If stdout is already redirected (pipe or file), leave it alone so the
    // output can be captured; only bind to the console when it is not.
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    bool redirected = h && h != INVALID_HANDLE_VALUE &&
                      GetFileType(h) != FILE_TYPE_UNKNOWN;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (!redirected) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}
// Can anything actually read what we print?  A GUI subsystem process is not
// waited for by the shell, and PowerShell does not hook up "> file" for one at
// all: the check would run and its output would go nowhere.  Detect that and
// say so in a dialog rather than appearing to do nothing.
static bool consoleOutputUsable(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    DWORD t = GetFileType(h);
    return t == FILE_TYPE_CHAR || t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE;
}
#else
static void attachParentConsole(void) {}
static bool consoleOutputUsable(void) { return true; }
#endif

#include "appstyle.h"
#include "navimain.h"
#include "rtklib.h"
#include "optdlg.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyAppStyle(app);

    // Winsock startup. Without it every socket() fails with WSANOTINITIALISED
    // (10093) on Windows, so all TCP / NTRIP / UDP / FTP / HTTP streams break.
    // On other platforms this is a no-op.
    strinitcom();

    // --selftest: exhaustive Options widget-to-option mapping check
    for (int i = 1; i < argc; i++) {
        if (QString(argv[i]) == "--coverage" || QString(argv[i]) == "--selftest") {
            attachParentConsole();
            if (!consoleOutputUsable()) {
                QMessageBox::warning(nullptr, "RTKNAVI-Qt",
                    "The option checks cannot write their output from this "
                    "program.\n\n"
                    "rtknavi_qt.exe is a GUI subsystem binary, so the shell "
                    "does not wait for it and PowerShell does not connect a "
                    "redirected file to it.\n\n"
                    "Use the console build instead:\n\n"
                    "    rtknavi_cli.exe " + QString(argv[i]));
                return 2;
            }
        }
        if (QString(argv[i]) == "--coverage") {
            static prcopt_t p; static solopt_t s[2]; static filopt_t fo;
            resetsysopts();
            getsysopts(&p, s, &fo);
            OptDialog dlg(nullptr, &p, s, &fo);
            QStringList out;
            dlg.coverage(out);
            for (const QString &l : out) printf("%s\n", qPrintable(l));
            return 0;
        }
        if (QString(argv[i]) == "--selftest") {
            static prcopt_t p;
            static solopt_t s[2];
            static filopt_t fo;
            resetsysopts();
            getsysopts(&p, s, &fo);
            OptDialog dlg(nullptr, &p, s, &fo);
            QStringList ng;
            int nfail = dlg.selfTest(ng);
            for (const QString &l : ng) printf("%s\n", qPrintable(l));
            return nfail;
        }
    }

    MainWindow win;
    bool autostart = false;
    for (int i = 1; i < argc; i++) {
        if (QString(argv[i]) == "-o" && i + 1 < argc) win.loadConf(argv[i + 1]);
        if (QString(argv[i]) == "-s") autostart = true;
        if (QString(argv[i]) == "-m" && i + 1 < argc)
            win.setMoniPort(QString(argv[i + 1]).toInt());
    }
    win.show();
    if (autostart) {
        QTimer::singleShot(500, &win, [&win] {
            QMetaObject::invokeMethod(&win, "btnStartClicked");
        });
    }
    return app.exec();
}
