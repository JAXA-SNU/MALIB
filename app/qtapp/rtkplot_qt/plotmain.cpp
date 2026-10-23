//------------------------------------------------------------------------------
// plotmain.cpp : RTKPLOT-Qt main window
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
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <QTimer>

#include "appstyle.h"
#include "conndlg.h"
#include "plotopt.h"
#include "plotmain.h"
#include "plotwidget.h"

//------------------------------------------------------------------------------
PlotMain::PlotMain(QWidget *parent) : QMainWindow(parent)
{
    data = new PlotData;
    setWindowTitle(QString("RTKPLOT-Qt  MALIB %1").arg(VER_MALIB));

    plot = new PlotWidget(data);
    setCentralWidget(plot);
    connect(plot, &PlotWidget::statusChanged, this, &PlotMain::showStatus);

    // ---- menu ---------------------------------------------------------------
    QMenu *mFile = menuBar()->addMenu("&File");
    mFile->addAction("Open &Solution 1...", this, &PlotMain::actOpenSol1);
    mFile->addAction("Open Solution &2...", this, &PlotMain::actOpenSol2);
    mFile->addAction("Open &Obs Data...",   this, &PlotMain::actOpenObs);
    mFile->addSeparator();
    mFile->addAction("&Clear", this, &PlotMain::actClear);
    mFile->addSeparator();
    actConn    = mFile->addAction("Co&nnect...",  this, &PlotMain::actConnect);
    actDisconn = mFile->addAction("&Disconnect", this, &PlotMain::actDisconnect);
    actConn->setShortcut(QKeySequence("Ctrl+T"));
    actDisconn->setShortcut(QKeySequence("Ctrl+D"));
    actDisconn->setEnabled(false);
    mFile->addSeparator();
    mFile->addAction("&Options...", this, &PlotMain::actOptions)
         ->setShortcut(QKeySequence("Ctrl+O"));
    mFile->addSeparator();
    mFile->addAction("E&xit", this, &QWidget::close);

    QMenu *mView = menuBar()->addMenu("&View");
    actSol1 = mView->addAction("Solution &1");
    actSol2 = mView->addAction("Solution &2");
    actCircles = mView->addAction("Track &Circles");
    for (QAction *a : {actSol1, actSol2, actCircles}) a->setCheckable(true);
    actSol1->setChecked(true);
    connect(actSol1, &QAction::toggled, this, &PlotMain::solSelChanged);
    connect(actSol2, &QAction::toggled, this, &PlotMain::solSelChanged);
    connect(actCircles, &QAction::toggled, this,
            [this](bool on) { plot->setTrackCircles(on); });

    // ---- toolbar ------------------------------------------------------------
    auto *tb = addToolBar("main");
    tb->setMovable(false);

    selType = new QComboBox();
    selType->addItem("Gnd Trk",    PLOT_TRK);
    selType->addItem("Position",   PLOT_SOLP);
    selType->addItem("Velocity",   PLOT_SOLV);
    selType->addItem("Accel",      PLOT_SOLA);
    selType->addItem("NSat/Age/Ratio", PLOT_NSAT);
    selType->setMinimumWidth(150);
    connect(selType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotMain::plotTypeChanged);

    selQFlag = new QComboBox();
    selQFlag->addItem("ALL", 0);
    for (int q = 1; q <= 7; q++) selQFlag->addItem(SOLQ_LABEL[q], q);
    selQFlag->setMinimumWidth(90);
    connect(selQFlag, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotMain::qFlagChanged);

    tb->addWidget(new QLabel(" Plot Type "));
    tb->addWidget(selType);
    tb->addWidget(new QLabel("  Quality "));
    tb->addWidget(selQFlag);
    tb->addSeparator();
    tb->addAction("Fit",   this, [this] { plot->fitRange();  plot->update(); })
      ->setToolTip("fit both axes to the data");
    tb->addAction("Fit Time",  this, [this] { plot->fitHoriz(); plot->update(); })
      ->setToolTip("fit the time axis only, keeping the current value range");
    tb->addAction("Fit Value", this, [this] { plot->fitVert();  plot->update(); })
      ->setToolTip("fit the value axis only, keeping the current time range");
    tb->addAction("Center", this, [this] { plot->centerOrigin(); })
      ->setToolTip("centre the view at the origin");
    tb->addSeparator();
    actAutoFit = tb->addAction("Auto");
    actAutoFit->setCheckable(true);
    actAutoFit->setChecked(true);
    actAutoFit->setToolTip("keep the view fitted while new data arrives\n"
                           "(turned off automatically when you pan or zoom)");
    connect(actAutoFit, &QAction::toggled, this,
            [this](bool on) { plot->setAutoFit(on); plot->update(); });
    actInfo = tb->addAction("Info");
    actInfo->setCheckable(true);
    actInfo->setChecked(true);
    actInfo->setToolTip("show the AVE / STD / RMS overlay");
    connect(actInfo, &QAction::toggled, this,
            [this](bool on) { plot->setShowInfo(on); });
    tb->addSeparator();
    tb->addAction("Options...", this, &PlotMain::actOptions);

    labelFile = new QLabel("");
    labelStatus = new QLabel("");
    statusBar()->addWidget(labelFile, 1);
    statusBar()->addPermanentWidget(labelStatus);

    connect(plot, &PlotWidget::autoFitOff, this, [this] {
        QSignalBlocker b(actAutoFit);
        actAutoFit->setChecked(false);
    });

    for (int i = 0; i < 2; i++) strinit(&rtStream[i]);
    rtTimer = new QTimer(this);
    connect(rtTimer, &QTimer::timeout, this, &PlotMain::streamTick);

    loadSettings();
    applyOpt();
}
//------------------------------------------------------------------------------
void PlotMain::showStatus(const QString &text)
{
    labelStatus->setText(text);
}
//------------------------------------------------------------------------------
void PlotMain::openSolution(const QStringList &files, int sel)
{
    if (files.isEmpty()) return;
    QString msg;
    if (!data->readSol(files, sel, msg)) {
        QMessageBox::warning(this, "RTKPLOT-Qt", msg);
        return;
    }
    data->updateOrigin(opt.origin, opt.ooPos);
    (sel == 0 ? actSol1 : actSol2)->setChecked(true);
    plot->setShowSol(sel, true);
    plot->fitRange();
    plot->update();

    QStringList names;
    for (int s = 0; s < 2; s++) {
        if (!data->solFiles[s].isEmpty()) {
            names << QFileInfo(data->solFiles[s].first()).fileName() +
                     (data->solFiles[s].size() > 1 ? "..." : "");
        }
    }
    labelFile->setText(names.join("  |  "));
    showStatus(msg);
}
//------------------------------------------------------------------------------
void PlotMain::openObs(const QStringList &files)
{
    if (files.isEmpty()) return;
    QString msg;
    if (!data->readObs(files, msg)) {
        QMessageBox::warning(this, "RTKPLOT-Qt", msg);
        return;
    }
    showStatus(msg);
}
//------------------------------------------------------------------------------
void PlotMain::actOpenSol1()
{
    QStringList f = QFileDialog::getOpenFileNames(
        this, "Open Solution 1", "", "Solution (*.pos *.stat *.nmea *.txt);;All (*)");
    openSolution(f, 0);
}
//------------------------------------------------------------------------------
void PlotMain::actOpenSol2()
{
    QStringList f = QFileDialog::getOpenFileNames(
        this, "Open Solution 2", "", "Solution (*.pos *.stat *.nmea *.txt);;All (*)");
    openSolution(f, 1);
}
//------------------------------------------------------------------------------
void PlotMain::actOpenObs()
{
    QStringList f = QFileDialog::getOpenFileNames(
        this, "Open Observation Data", "", "RINEX OBS (*.obs *.*o);;All (*)");
    openObs(f);
}
//------------------------------------------------------------------------------
void PlotMain::actClear()
{
    data->clearSol(0);
    data->clearSol(1);
    data->clearObs();
    labelFile->clear();
    showStatus("");
    plot->update();
}
//------------------------------------------------------------------------------
void PlotMain::plotTypeChanged()
{
    plot->setPlotType(selType->currentData().toInt());
}
//------------------------------------------------------------------------------
void PlotMain::qFlagChanged()
{
    plot->setQFlag(selQFlag->currentData().toInt());
    plot->update();
}
//------------------------------------------------------------------------------
void PlotMain::solSelChanged()
{
    plot->setShowSol(0, actSol1->isChecked());
    plot->setShowSol(1, actSol2->isChecked());
    plot->update();
}
//------------------------------------------------------------------------------
void PlotMain::actOptions()
{
    PlotOptDialog dlg(this);
    dlg.opt = opt;
    if (dlg.exec() != QDialog::Accepted) return;
    opt = dlg.opt;
    applyOpt();
}
//------------------------------------------------------------------------------
// push the option set into the canvas, the origin and the real-time timer
void PlotMain::applyOpt()
{
    data->updateOrigin(opt.origin, opt.ooPos);
    plot->setOpt(&opt);
    {
        QSignalBlocker b1(actAutoFit), b2(actInfo);
        actAutoFit->setChecked(opt.autoScale != 0);
        actInfo->setChecked(opt.showStats != 0);
    }
    if (rtTimer->isActive()) rtTimer->start(opt.refCycle);
    plot->update();
}
//------------------------------------------------------------------------------
void PlotMain::loadSettings()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtkplot_qt");
    restoreGeometry(s.value("window/geometry").toByteArray());
    if (!s.contains("window/geometry")) resize(900, 640);
    int t = s.value("plot/type", PLOT_TRK).toInt();
    int i = selType->findData(t);
    if (i >= 0) selType->setCurrentIndex(i);
    actCircles->setChecked(s.value("plot/circles", false).toBool());
    actAutoFit->setChecked(s.value("plot/autofit", true).toBool());
    actInfo->setChecked(s.value("plot/info", true).toBool());
    opt.load(s);

    for (int k = 0; k < 2; k++) {
        rtStreamType[k] = s.value(QString("conn/stream%1").arg(k), STR_NONE).toInt();
        rtFormat[k] = s.value(QString("conn/format%1").arg(k), SOLF_LLH).toInt();
        for (int j = 0; j < 3; j++)
            rtPaths[k][j] = s.value(QString("conn/path%1_%2").arg(k).arg(j)).toString();
    }
    rtTimeForm = s.value("conn/timeform", 0).toInt();
    rtDegForm  = s.value("conn/degform", 0).toInt();
    rtFieldSep = s.value("conn/fieldsep", "").toString();
    rtTimeOut  = s.value("conn/timeout", 0).toInt();
    rtReConn   = s.value("conn/reconn", 10000).toInt();
    for (int k = 0; k < 10; k++)
        rtHistory[k] = s.value(QString("conn/history%1").arg(k)).toString();
}
//------------------------------------------------------------------------------
void PlotMain::saveSettings()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtkplot_qt");
    s.setValue("window/geometry", saveGeometry());
    s.setValue("plot/type", selType->currentData().toInt());
    s.setValue("plot/circles", actCircles->isChecked());
    s.setValue("plot/autofit", actAutoFit->isChecked());
    s.setValue("plot/info", actInfo->isChecked());
    opt.save(s);

    for (int k = 0; k < 2; k++) {
        s.setValue(QString("conn/stream%1").arg(k), rtStreamType[k]);
        s.setValue(QString("conn/format%1").arg(k), rtFormat[k]);
        for (int j = 0; j < 3; j++)
            s.setValue(QString("conn/path%1_%2").arg(k).arg(j), rtPaths[k][j]);
    }
    s.setValue("conn/timeform", rtTimeForm);
    s.setValue("conn/degform",  rtDegForm);
    s.setValue("conn/fieldsep", rtFieldSep);
    s.setValue("conn/timeout",  rtTimeOut);
    s.setValue("conn/reconn",   rtReConn);
    for (int k = 0; k < 10; k++)
        s.setValue(QString("conn/history%1").arg(k), rtHistory[k]);
}
//------------------------------------------------------------------------------
// real-time input: open the configured streams and start polling
void PlotMain::actConnect()
{
    ConnectDialog dlg(this);
    for (int i = 0; i < 2; i++) {
        dlg.stream[i] = rtStreamType[i];
        dlg.format[i] = rtFormat[i];
        for (int k = 0; k < 3; k++) dlg.paths[i][k] = rtPaths[i][k];
    }
    dlg.timeForm = rtTimeForm;  dlg.degForm = rtDegForm;
    dlg.fieldSep = rtFieldSep;
    dlg.timeOutTime = rtTimeOut; dlg.reConnTime = rtReConn;
    for (int i = 0; i < 10; i++) dlg.tcpHistory[i] = rtHistory[i];

    if (dlg.exec() != QDialog::Accepted) return;

    for (int i = 0; i < 2; i++) {
        rtStreamType[i] = dlg.stream[i];
        rtFormat[i] = dlg.format[i];
        for (int k = 0; k < 3; k++) rtPaths[i][k] = dlg.paths[i][k];
    }
    rtTimeForm = dlg.timeForm;  rtDegForm = dlg.degForm;
    rtFieldSep = dlg.fieldSep;
    rtTimeOut = dlg.timeOutTime; rtReConn = dlg.reConnTime;
    for (int i = 0; i < 10; i++) rtHistory[i] = dlg.tcpHistory[i];

    if (!dlg.connectNow) return;                // [Set] only
    startConnect();
}
//------------------------------------------------------------------------------
// parse "<type>://<path>" and store it as stream ch (port of ConnectPath)
bool PlotMain::connectPath(const QString &path, int ch)
{
    rtStreamType[ch] = STR_NONE;
    int i = path.indexOf("://");
    if (i < 0) return false;

    QString type = path.left(i);
    if      (type == "serial") rtStreamType[ch] = STR_SERIAL;
    else if (type == "tcpsvr") rtStreamType[ch] = STR_TCPSVR;
    else if (type == "tcpcli") rtStreamType[ch] = STR_TCPCLI;
    else if (type.startsWith("ntrip")) rtStreamType[ch] = STR_NTRIPCLI;
    else if (type == "file")   rtStreamType[ch] = STR_FILE;
    else return false;

    QString body = path.mid(i + 3);
    rtPaths[ch][rtStreamType[ch] == STR_SERIAL ? 0 :
                rtStreamType[ch] == STR_FILE   ? 2 : 1] = body;
    rtFormat[ch] = SOLF_LLH;
    rtTimeForm = 0;
    rtDegForm  = 0;
    rtFieldSep = " ";
    rtTimeOut  = 0;
    rtReConn   = 10000;
    return true;
}
//------------------------------------------------------------------------------
void PlotMain::startConnect()
{
    trace(3, "startConnect: type=%d,%d\n", rtStreamType[0], rtStreamType[1]);
    if (connected) actDisconnect();
    if (!openStreams()) return;

    // real-time mode starts from an empty buffer
    data->clearSol(0); data->clearSol(1);
    data->week = 0;
    connected = true;
    actConn->setEnabled(false);
    actDisconn->setEnabled(true);
    plot->setShowSol(0, rtStreamType[0] != STR_NONE);
    plot->setShowSol(1, rtStreamType[1] != STR_NONE);
    rtTimer->start(opt.refCycle);
    showStatus("connected");
}
//------------------------------------------------------------------------------
bool PlotMain::openStreams()
{
    int stropt[8] = {0};
    stropt[0] = rtTimeOut;
    stropt[1] = rtReConn;
    stropt[2] = 1000;
    stropt[3] = 32768;
    strsetopt(stropt);

    int nopen = 0;
    for (int i = 0; i < 2; i++) {
        int type = rtStreamType[i];
        if (type == STR_NONE) continue;
        int kind = type == STR_SERIAL ? 0 : type == STR_FILE ? 2 : 1;
        QByteArray p = rtPaths[i][kind].toLocal8Bit();
        if (!stropen(&rtStream[i], type, STR_MODE_R, p.constData())) {
            showStatus(QString("stream %1 open error").arg(i + 1));
            continue;
        }
        nopen++;
    }
    if (!nopen) {
        QMessageBox::warning(this, "RTKPLOT-Qt", "no stream could be opened");
        return false;
    }
    return true;
}
//------------------------------------------------------------------------------
void PlotMain::actDisconnect()
{
    if (!connected) return;
    trace(3, "actDisconnect:\n");
    rtTimer->stop();
    for (int i = 0; i < 2; i++) strclose(&rtStream[i]);
    connected = false;
    actConn->setEnabled(true);
    actDisconn->setEnabled(false);
    showStatus("disconnected");
}
//------------------------------------------------------------------------------
// poll the streams and feed the bytes to inputsol() (port of TimeTimer)
void PlotMain::streamTick()
{
    static uint8_t buff[16384];
    gtime_t ts = {0};
    solopt_t sopt = solopt_default;
    QStringList st;
    bool newData = false;

    for (int i = 0; i < 2; i++) {
        if (rtStreamType[i] == STR_NONE) continue;

        sopt.posf  = rtFormat[i];
        sopt.times = rtTimeForm == 0 ? 0 : rtTimeForm - 1;
        sopt.timef = rtTimeForm >= 1;
        sopt.degf  = rtDegForm;
        QByteArray sep = rtFieldSep.toLocal8Bit();
        strncpy(sopt.sep, sep.constData(), sizeof(sopt.sep) - 1);

        char msg[MAXSTRMSG] = "";
        int stat = strstat(&rtStream[i], msg);
        if (*msg && strcmp(msg, "localhost")) {
            st << QString("(%1) %2").arg(i + 1).arg(msg);
        }
        (void)stat;

        int n;
        while ((n = strread(&rtStream[i], buff, sizeof(buff))) > 0) {
            for (int j = 0; j < n; j++) {
                int istat = inputsol(buff[j], ts, ts, 0.0, 0, &sopt, &data->solData[i]);
                if (istat == 0) continue;
                if (istat < 0) { actDisconnect(); return; }   // disconnect received
                if (data->week == 0 && data->solData[i].n == 1) {
                    time2gpst(data->solData[i].data[0].time, &data->week);
                    data->updateOrigin(opt.origin, opt.ooPos);
                }
                // keep at most rtBuffSize epochs (port of the VCL RtBuffSize)
                solbuf_t *sb = &data->solData[i];
                if (sb->n > opt.rtBuffSize) {
                    int drop = sb->n - opt.rtBuffSize;
                    memmove(sb->data, sb->data + drop,
                            sizeof(sol_t) * (size_t)(sb->n - drop));
                    sb->n -= drop;
                }
                newData = true;
            }
        }
    }
    if (newData) {
        trace(3, "streamTick: n=%d,%d\n", data->solData[0].n, data->solData[1].n);
        plot->dataChanged();
        int total = data->solData[0].n + data->solData[1].n;
        st << QString("%1 solutions").arg(total);
    }
    if (!st.isEmpty()) labelFile->setText(st.join("  "));
}
//------------------------------------------------------------------------------
PlotMain::~PlotMain()
{
    if (connected) {
        rtTimer->stop();
        for (int i = 0; i < 2; i++) strclose(&rtStream[i]);
    }
    delete data;
}
//------------------------------------------------------------------------------
void PlotMain::closeEvent(QCloseEvent *event)
{
    if (connected) actDisconnect();
    saveSettings();
    QMainWindow::closeEvent(event);
}
//------------------------------------------------------------------------------
