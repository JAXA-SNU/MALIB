//------------------------------------------------------------------------------
// plotmain.h : RTKPLOT-Qt main window
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotmain (RTKLIB 2.4.3 b35).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef PLOTMAIN_H
#define PLOTMAIN_H

#include <QMainWindow>

#include "plotdata.h"
#include "plotopt.h"

class QAction;
class QComboBox;
class QLabel;
class QTimer;
class PlotWidget;

class PlotMain : public QMainWindow
{
    Q_OBJECT
public:
    explicit PlotMain(QWidget *parent = nullptr);
    ~PlotMain() override;

    void openSolution(const QStringList &files, int sel);
    void openObs(const QStringList &files);
    // command-line real-time input: "<type>://<path>" (-p/-p1/-p2)
    bool connectPath(const QString &path, int ch);
    void startConnect();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void actOpenSol1();
    void actOpenSol2();
    void actOpenObs();
    void actClear();
    void actOptions();
    void actConnect();
    void actDisconnect();
    void streamTick();                  // poll the real-time streams
    void plotTypeChanged();
    void qFlagChanged();
    void solSelChanged();
    void showStatus(const QString &text);

private:
    bool openStreams();
    void loadSettings();
    void saveSettings();

    // PlotData embeds nav_t which is ~135MB in MALIB 1.3.0; keeping it as a
    // by-value member would overflow the stack when PlotMain is a local.
    void applyOpt();

    PlotData   *data;
    PlotOpt     opt;
    PlotWidget *plot;
    QComboBox  *selType, *selQFlag;
    QAction    *actSol1, *actSol2, *actCircles;
    QAction    *actAutoFit, *actInfo;
    QLabel     *labelFile, *labelStatus;
    QAction    *actConn, *actDisconn;

    // real-time input (port of the VCL ConnectState / Stream[2] logic)
    stream_t    rtStream[2] = {};
    int         rtStreamType[2] = {STR_NONE, STR_NONE};
    int         rtFormat[2] = {SOLF_LLH, SOLF_LLH};
    QString     rtPaths[2][3];
    int         rtTimeForm = 0, rtDegForm = 0;
    QString     rtFieldSep, rtHistory[10];
    int         rtTimeOut = 0, rtReConn = 10000;
    bool        connected = false;
    QTimer     *rtTimer = nullptr;
};

#endif // PLOTMAIN_H
