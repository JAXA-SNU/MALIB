//------------------------------------------------------------------------------
// mondlg.h : RTK monitor dialog (Qt port of winapp/rtknavi/mondlg)
//
// Shows the internals of the running rtk server: server/solution status,
// per-satellite state, streams, observations and the MADOCA-PPP (SSR)
// corrections. Adapted to MALIB 1.3.0: 4 input streams / 10 total streams
// and the main/sub SSR planes (nav.ssr[MAXINSSR][]).
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/mondlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef MONDLG_H
#define MONDLG_H

#include <QDialog>

#include "rtklib.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;

class MonitorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MonitorDialog(rtksvr_t *svr, stream_t *moni, QWidget *parent = nullptr);

private slots:
    void typeChanged();
    void tick();
    void btnClearClicked();

private:
    void setTable(const QStringList &head, const QList<int> &width);
    void showRtk();
    void showSat();
    void showStr();
    void showObs();
    void showSsr();
    void showNav();
    void showConsole();

    rtksvr_t       *svr;
    stream_t       *moni;
    QComboBox      *selType, *selSys, *selSsr, *selStr, *selSat;
    QLabel         *label;
    QTableWidget   *table;
    QPlainTextEdit *console;
    QStackedWidget *stack;
    QPushButton    *btnPause, *btnClear;
    QTimer         *timer;
};

#endif // MONDLG_H
