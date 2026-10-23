//------------------------------------------------------------------------------
// Phase 1 spike: open TcpOptDialog standalone
//   usage: spike_tcpopt [opt] [path]
//     opt : 0:TCP Server ... 3:NTRIP Client (default) ... 7:UDP Client
//     path: [user[:passwd]]@addr[:port][/mntpnt]
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QApplication>
#include <QDebug>

#include "tcpoptdlg.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    TcpOptDialog dialog;
    dialog.Opt  = argc > 1 ? atoi(argv[1]) : 3;
    dialog.Path = argc > 2 ? argv[2]
                           : "user:pass@rtk2go.com:2101/EXAMPLE";
    dialog.History[0] = "rtk2go.com";
    dialog.History[1] = "163.43.83.22";

    if (dialog.exec() == QDialog::Accepted) {
        qDebug().noquote() << "path =" << dialog.Path;
    } else {
        qDebug().noquote() << "canceled";
    }
    return 0;
}
