//------------------------------------------------------------------------------
// RTKPLOT-Qt entry point
//   usage: rtkplot_qt [-p|-p1 path] [-p2 path] [solution-file ...]
//     path: "<type>://<path>"  type = serial|tcpsvr|tcpcli|ntrip|file
//   [-t title] [-x trace-level]
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot (RTKLIB 2.4.3 b35).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QApplication>
#include <QStringList>

#include "appstyle.h"
#include "plotmain.h"
#include "rtklib.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyAppStyle(app);

    // see the note in rtknavi_qt/main.cpp: required before any socket use
    strinitcom();

    PlotMain win;
    QStringList files;
    QString path[2];
    int trace = 0;
    QString title;
    for (int i = 1; i < argc; i++) {
        QString a = argv[i];
        if      ((a == "-p" || a == "-p1") && i + 1 < argc) path[0] = argv[++i];
        else if (a == "-p2" && i + 1 < argc) path[1] = argv[++i];
        else if (a == "-t"  && i + 1 < argc) title = argv[++i];
        else if (a == "-x"  && i + 1 < argc) trace = QString(argv[++i]).toInt();
        else if (a[0] != '-') files << a;
    }
    if (trace > 0) { traceopen("rtkplot_qt.trace"); tracelevel(trace); }
    if (!title.isEmpty()) win.setWindowTitle(title);
    win.show();

    if (!path[0].isEmpty() || !path[1].isEmpty()) {
        for (int i = 0; i < 2; i++)
            if (!path[i].isEmpty()) win.connectPath(path[i], i);
        win.startConnect();
    }
    else if (!files.isEmpty()) win.openSolution(files, 0);

    int stat = app.exec();
    if (trace > 0) traceclose();
    return stat;
}
