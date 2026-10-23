//------------------------------------------------------------------------------
// navimain.h : RTKNAVI-Qt main window
//
// Faithful port of winapp/rtknavi (solution panel + plot panels + stream
// indicator strip + bottom buttons). Options come from a rtkrcv .conf via
// the Options... button until the option dialogs are ported (Phase 3).
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/navimain.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef NAVIMAIN_H
#define NAVIMAIN_H

#include <QMainWindow>

#include "rtklib.h"

class QLabel;
class QPushButton;
class QScrollBar;
class QSplitter;
class QTimer;
class PlotPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void setMoniPort(int port) { moniPort = port; }
    void loadConf(const QString &file);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void btnStartClicked();
    void btnStopClicked();
    void btnPlotClicked();
    void btnOptClicked();
    void btnInputStrClicked();
    void btnMonitorClicked();
    void btnTimeSysClicked();
    void btnSolTypeClicked();
    void scbSolChanged(int value);
    void timerTick();

private:
    int  moniPort = 52001;              // 0: monitor port disabled
    QTimer *keepAlive = nullptr;
    int  serialInB[NSTRIN] = {};        // last seen input byte count
    uint32_t serialTick[NSTRIN] = {};   // when it last moved
    bool serialSeen[NSTRIN] = {};       // data arrived at least once

    bool svrStart();
    void checkSerialStreams();          // BASELIB_ISSUES #4 workaround
    bool openMoni();                    // monitor port (rtkrcv -m)
    void closeMoni();
    void svrStop();
    void updateLog(int stat, gtime_t time, double *rr, float *qr,
                   double *rb, int ns, double age, double ratio);
    void updateTime();
    void updatePos();
    void updateStr();
    void updatePlots();
    void updateScb();
    void log(const QString &msg);
    void loadSettings();
    void saveSettings();
    QString defaultConfPath() const;

    QString confFile;
    int     timeSys = 0;                // 0:GPST,1:UTC,2:LT,3:GPST tow
    int     solType = 0;                // 0:dms,1:deg,2:xyz,3:enu,4:pyl
    int     timerCycle = 0, timerInact = 0;

    QLabel      *labelTime, *panelTitle, *labelSolLabel, *labelSol, *indSol;
    QLabel      *plabel[3], *posLabel[3], *labelStd, *labelNSat, *message;
    QLabel      *indSvr, *indStr[MAXSTRRTK];
    QPushButton *btnStart, *btnStop, *btnMark, *btnPlot, *btnOpt, *btnExit;
    QPushButton *btnTimeSys, *btnSolType, *btnInput, *btnOutput, *btnLog;
    QPushButton *btnMonitor;
    QString      ntripHistory[10];
    QString      rcvCmds[NSTRIN][3];    // receiver commands edited in the dialog
    int          rcvCmdEna[NSTRIN][3] = {};
    QScrollBar  *scbSol;
    QSplitter   *splitter;
    PlotPanel   *plot1, *plot2;
    QTimer      *timer;
    bool         scbLock = false;
};

#endif // NAVIMAIN_H
