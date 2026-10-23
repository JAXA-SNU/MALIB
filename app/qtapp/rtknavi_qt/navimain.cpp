//------------------------------------------------------------------------------
// navimain.cpp : RTKNAVI-Qt main window
//
// - layout / solution panel / plots / indicators: port of winapp/rtknavi
// - server control: port of app/consapp/rtkrcv/rtkrcv.c (option table,
//   startsvr/stopsvr) so a rtkrcv .conf works as-is
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
#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QFontMetrics>
#include <QScrollBar>
#include <QSplitter>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "instrdlg.h"
#include "outstrdlg.h"
#include "optdlg.h"
#include "mondlg.h"
#include "navimain.h"
#include "textconv.h"
#include "appstyle.h"
#include "plotpanel.h"
#include "basefix.h"

#define TIMEOUT_SOL     10000           // solution inactive timeout (ms)
#define TIMER_CYCLE     100             // display update cycle (ms)
#define MAXSTR          1024            // max length of a stream path (rtkrcv.c)
#define SQRT(x)         ((x)<=0.0?0.0:sqrt(x))

static const QString CHARDEG = QString::fromUtf8("°");

// option value labels (same as rtkrcv.c) -------------------------------------
#define TIMOPT  "0:gpst,1:utc,2:jst,3:tow"
#define CONOPT  "0:dms,1:deg,2:xyz,3:enu,4:pyl"
#define FLGOPT  "0:off,1:std+2:age/ratio/ns"
#define ISTOPT  "0:off,1:serial,2:file,3:tcpsvr,4:tcpcli,6:ntripcli,7:ftp,8:http,10:udpsvr,11:udpcli"
#define OSTOPT  "0:off,1:serial,2:file,3:tcpsvr,4:tcpcli,5:ntripsvr,9:ntripcas,10:udpsvr,11:udpcli"
#define FMTOPT  "0:rtcm2,1:rtcm3,2:oem4,3:oem3,4:ubx,5:ss2,6:hemis,7:skytraq,8:javad,9:nvs,10:binex,11:rt17,12:sbf,14:sp3,15:l6,20:stat"
#define NMEOPT  "0:off,1:latlon,2:single"
#define SOLOPT_ "0:llh,1:xyz,2:enu,3:nmea,4:stat"
#define MSGOPT  "0:all,1:rover,2:base,3:corr"
#define SSROPT  "0:main,1:sub,-1:off"

// server control state (mirrors rtkrcv.c statics) -----------------------------
// rtksvr_t is ~2.3GB (rtcm_t x4 + raw_t x4). A static instance breaks the
// PE 2GB image limit on Windows, so it lives on the heap (zeroed like a static).
static rtksvr_t &svr = *(rtksvr_t *)calloc(1, sizeof(rtksvr_t));
static stream_t moni;                   /* monitor stream (see openMoni) */
static const char *SOL_FMT_NAME[]={"LLH","XYZ","ENU","NMEA","STAT"};
#define INTKEEPALIVE 1000               /* keep alive interval (ms) */

static char passwd[MAXSTR]="admin";
static int timetype     =0;
static int soltype      =0;
static int solflag      =2;
static int strtype[MAXSTRRTK]={
    STR_NONE,STR_NONE,STR_NONE,STR_NONE,STR_NONE,
    STR_NONE,STR_NONE,STR_NONE,STR_NONE,STR_NONE
};
static char strpath[MAXSTRRTK][MAXSTR]={"","","","","","","","","",""};
static int strfmt[NSTRIN+NSTROUT]={
    STRFMT_UBX,STRFMT_RTCM3,STRFMT_SP3,STRFMT_SP3,SOLF_LLH,SOLF_NMEA
};
static int strssr[NSTRIN]={
    STRSSR_OFF,STRSSR_MAIN,STRSSR_SUB,STRSSR_OFF
};
static int svrcycle     =10;
static int timeout      =10000;
static int reconnect    =10000;
static int nmeacycle    =5000;
static int buffsize     =32768;
static int navmsgsel    =0;
static char proxyaddr[256]="";
static int nmeareq      =0;
static double nmeapos[] ={0,0,0};
static char rcvcmds[NSTRIN][MAXSTR]={""};
static char startcmd[MAXSTR]="";
static char stopcmd [MAXSTR]="";
static int fswapmargin  =30;
static char sta_name[256]="";
static double maxdrtr   =0;

static prcopt_t prcopt;
static solopt_t solopt[NSTROUT]={{0}};
static filopt_t filopt  ={""};
static gtime_t rst = {0};

static opt_t rcvopts[]={
    {"console-passwd",  2,  (void *)passwd,                ""     },
    {"console-timetype",3,  (void *)&timetype,             TIMOPT },
    {"console-soltype", 3,  (void *)&soltype,              CONOPT },
    {"console-solflag", 0,  (void *)&solflag,              FLGOPT },

    {"inpstr1-type",    3,  (void *)&strtype[IDXSTRIN],    ISTOPT },
    {"inpstr2-type",    3,  (void *)&strtype[IDXSTRIN+1],  ISTOPT },
    {"inpstr3-type",    3,  (void *)&strtype[IDXSTRIN+2],  ISTOPT },
    {"inpstr4-type",    3,  (void *)&strtype[IDXSTRIN+3],  ISTOPT },
    {"inpstr1-path",    2,  (void *)strpath [IDXSTRIN],    ""     },
    {"inpstr2-path",    2,  (void *)strpath [IDXSTRIN+1],  ""     },
    {"inpstr3-path",    2,  (void *)strpath [IDXSTRIN+2],  ""     },
    {"inpstr4-path",    2,  (void *)strpath [IDXSTRIN+3],  ""     },
    {"inpstr1-format",  3,  (void *)&strfmt [IDXSTRIN],    FMTOPT },
    {"inpstr2-format",  3,  (void *)&strfmt [IDXSTRIN+1],  FMTOPT },
    {"inpstr3-format",  3,  (void *)&strfmt [IDXSTRIN+2],  FMTOPT },
    {"inpstr4-format",  3,  (void *)&strfmt [IDXSTRIN+3],  FMTOPT },
    {"inpstr2-nmeareq", 3,  (void *)&nmeareq,              NMEOPT },
    {"inpstr2-nmealat", 1,  (void *)&nmeapos[0],           "deg"  },
    {"inpstr2-nmealon", 1,  (void *)&nmeapos[1],           "deg"  },
    {"inpstr2-nmeahgt", 1,  (void *)&nmeapos[2],           "m"    },
    {"inpstr1-ssr",     3,  (void *)&strssr [IDXSTRIN],    SSROPT },
    {"inpstr2-ssr",     3,  (void *)&strssr [IDXSTRIN+1],  SSROPT },
    {"inpstr3-ssr",     3,  (void *)&strssr [IDXSTRIN+2],  SSROPT },
    {"inpstr4-ssr",     3,  (void *)&strssr [IDXSTRIN+3],  SSROPT },
    {"outstr1-type",    3,  (void *)&strtype[IDXSTROUT],   OSTOPT },
    {"outstr2-type",    3,  (void *)&strtype[IDXSTROUT+1], OSTOPT },
    {"outstr1-path",    2,  (void *)strpath [IDXSTROUT],   ""     },
    {"outstr2-path",    2,  (void *)strpath [IDXSTROUT+1], ""     },
    {"outstr1-format",  3,  (void *)&strfmt [IDXSTROUT],   SOLOPT_},
    {"outstr2-format",  3,  (void *)&strfmt [IDXSTROUT+1], SOLOPT_},
    {"logstr1-type",    3,  (void *)&strtype[IDXSTRLOG],   OSTOPT },
    {"logstr2-type",    3,  (void *)&strtype[IDXSTRLOG+1], OSTOPT },
    {"logstr3-type",    3,  (void *)&strtype[IDXSTRLOG+2], OSTOPT },
    {"logstr4-type",    3,  (void *)&strtype[IDXSTRLOG+3], OSTOPT },
    {"logstr1-path",    2,  (void *)strpath [IDXSTRLOG],   ""     },
    {"logstr2-path",    2,  (void *)strpath [IDXSTRLOG+1], ""     },
    {"logstr3-path",    2,  (void *)strpath [IDXSTRLOG+2], ""     },
    {"logstr4-path",    2,  (void *)strpath [IDXSTRLOG+3], ""     },

    {"misc-svrcycle",   0,  (void *)&svrcycle,             "ms"   },
    {"misc-timeout",    0,  (void *)&timeout,              "ms"   },
    {"misc-reconnect",  0,  (void *)&reconnect,            "ms"   },
    {"misc-nmeacycle",  0,  (void *)&nmeacycle,            "ms"   },
    {"misc-buffsize",   0,  (void *)&buffsize,             "bytes"},
    {"misc-navmsgsel",  3,  (void *)&navmsgsel,            MSGOPT },
    {"misc-proxyaddr",  2,  (void *)proxyaddr,             ""     },
    {"misc-fswapmargin",0,  (void *)&fswapmargin,          "s"    },

    {"misc-startcmd",   2,  (void *)startcmd,              ""     },
    {"misc-stopcmd",    2,  (void *)stopcmd,               ""     },

    {"misc-maxdrtr",    1,  (void *)&maxdrtr,              "s"    },

    {"file-cmdfile1",   2,  (void *)rcvcmds[IDXSTRIN],     ""     },
    {"file-cmdfile2",   2,  (void *)rcvcmds[IDXSTRIN+1],   ""     },
    {"file-cmdfile3",   2,  (void *)rcvcmds[IDXSTRIN+2],   ""     },
    {"file-cmdfile4",   2,  (void *)rcvcmds[IDXSTRIN+3],   ""     },

    {"",0,NULL,""}
};

// solution status labels/colors (VCL UpdatePos + SOLQ_DR) ---------------------
static const char *SOL_STR[]={
    "----","FIX","FLOAT","SBAS","DGPS","SINGLE","PPP","DR"
};
static const char *SOL_CSS[]={
    "#c0c0c0","#008000","#ff8000","#ff00ff","#0000ff","#ff0000","#008080","#806000"
};
static const char *PANEL_TITLE[]={
    "Lat/Lon/Height","Lat/Lon/Height","X/Y/Z-ECEF","E/N/U-Baseline",
    "Pitch/Yaw/Length-Baseline"
};

// read receiver commands (same as rtkrcv.c) -----------------------------------
static int readcmd(const char *file, char *cmd, int type)
{
    FILE *fp;
    char buff[MAXSTR],*p=cmd;
    int i=0;

    if (!(fp=fopen(file,"r"))) return 0;

    while (fgets(buff,sizeof(buff),fp)) {
        if (*buff=='@') i++;
        else if (i==type&&p+strlen(buff)+1<cmd+MAXRCVCMD) {
            p+=sprintf(p,"%s",buff);
        }
    }
    fclose(fp);
    return 1;
}
//------------------------------------------------------------------------------
static QLabel *makeLamp(const QString &tip, int w = 14)
{
    auto *l = new QLabel();
    l->setFixedSize(w, 12);
    l->setToolTip(tip);
    l->setStyleSheet("background:white;border:1px solid #808080;");
    return l;
}
//------------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QString("RTKNAVI-Qt ver.%1 %2 MALIB %3")
                       .arg(VER_RTKLIB, PATCH_LEVEL_MALIB, VER_MALIB));

    navi.svr = &svr;
    navi.prcopt = &prcopt;

    auto *central = new QWidget();
    setCentralWidget(central);

    // ---- top strip: time + stream indicators --------------------------------
    labelTime = new QLabel("2000/01/01 00:00:00.0");
    QFont ft = labelTime->font();
    ft.setPointSizeF(ft.pointSizeF() * 1.3);
    labelTime->setFont(ft);
    btnTimeSys = new QPushButton("GPST");
    btnTimeSys->setFixedSize(46, 20);
    btnTimeSys->setToolTip("time system (GPST/UTC/LT/GPST tow)");

    indSvr = makeLamp("server state");
    auto mkStrBtn = [this](const QString &t, const QString &tip) {
        auto *b = new QPushButton(t);
        // wide enough for the label plus the button frame
        QFontMetrics fm(b->font());
        b->setFixedSize(qMax(26, fm.horizontalAdvance(t) + 14), 20);
        b->setToolTip(tip);
        return b;
    };
    btnInput  = mkStrBtn("I", "input streams...");
    btnOutput = mkStrBtn("O", "output streams...");
    btnLog    = mkStrBtn("L", "log streams...");
    auto *stripFrame = new QFrame();
    stripFrame->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    stripFrame->setLineWidth(1);
    auto *strip = new QHBoxLayout(stripFrame);
    strip->setContentsMargins(4, 1, 4, 1);
    strip->setSpacing(2);
    strip->addWidget(labelTime);
    strip->addWidget(btnTimeSys);
    strip->addStretch();
    strip->addWidget(indSvr);
    strip->addSpacing(6);
    strip->addWidget(btnInput);
    static const char *tips[MAXSTRRTK] = {
        "input rover","input 2","input 3","input 4",
        "output solution 1","output solution 2",
        "log 1","log 2","log 3","log 4"
    };
    for (int i = 0; i < MAXSTRRTK; i++) {
        indStr[i] = makeLamp(tips[i]);
        if (i == IDXSTROUT) { strip->addSpacing(4); strip->addWidget(btnOutput); }
        if (i == IDXSTRLOG) { strip->addSpacing(4); strip->addWidget(btnLog); }
        strip->addWidget(indStr[i]);
    }

    // ---- left: solution panel ------------------------------------------------
    auto *solPanel = new QFrame();
    solPanel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    solPanel->setStyleSheet(
        "QFrame{background:white;} QLabel{color:black;background:white;}");
    solPanel->setMinimumWidth(160);

    panelTitle = new QLabel(PANEL_TITLE[0]);
    panelTitle->setStyleSheet("color:gray;background:white;");
    btnSolType = new QPushButton(QString::fromUtf8("▾"));
    btnSolType->setFlat(true);
    btnSolType->setFixedSize(20, 18);
    btnSolType->setStyleSheet("color:gray;background:white;border:none;");
    btnSolType->setToolTip("solution format");

    labelSolLabel = new QLabel("Solution:");
    labelSol = new QLabel("----");
    QFont fsol = labelSol->font();
    fsol.setBold(true);
    fsol.setPointSizeF(fsol.pointSizeF() * 1.2);
    labelSol->setFont(fsol);
    indSol = new QLabel();
    indSol->setFixedSize(14, 14);
    indSol->setStyleSheet("background:white;border:1px solid #999;border-radius:7px;");

    for (int i = 0; i < 3; i++) {
        plabel[i] = new QLabel();
        posLabel[i] = new QLabel("-");
        posLabel[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    QFont fpos = font();
    fpos.setPointSizeF(fpos.pointSizeF() * 2.0);
    for (int i = 0; i < 3; i++) {
        plabel[i]->setFont(fpos);
        posLabel[i]->setFont(fpos);
    }
    labelStd  = new QLabel();
    labelNSat = new QLabel();
    labelStd->setStyleSheet("color:gray;background:white;");
    labelNSat->setStyleSheet("color:gray;background:white;");
    labelStd->setAlignment(Qt::AlignCenter);
    labelNSat->setAlignment(Qt::AlignCenter);

    scbSol = new QScrollBar(Qt::Horizontal);
    scbSol->setEnabled(false);

    auto *sv = new QVBoxLayout(solPanel);
    sv->setContentsMargins(6, 3, 6, 3);
    sv->setSpacing(1);
    auto *hdr = new QHBoxLayout();
    hdr->setContentsMargins(0, 0, 0, 0);
    hdr->addWidget(panelTitle);
    hdr->addStretch();
    hdr->addWidget(btnSolType);
    sv->addLayout(hdr);
    sv->addStretch(2);
    auto *solr = new QHBoxLayout();
    solr->addWidget(labelSolLabel);
    solr->addStretch();
    solr->addWidget(labelSol);
    solr->addWidget(indSol);
    sv->addLayout(solr);
    auto *pg = new QGridLayout();
    pg->setContentsMargins(0, 0, 0, 0);
    pg->setVerticalSpacing(0);
    for (int i = 0; i < 3; i++) {
        pg->addWidget(plabel[i], i, 0);
        pg->addWidget(posLabel[i], i, 1);
    }
    pg->setColumnStretch(1, 1);
    sv->addLayout(pg);
    sv->addWidget(labelStd);
    sv->addWidget(labelNSat);
    sv->addStretch(3);
    sv->addWidget(scbSol);

    // ---- plots ----------------------------------------------------------------
    plot1 = new PlotPanel(1);           // Rover SNR
    plot2 = new PlotPanel(6);           // Gnd Trk

    // ---- bottom: message + buttons --------------------------------------------
    message = new QLabel("");
    message->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    message->setMinimumHeight(17);
    message->setStyleSheet("padding-left:3px;");

    btnStart = new QPushButton(QString::fromUtf8("▶ &Start"));
    btnStop  = new QPushButton(QString::fromUtf8("■ St&op"));
    btnMark  = new QPushButton("Mark...");
    btnMonitor = new QPushButton("&Monitor...");
    btnPlot  = new QPushButton(QString::fromUtf8("⊕ &Plot"));
    btnOpt   = new QPushButton(QString::fromUtf8("⚙ Op&tions..."));
    btnExit  = new QPushButton("E&xit");
    btnStop->setVisible(false);
    btnMark->setEnabled(false);
    btnMark->setToolTip("Phase 3 (not ported yet)");
    btnOpt->setToolTip("load rtkrcv .conf (options dialog: Phase 3)");
    auto *btns = new QHBoxLayout();
    for (QPushButton *b : {btnStart, btnStop, btnMark, btnMonitor, btnPlot, btnOpt, btnExit}) {
        b->setMinimumHeight(26);
        btns->addWidget(b, 1);
    }
    btns->setSpacing(2);

    // ---- assemble ---------------------------------------------------------------
    // draggable splitters between the panels (same as the original RTKNAVI)
    splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(true);
    splitter->setHandleWidth(3);
    splitter->addWidget(solPanel);
    splitter->addWidget(plot1);
    splitter->addWidget(plot2);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);

    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addWidget(stripFrame);
    vbox->addWidget(splitter, 1);
    vbox->addWidget(message);
    vbox->addLayout(btns);

    connect(btnStart,   &QPushButton::clicked, this, &MainWindow::btnStartClicked);
    connect(btnStop,    &QPushButton::clicked, this, &MainWindow::btnStopClicked);
    connect(btnPlot,    &QPushButton::clicked, this, &MainWindow::btnPlotClicked);
    connect(btnOpt,     &QPushButton::clicked, this, &MainWindow::btnOptClicked);
    connect(btnInput,   &QPushButton::clicked, this, &MainWindow::btnInputStrClicked);
    connect(btnMonitor, &QPushButton::clicked, this, &MainWindow::btnMonitorClicked);
    connect(btnOutput,  &QPushButton::clicked, this, [this] {
        OutStrDialog dlg(0, this);
        for (int i = 0; i < NSTROUT; i++) {
            dlg.strType[i] = strtype[IDXSTROUT + i];
            dlg.strPath[i] = fromLib(strpath[IDXSTROUT + i]);
            dlg.strFmt[i]  = strfmt[IDXSTROUT + i];
        }
        for (int i = 0; i < 10; i++) dlg.history[i] = ntripHistory[i];
        if (dlg.exec() != QDialog::Accepted) return;
        for (int i = 0; i < NSTROUT; i++) {
            strtype[IDXSTROUT + i] = dlg.strType[i];
            strncpy(strpath[IDXSTROUT + i], toLib(dlg.strPath[i]).constData(), MAXSTR - 1);
            strfmt[IDXSTROUT + i] = dlg.strFmt[i];
        }
        // keep out-solformat showing what solution 1 will actually produce
        solopt[0].posf = strfmt[IDXSTROUT];
        for (int i = 0; i < 10; i++) ntripHistory[i] = dlg.history[i];
        log("output streams updated");
    });
    connect(btnLog,     &QPushButton::clicked, this, [this] {
        OutStrDialog dlg(1, this);
        for (int i = 0; i < NSTRLOG; i++) {
            dlg.strType[i] = strtype[IDXSTRLOG + i];
            dlg.strPath[i] = fromLib(strpath[IDXSTRLOG + i]);
        }
        for (int i = 0; i < 10; i++) dlg.history[i] = ntripHistory[i];
        if (dlg.exec() != QDialog::Accepted) return;
        for (int i = 0; i < NSTRLOG; i++) {
            strtype[IDXSTRLOG + i] = dlg.strType[i];
            strncpy(strpath[IDXSTRLOG + i], toLib(dlg.strPath[i]).constData(), MAXSTR - 1);
        }
        for (int i = 0; i < 10; i++) ntripHistory[i] = dlg.history[i];
        log("log streams updated");
    });
    connect(btnExit,    &QPushButton::clicked, this, &QWidget::close);
    connect(btnTimeSys, &QPushButton::clicked, this, &MainWindow::btnTimeSysClicked);
    connect(btnSolType, &QPushButton::clicked, this, &MainWindow::btnSolTypeClicked);
    connect(scbSol,     &QScrollBar::valueChanged, this, &MainWindow::scbSolChanged);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::timerTick);
    timer->start(TIMER_CYCLE);

    rtksvrinit(&svr);
    strinit(&moni);
    resetsysopts();
    getsysopts(&prcopt, solopt, &filopt);
    loadSettings();
    updatePos();
}
//------------------------------------------------------------------------------
QString MainWindow::defaultConfPath() const
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtknavi_qt");
    return QFileInfo(s.fileName()).absolutePath() + "/rtknavi_qt.conf";
}
//------------------------------------------------------------------------------
// restore conf (streams + processing options) and GUI state from last session
void MainWindow::loadSettings()
{
    {
        QSettings st(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtknavi_qt");
        moniPort = st.value("misc/moniport", moniPort).toInt();
    }
    QString conf = defaultConfPath();
    if (QFile::exists(conf)) {
        QByteArray confB = toLib(conf);
        if (loadopts(confB.constData(), rcvopts) &&
            loadopts(confB.constData(), sysopts)) {
            getsysopts(&prcopt, solopt, &filopt);
            fixSsrFromConf(conf, strssr);   // base-lib str2enum bug workaround
            log("settings restored: " + conf);
        }
    }
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtknavi_qt");
    restoreGeometry(s.value("window/geometry").toByteArray());
    if (!s.contains("window/geometry")) resize(1100, 560);
    timeSys = s.value("display/timesys", 0).toInt() % 4;
    solType = s.value("display/soltype", 0).toInt() % 5;
    static const char *tlabel[] = {"GPST", "UTC", "LT", "GPST"};
    btnTimeSys->setText(tlabel[timeSys]);
    panelTitle->setText(PANEL_TITLE[solType]);
    plot1->setState(s.value("plot1/type", 1).toInt(), s.value("plot1/freq", 0).toInt(),
                    s.value("plot1/trk", 0).toInt(),  s.value("plot1/scale", 5).toInt());
    plot2->setState(s.value("plot2/type", 6).toInt(), s.value("plot2/freq", 0).toInt(),
                    s.value("plot2/trk", 0).toInt(),  s.value("plot2/scale", 5).toInt());
    confFile = s.value("conf/lastfile").toString();
    QByteArray sp = s.value("window/splitter").toByteArray();
    if (!sp.isEmpty()) splitter->restoreState(sp);
    else splitter->setSizes({300, 380, 380});
    int size = s.beginReadArray("ntrip/history");
    for (int i = 0; i < size && i < 10; i++) {
        s.setArrayIndex(i);
        ntripHistory[i] = s.value("addr").toString();
    }
    s.endArray();
}
//------------------------------------------------------------------------------
// persist conf (streams + processing options) and GUI state
void MainWindow::saveSettings()
{
    {
        QSettings st(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtknavi_qt");
        st.setValue("misc/moniport", moniPort);
    }
    QString conf = defaultConfPath();
    QDir().mkpath(QFileInfo(conf).absolutePath());
    setsysopts(&prcopt, solopt, &filopt);
    QString comment = QString("rtknavi_qt settings (MALIB ver.%1 %2) - auto-saved")
                          .arg(VER_MALIB, PATCH_LEVEL_MALIB);
    QStringList kept = keepRawOpts(confFile.isEmpty() ? conf : confFile);
    saveopts(qPrintable(conf), "w", qPrintable(comment), rcvopts);
    saveopts(qPrintable(conf), "a", NULL, sysopts);
    restoreRawOpts(conf, kept);         // base-lib opt2str workaround

    QSettings s(QSettings::IniFormat, QSettings::UserScope, "MALIB", "rtknavi_qt");
    s.setValue("window/geometry", saveGeometry());
    s.setValue("display/timesys", timeSys);
    s.setValue("display/soltype", solType);
    s.setValue("plot1/type", plot1->type());
    s.setValue("plot1/freq", plot1->freq());
    s.setValue("plot1/trk",  plot1->trk());
    s.setValue("plot1/scale", plot1->scale());
    s.setValue("plot2/type", plot2->type());
    s.setValue("plot2/freq", plot2->freq());
    s.setValue("plot2/trk",  plot2->trk());
    s.setValue("plot2/scale", plot2->scale());
    s.setValue("conf/lastfile", confFile);
    s.setValue("window/splitter", splitter->saveState());
    s.beginWriteArray("ntrip/history");
    for (int i = 0; i < 10; i++) {
        s.setArrayIndex(i);
        s.setValue("addr", ntripHistory[i]);
    }
    s.endArray();
}
//------------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (svr.state) svrStop();
    closeMoni();
    saveSettings();
    QMainWindow::closeEvent(event);
}
//------------------------------------------------------------------------------
MainWindow::~MainWindow()
{
    if (svr.state) svrStop();
}
//------------------------------------------------------------------------------
void MainWindow::log(const QString &msg)
{
    message->setText(msg);
    statusBar()->showMessage(msg, 5000);
}
//------------------------------------------------------------------------------
void MainWindow::loadConf(const QString &file)
{
    resetsysopts();
    if (!loadopts(qPrintable(file), rcvopts) ||
        !loadopts(qPrintable(file), sysopts)) {
        log("conf read error: " + file);
        return;
    }
    getsysopts(&prcopt, solopt, &filopt);
    fixSsrFromConf(file, strssr);       // base-lib str2enum bug workaround
    confFile = file;
    log("conf loaded: " + file);
}
//------------------------------------------------------------------------------
void MainWindow::btnOptClicked()
{
    // out-solformat and outstrN-format are two names for one thing: svrStart
    // copies the per-stream format over solopt[].posf, exactly as the original
    // does.  The original never showed out-solformat, ours does (the Options
    // page is generated from the option table), so changing it there looked
    // like it did nothing.  Carry an edit here across to output stream 1.
    int posf0 = solopt[0].posf;

    OptDialog dlg(rcvopts, &prcopt, solopt, &filopt, this);
    dlg.confFile = confFile;
    dlg.ssrVar = strssr;
    if (dlg.exec() != QDialog::Accepted) {
        if (!dlg.confFile.isEmpty()) confFile = dlg.confFile;
        return;
    }
    if (!dlg.confFile.isEmpty()) confFile = dlg.confFile;
    if (solopt[0].posf != posf0) {
        strfmt[IDXSTROUT] = solopt[0].posf;
        log(QString("solution 1 output format set to %1")
                .arg(SOL_FMT_NAME[qBound(0, solopt[0].posf, 4)]));
    }
    log("processing options updated");
}
//------------------------------------------------------------------------------
// open input streams dialog: sync with the rtkrcv conf model statics
void MainWindow::btnInputStrClicked()
{
    InputStrDialog dlg(this);

    for (int i = 0; i < NSTRIN; i++) {
        dlg.strType[i] = strtype[IDXSTRIN + i];
        dlg.strPath[i] = fromLib(strpath[IDXSTRIN + i]);
        dlg.strFmt[i]  = strfmt[IDXSTRIN + i];
        dlg.strSsr[i]  = strssr[i];
        dlg.cmdFile[i] = rcvcmds[i];
        for (int j = 0; j < 3; j++) {
            dlg.cmds[i][j]   = rcvCmds[i][j];
            dlg.cmdEna[i][j] = rcvCmdEna[i][j];
        }
    }
    dlg.nmeaReq = nmeareq;
    for (int i = 0; i < 3; i++) dlg.nmeaPos[i] = nmeapos[i];
    for (int i = 0; i < 10; i++) dlg.history[i] = ntripHistory[i];

    if (dlg.exec() != QDialog::Accepted) return;

    for (int i = 0; i < NSTRIN; i++) {
        strtype[IDXSTRIN + i] = dlg.strType[i];
        strncpy(strpath[IDXSTRIN + i], toLib(dlg.strPath[i]).constData(), MAXSTR - 1);
        strfmt[IDXSTRIN + i] = dlg.strFmt[i];
        strssr[i] = dlg.strSsr[i];
        strncpy(rcvcmds[i], toLib(dlg.cmdFile[i]).constData(), MAXSTR - 1);
        for (int j = 0; j < 3; j++) {
            rcvCmds[i][j]   = dlg.cmds[i][j];
            rcvCmdEna[i][j] = dlg.cmdEna[i][j];
        }
    }
    nmeareq = dlg.nmeaReq;
    for (int i = 0; i < 3; i++) nmeapos[i] = dlg.nmeaPos[i];
    for (int i = 0; i < 10; i++) ntripHistory[i] = dlg.history[i];
    log("input streams updated");
}
//------------------------------------------------------------------------------
// RTK monitor: internals of the running server (modeless, own update timer)
void MainWindow::btnMonitorClicked()
{
    auto *dlg = new MonitorDialog(&svr, &moni, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlag(Qt::Window);
    dlg->show();
}
//------------------------------------------------------------------------------
void MainWindow::btnPlotClicked()
{
    // launch our own RTKPLOT-Qt from next to this executable; "rtkplot" alone
    // relies on a PATH entry that does not exist in the binary distribution
    QString exe = QDir(QCoreApplication::applicationDirPath())
                      .filePath("rtkplot_qt");
#ifdef WIN32
    exe += ".exe";
#endif
    if (!QFileInfo::exists(exe)) {
        QMessageBox::warning(this, windowTitle(),
            QString("RTKPLOT-Qt was not found.\n\nexpected: %1").arg(exe));
        return;
    }
    QStringList args;
    if (svr.state) {
        // feed the running solution through the monitor port, as the original
        // RTKNAVI does; open it on demand instead of refusing like the original
        if (!moni.port && !openMoni()) {
            QMessageBox::warning(this, windowTitle(),
                QString("Cannot open the monitor port (%1).").arg(moniPort));
            return;
        }
        args << "-p" << QString("tcpcli://localhost:%1").arg(moniPort)
             << "-t" << windowTitle() + " : RTKPLOT";
    }
    if (!QProcess::startDetached(exe, args)) {
        QMessageBox::warning(this, windowTitle(),
            QString("Cannot start RTKPLOT-Qt.\n\n%1").arg(exe));
    }
}
//------------------------------------------------------------------------------
// Reopen a serial input that has gone quiet.
//
// Working around a defect in the base library (BASELIB_ISSUES #4): when the
// device disappears, readserial() only returns 0 -- it never sets
// serial->error -- so stateserial() keeps reporting 2 (OK) and nothing
// upstream can tell that the port died.  There is also no reconnect for
// serial, unlike tcp.  So detect it here by the byte count standing still and
// reopen the port, which is what the tester had to do by hand with Stop/Start.
void MainWindow::checkSerialStreams()
{
    if (!svr.state) return;

    uint32_t tick = tickget();
    for (int i = 0; i < NSTRIN; i++) {
        if (strtype[IDXSTRIN + i] != STR_SERIAL) continue;

        int inb = 0, inr = 0;
        strsum(&svr.stream[i], &inb, &inr, NULL, NULL);

        if (inb != serialInB[i] || !serialTick[i]) {   // still flowing
            if (inb > serialInB[i]) serialSeen[i] = true;
            serialInB[i] = inb;
            serialTick[i] = tick;
            continue;
        }
        // only rescue a port that was working: a silent port that never
        // delivered anything is a wrong setting, and reopening it forever
        // would just fill the log
        if (!serialSeen[i]) continue;
        if ((int)(tick - serialTick[i]) < timeout) continue;
        serialTick[i] = tick;                          // do not retry every cycle

        log(QString("input %1: no data for %2 s, reopening %3")
                .arg(i + 1).arg(timeout / 1000).arg(fromLib(strpath[IDXSTRIN + i])));

        // The server thread calls strread() on this stream outside
        // rtksvrlock(), and strread() tests stream->port before taking the
        // stream lock, so closing it from here would leave a window where the
        // thread passes that test and then reads a freed port.  Shut the
        // window with the stream's own lock: clear the read mode under it, so
        // whoever is already waiting for the lock still sees the old, valid
        // port, and anyone arriving later gives up at the mode test.  Once the
        // in-flight read has finished nothing can enter, and the close is safe.
        stream_t *str = &svr.stream[i];
        strlock(str);
        str->mode &= ~STR_MODE_R;
        strunlock(str);
        sleepms(100);                       // let an in-flight read finish

        strclose(str);
        int ok = stropen(str, STR_SERIAL, STR_MODE_R, strpath[IDXSTRIN + i]);

        log(ok ? QString("input %1: reopened").arg(i + 1)
               : QString("input %1: reopen failed").arg(i + 1));
        if (ok) serialInB[i] = 0;
    }
}
//------------------------------------------------------------------------------
// monitor port: a TCP server carrying the solution, same as rtkrcv -m.
// rtksvrstart() was already given &moni, and it only writes through that
// pointer, so the stream can be opened later than the server.
bool MainWindow::openMoni()
{
    if (moniPort <= 0) return false;
    QString path = QString(":%1").arg(moniPort);
    if (!stropen(&moni, STR_TCPSVR, STR_MODE_RW, qPrintable(path))) return false;
    strsettimeout(&moni, timeout, reconnect);

    if (!keepAlive) {
        keepAlive = new QTimer(this);
        connect(keepAlive, &QTimer::timeout, this,
                [] { strwrite(&moni, (uint8_t *)"\r", 1); });
    }
    keepAlive->start(INTKEEPALIVE);
    log(QString("monitor port opened (%1)").arg(moniPort));
    return true;
}
//------------------------------------------------------------------------------
void MainWindow::closeMoni()
{
    if (!moni.port) return;
    if (keepAlive) keepAlive->stop();
    strwrite(&moni, (uint8_t *)MSG_DISCONN, (int)strlen(MSG_DISCONN));
    strclose(&moni);
    moni = stream_t{};
}
//------------------------------------------------------------------------------
void MainWindow::btnStartClicked()
{
    int i, nin = 0;
    for (i = 0; i < NSTRIN; i++) if (strtype[IDXSTRIN + i] != STR_NONE) nin++;
    if (nin == 0) {
        QMessageBox::warning(this, "RTKNAVI-Qt",
            "no input stream: configure streams (I) or load a conf (Options... > Load)");
        return;
    }
    if (svrStart()) {
        btnStart->setVisible(false);
        btnStop->setVisible(true);
        btnOpt->setEnabled(false);
        btnInput->setEnabled(false);
        scbSol->setEnabled(false);
        indSvr->setStyleSheet("background:orange;border:1px solid #aaa;");
    }
}
//------------------------------------------------------------------------------
void MainWindow::btnStopClicked()
{
    svrStop();
    btnStart->setVisible(true);
    btnStop->setVisible(false);
    btnOpt->setEnabled(true);
    btnInput->setEnabled(true);
    scbSol->setEnabled(true);
    updateScb();
    updatePos();
}
//------------------------------------------------------------------------------
void MainWindow::btnTimeSysClicked()
{
    static const char *label[] = {"GPST", "UTC", "LT", "GPST"};
    timeSys = (timeSys + 1) % 4;
    btnTimeSys->setText(label[timeSys]);
    updateTime();
}
//------------------------------------------------------------------------------
void MainWindow::btnSolTypeClicked()
{
    solType = (solType + 1) % 5;
    panelTitle->setText(PANEL_TITLE[solType]);
    updatePos();
}
//------------------------------------------------------------------------------
void MainWindow::scbSolChanged(int value)
{
    if (scbLock) return;
    navi.pSol = (navi.pSolS + value) % SOLBUF_SIZE;
    updateTime();
    updatePos();
}
//------------------------------------------------------------------------------
// port of rtkrcv.c startsvr() (vt console replaced by GUI log)
bool MainWindow::svrStart()
{
    static sta_t sta[MAXRCV]={{""}};
    double pos[3],npos[3];
    static char s1[NSTRIN][MAXRCVCMD],s2[NSTRIN][MAXRCVCMD];
    char *cmds[NSTRIN]={NULL},*cmds_periodic[NSTRIN]={NULL};
    char *ropts[NSTRIN]={(char *)"",(char *)"",(char *)"",(char *)""};
    char *paths[MAXSTRRTK];
    char errmsg[2048]="";
    int i,ret,stropt[8]={0};

    for (i=0;i<NSTRIN;i++) {
        s1[i][0]=s2[i][0]='\0';
    }
    for (i=0;i<MAXSTRRTK;i++) {
        paths[i]=strpath[i];
    }
    /* read start commands from command files */
    for (i=0;i<NSTRIN;i++) {
        // commands edited in the dialog take precedence over the command file
        if (rcvCmdEna[i][0]&&!rcvCmds[i][0].isEmpty()) {
            strncpy(s1[i],qPrintable(rcvCmds[i][0]),MAXRCVCMD-1);
            cmds[i]=s1[i];
        }
        if (rcvCmdEna[i][2]&&!rcvCmds[i][2].isEmpty()) {
            strncpy(s2[i],qPrintable(rcvCmds[i][2]),MAXRCVCMD-1);
            cmds_periodic[i]=s2[i];
        }
        if (!*rcvcmds[i]) continue;
        if (!cmds[i]) {
            if (!readcmd(rcvcmds[i],s1[i],0)) log(QString("no command file: %1").arg(rcvcmds[i]));
            else cmds[i]=s1[i];
        }
        if (!cmds_periodic[i]) {
            if (!readcmd(rcvcmds[i],s2[i],2)) log(QString("no command file: %1").arg(rcvcmds[i]));
            else cmds_periodic[i]=s2[i];
        }
    }
    /* confirm overwrite */
    for (i=IDXSTROUT;i<MAXSTRRTK;i++) {
        if (strtype[i]!=STR_FILE) continue;
        FILE *fp=fopen(strpath[i],"r");
        if (!fp) continue;
        fclose(fp);
        if (QMessageBox::question(this,"RTKNAVI-Qt",
                QString("overwrite %1 ?").arg(fromLib(strpath[i])))!=QMessageBox::Yes) return false;
    }
    if (prcopt.refpos==4) { /* rtcm */
        for (i=0;i<3;i++) prcopt.rb[i]=0.0;
    }
    pos[0]=nmeapos[0]*D2R;
    pos[1]=nmeapos[1]*D2R;
    pos[2]=nmeapos[2];
    pos2ecef(pos,npos);

    /* read antenna file (rtkrcv.c readant() with GUI log) */
    {
        const pcv_t pcv0={0};
        pcvs_t pcvr={0},pcvs={0};
        pcv_t *pcv;
        gtime_t time=timeget();

        prcopt.pcvr[0]=prcopt.pcvr[1]=pcv0;
        if (*filopt.rcvantp) {
            if (readpcv(filopt.rcvantp,&pcvr)) {
                for (i=0;i<2;i++) {
                    if (!*prcopt.anttype[i]) continue;
                    if (!(pcv=searchpcv(0,prcopt.anttype[i],time,&pcvr))) {
                        log(QString("no antenna %1 in %2").arg(prcopt.anttype[i],filopt.rcvantp));
                        continue;
                    }
                    copypcv(pcv,&prcopt.pcvr[i]);
                }
            }
            else log(QString("antenna file open error %1").arg(filopt.rcvantp));
            if (readpcv(filopt.satantp,&pcvs)) {
                for (i=0;i<MAXSAT;i++) {
                    if (!(pcv=searchpcv(i+1,"",time,&pcvs))) continue;
                    copypcv(pcv,&svr.nav.pcvs[i]);
                }
            }
            else log(QString("antenna file open error %1").arg(filopt.satantp));
            freepcv(&pcvr); freepcv(&pcvs);
        }
    }
    /* read dcb file */
    if (*filopt.dcb) {
        strncpy(sta[0].name,sta_name,sizeof(sta[0].name)-1);
        readdcb(filopt.dcb,&svr.nav,sta);
    }
    /* open geoid data file */
    if (solopt[0].geoid>0&&!opengeoid(solopt[0].geoid,filopt.geoid)) {
        log(QString("geoid data open error: %1").arg(filopt.geoid));
    }
    /* read mask file */
    if (*filopt.mask) {
        readmask(filopt.mask,&prcopt);
    }
    /* read exclusion satellites file */
    if (*filopt.exclsatsfile) {
        read_exclsats(filopt.exclsatsfile);
    }
    /* set stream options */
    stropt[0]=timeout;
    stropt[1]=reconnect;
    stropt[2]=1000;
    stropt[3]=buffsize;
    stropt[4]=fswapmargin;
    strsetopt(stropt);

    if (strfmt[2]==8) strfmt[2]=STRFMT_SP3;

    /* set ftp/http directory and proxy */
    strsetdir(filopt.tempdir);
    strsetproxy(proxyaddr);

    /* execute start command */
    if (*startcmd&&(ret=system(startcmd))) {
        log(QString("command exec error: %1 (%2)").arg(startcmd).arg(ret));
    }
    solopt[0].posf=strfmt[IDXSTROUT];
    solopt[1].posf=strfmt[IDXSTROUT+1];

    for (i=0;i<NSTRIN;i++) { serialInB[i]=0; serialTick[i]=0; serialSeen[i]=false; }

    /* reset solution ring buffer and track origin (VCL SvrStart) */
    navi.pSol=navi.pSolS=navi.pSolE=0;
    navi.solStat[0]=navi.nvsat[0]=0;
    for (i=0;i<3;i++) navi.trkOri[i]=0.0;
    navi.solCurrentStat=0;

    /* start rtk server */
    if (!rtksvrstart(&svr,svrcycle,buffsize,strtype,paths,strfmt,strssr,navmsgsel,
                     cmds,cmds_periodic,ropts,nmeacycle,nmeareq,npos,maxdrtr,&prcopt,
                     solopt,&moni,rst,errmsg,&filopt)) {
        log(QString("rtk server start error (%1)").arg(errmsg));
        return false;
    }
    log("rtk server started");
    return true;
}
//------------------------------------------------------------------------------
// port of rtkrcv.c stopsvr()
void MainWindow::svrStop()
{
    static char s[NSTRIN][MAXRCVCMD];
    char *cmds[NSTRIN]={NULL};
    int i,ret;

    if (!svr.state) return;

    for (i=0;i<NSTRIN;i++) {
        s[i][0]='\0';
        if (rcvCmdEna[i][1]&&!rcvCmds[i][1].isEmpty()) {
            strncpy(s[i],qPrintable(rcvCmds[i][1]),MAXRCVCMD-1);
            cmds[i]=s[i];
            continue;
        }
        if (!*rcvcmds[i]) continue;
        if (readcmd(rcvcmds[i],s[i],1)) cmds[i]=s[i];
    }
    rtksvrstop(&svr,cmds);
    closeMoni();

    /* execute stop command */
    if (*stopcmd&&(ret=system(stopcmd))) {
        log(QString("command exec error: %1 (%2)").arg(stopcmd).arg(ret));
    }
    log("rtk server stopped");
}
//------------------------------------------------------------------------------
// port of VCL UpdateLog(): store a solution into the ring buffer
void MainWindow::updateLog(int stat, gtime_t time, double *rr, float *qr,
                           double *rb, int ns, double age, double ratio)
{
    if (!stat) return;

    int i, e = navi.pSolE;
    navi.solStat[e]=stat; navi.time[e]=time; navi.nvsat[e]=ns;
    navi.age[e]=age; navi.ratio[e]=ratio;
    for (i=0;i<3;i++) {
        navi.solRov[i+e*3]=rr[i];
        navi.solRef[i+e*3]=rb[i];
        navi.velRov[i+e*3]=rr[i+3];
    }
    navi.qr[  e*9]=qr[0];
    navi.qr[4+e*9]=qr[1];
    navi.qr[8+e*9]=qr[2];
    navi.qr[1+e*9]=navi.qr[3+e*9]=qr[3];
    navi.qr[5+e*9]=navi.qr[7+e*9]=qr[4];
    navi.qr[2+e*9]=navi.qr[6+e*9]=qr[5];

    navi.pSol=navi.pSolE;
    if (++navi.pSolE>=SOLBUF_SIZE) navi.pSolE=0;
    if (navi.pSolE==navi.pSolS&&++navi.pSolS>=SOLBUF_SIZE) navi.pSolS=0;
}
//------------------------------------------------------------------------------
// port of VCL UpdateTime()
void MainWindow::updateTime()
{
    gtime_t time=navi.time[navi.pSol];
    struct tm *t;
    double tow;
    int week;
    char tstr[64]="2000/01/01 00:00:00.0";

    if (time.time) {
        if      (timeSys==0) time2str(time,tstr,1);
        else if (timeSys==1) time2str(gpst2utc(time),tstr,1);
        else if (timeSys==2) {
            time=gpst2utc(time);
            if (!(t=localtime(&time.time))) strcpy(tstr,"2000/01/01 00:00:00.0");
            else sprintf(tstr,"%04d/%02d/%02d %02d:%02d:%02d.%d",t->tm_year+1900,
                         t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec,
                         (int)(time.sec*10));
        }
        else {
            tow=time2gpst(time,&week); sprintf(tstr,"week %04d %8.1f s",week,tow);
        }
    }
    labelTime->setText(tstr);
}
//------------------------------------------------------------------------------
// port of VCL UpdatePos()
void MainWindow::updatePos()
{
    QString s[9],ext="";
    double *rr=navi.solRov+navi.pSol*3,*rb=navi.solRef+navi.pSol*3;
    double *qr=navi.qr+navi.pSol*9,pos[3]={0},Qe[9]={0};
    double dms1[3]={0},dms2[3]={0},bl[3]={0},enu[3]={0},pitch=0.0,yaw=0.0,len;
    int i,stat=navi.solStat[navi.pSol];

    if (svr.rtk.opt.mode==PMODE_STATIC||svr.rtk.opt.mode==PMODE_PPP_STATIC) {
        ext=" (S)";
    }
    else if (svr.rtk.opt.mode==PMODE_FIXED||svr.rtk.opt.mode==PMODE_PPP_FIXED) {
        ext=" (F)";
    }
    labelSolLabel->setText("Solution"+ext+":");
    // MALIB: show "PPP-FIX" when a PPP mode reaches a fixed solution
    if ((svr.rtk.opt.mode==PMODE_PPP_KINEMA||svr.rtk.opt.mode==PMODE_PPP_STATIC||
         svr.rtk.opt.mode==PMODE_PPP_FIXED)&&stat==SOLQ_FIX) {
        labelSol->setText("PPP-FIX");
    } else {
        labelSol->setText(SOL_STR[stat<=7?stat:0]);
    }
    labelSol->setStyleSheet(QString("color:%1;background:white;")
                                .arg(svr.state?SOL_CSS[stat<=7?stat:0]:"gray"));
    indSol->setStyleSheet(QString("background:%1;border:1px solid #999;border-radius:7px;")
                              .arg(svr.state&&stat?SOL_CSS[stat<=7?stat:0]:"white"));
    if (norm(rr,3)>0.0&&norm(rb,3)>0.0) {
        for (i=0;i<3;i++) bl[i]=rr[i]-rb[i];
    }
    len=norm(bl,3);
    if (solType==0) {
        if (norm(rr,3)>0.0) {
            ecef2pos(rr,pos); covenu(pos,qr,Qe);
            deg2dms(pos[0]*R2D,dms1,4);
            deg2dms(pos[1]*R2D,dms2,4);
            if (solopt[0].height==1) pos[2]-=geoidh(pos); /* geodetic */
        }
        s[0]=pos[0]<0?"S:":"N:"; s[1]=pos[1]<0?"W:":"E:";
        s[2]=solopt[0].height==1?"H:":"He:";
        s[3]=QString::asprintf("%.0f",fabs(dms1[0]))+CHARDEG+
             QString::asprintf(" %02.0f' %07.4f\"",dms1[1],dms1[2]);
        s[4]=QString::asprintf("%.0f",fabs(dms2[0]))+CHARDEG+
             QString::asprintf(" %02.0f' %07.4f\"",dms2[1],dms2[2]);
        s[5]=QString::asprintf("%.3f m",pos[2]);
        s[6]=QString::asprintf("N:%6.3f E:%6.3f U:%6.3f m",SQRT(Qe[4]),SQRT(Qe[0]),SQRT(Qe[8]));
    }
    else if (solType==1) {
        if (norm(rr,3)>0.0) {
            ecef2pos(rr,pos); covenu(pos,qr,Qe);
            if (solopt[0].height==1) pos[2]-=geoidh(pos); /* geodetic */
        }
        s[0]=pos[0]<0?"S:":"N:"; s[1]=pos[1]<0?"W:":"E:";
        s[2]=solopt[0].height==1?"H:":"He:";
        s[3]=QString::asprintf("%.8f ",fabs(pos[0])*R2D)+CHARDEG;
        s[4]=QString::asprintf("%.8f ",fabs(pos[1])*R2D)+CHARDEG;
        s[5]=QString::asprintf("%.3f m",pos[2]);
        s[6]=QString::asprintf("E:%6.3f N:%6.3f U:%6.3f m",SQRT(Qe[0]),SQRT(Qe[4]),SQRT(Qe[8]));
    }
    else if (solType==2) {
        s[0]="X:"; s[1]="Y:"; s[2]="Z:";
        s[3]=QString::asprintf("%.3f m",rr[0]);
        s[4]=QString::asprintf("%.3f m",rr[1]);
        s[5]=QString::asprintf("%.3f m",rr[2]);
        s[6]=QString::asprintf("X:%6.3f Y:%6.3f Z:%6.3f m",SQRT(qr[0]),SQRT(qr[4]),SQRT(qr[8]));
    }
    else if (solType==3) {
        if (len>0.0) {
            ecef2pos(rb,pos); ecef2enu(pos,bl,enu); covenu(pos,qr,Qe);
        }
        s[0]="E:"; s[1]="N:"; s[2]="U:";
        s[3]=QString::asprintf("%.3f m",enu[0]);
        s[4]=QString::asprintf("%.3f m",enu[1]);
        s[5]=QString::asprintf("%.3f m",enu[2]);
        s[6]=QString::asprintf("E:%6.3f N:%6.3f U:%6.3f m",SQRT(Qe[0]),SQRT(Qe[4]),SQRT(Qe[8]));
    }
    else {
        if (len>0.0) {
            ecef2pos(rb,pos); ecef2enu(pos,bl,enu); covenu(pos,qr,Qe);
            pitch=asin(enu[2]/len);
            yaw=atan2(enu[0],enu[1]); if (yaw<0.0) yaw+=2.0*PI;
        }
        s[0]="P:"; s[1]="Y:"; s[2]="L:";
        s[3]=QString::asprintf("%.3f ",pitch*R2D)+CHARDEG;
        s[4]=QString::asprintf("%.3f ",yaw*R2D)+CHARDEG;
        s[5]=QString::asprintf("%.3f m",len);
        s[6]=QString::asprintf("E:%6.3f N:%6.3f U:%6.3f m",SQRT(Qe[0]),SQRT(Qe[4]),SQRT(Qe[8]));
    }
    s[7]=QString::asprintf("Age:%4.1f s Ratio:%4.1f #Sat:%2d",
                           navi.age[navi.pSol],navi.ratio[navi.pSol],navi.nvsat[navi.pSol]);

    for (i=0;i<3;i++) {
        plabel[i]->setText(s[i]);
        posLabel[i]->setText(s[i+3]);
        posLabel[i]->setStyleSheet(QString("background:white;color:%1;")
            .arg(prcopt.mode==PMODE_MOVEB&&solType<=2?"gray":"black"));
    }
    labelStd->setText(s[6]);
    labelNSat->setText(s[7]);
}
//------------------------------------------------------------------------------
// port of VCL UpdateStr()
void MainWindow::updateStr()
{
    static const char *css[]={
        "background:#e05050;border:1px solid #aaa;",   /* -1: error   */
        "background:white;border:1px solid #aaa;",     /*  0: closed  */
        "background:#ffd050;border:1px solid #aaa;",   /*  1: waiting */
        "background:#309030;border:1px solid #aaa;",   /*  2: connect */
        "background:#40e040;border:1px solid #aaa;",   /*  3: active  */
    };
    int i,sstat[MAXSTRRTK]={0};
    char msg[MAXSTRMSG]="";

    rtksvrsstat(&svr,sstat,msg);
    for (i=0;i<MAXSTRRTK;i++) {
        int idx=sstat[i]+1;
        indStr[i]->setStyleSheet(css[idx<0?0:(idx>4?4:idx)]);
    }
    if (*msg) message->setText(fromLib(msg));
}
//------------------------------------------------------------------------------
// fetch observation data for plots (VCL DrawPlot head) and repaint
void MainWindow::updatePlots()
{
    gtime_t time;
    int i,j,k,ns[2],sat[2][MAXSAT],vsat[2][MAXSAT],snr[2][MAXSAT][NFREQ];
    double az[2][MAXSAT],el[2][MAXSAT];
    int *snr0[MAXSAT],*snr1[MAXSAT];

    for (i=0;i<MAXSAT;i++) {
        snr0[i]=snr[0][i];
        snr1[i]=snr[1][i];
    }
    ns[0]=rtksvrostat(&svr,0,&time,sat[0],az[0],el[0],snr0,vsat[0]);
    ns[1]=rtksvrostat(&svr,1,&time,sat[1],az[1],el[1],snr1,vsat[1]);

    for (i=0;i<2;i++) {
        if (ns[i]>0) {
            navi.nsat[i]=ns[i];
            for (j=0;j<ns[i];j++) {
                navi.sat [i][j]=sat [i][j];
                navi.az  [i][j]=az  [i][j];
                navi.el  [i][j]=el  [i][j];
                for (k=0;k<NFREQ;k++) navi.snr[i][j][k]=snr[i][j][k];
                navi.vsat[i][j]=vsat[i][j];
            }
        }
        else {
            for (j=0;j<navi.nsat[i];j++) {
                navi.vsat[i][j]=0;
                for (k=0;k<NFREQ;k++) navi.snr[i][j][k]=0;
            }
        }
    }
    plot1->update();
    plot2->update();
}
//------------------------------------------------------------------------------
void MainWindow::updateScb()
{
    int count=(navi.pSolE-navi.pSolS+SOLBUF_SIZE)%SOLBUF_SIZE;
    scbLock=true;
    scbSol->setRange(0,count>0?count-1:0);
    scbSol->setValue((navi.pSol-navi.pSolS+SOLBUF_SIZE)%SOLBUF_SIZE);
    scbLock=false;
}
//------------------------------------------------------------------------------
// port of VCL TimerTimer()
void MainWindow::timerTick()
{
    sol_t *sol;
    int i,update=0;

    timerCycle++;
    checkSerialStreams();

    rtksvrlock(&svr);
    for (i=0;i<svr.nsol;i++) {
        sol=svr.solbuf+i;
        updateLog(sol->stat,sol->time,sol->rr,sol->qr,svr.rtk.rb,sol->ns,
                  sol->age,sol->ratio);
        if (sol->stat) update=1;
    }
    svr.nsol=0;
    navi.solCurrentStat=svr.state?svr.rtk.sol.stat:0;
    rtksvrunlock(&svr);

    if (update) {
        updateTime();
        updatePos();
        updateScb();
        timerInact=0;
    }
    else {
        if (++timerInact*TIMER_CYCLE>TIMEOUT_SOL) navi.solCurrentStat=0;
    }
    if (navi.solCurrentStat) {
        indSvr->setStyleSheet("background:#40e040;border:1px solid #aaa;");
    }
    else {
        indSvr->setStyleSheet(svr.state
            ?"background:#309030;border:1px solid #aaa;"
            :"background:white;border:1px solid #aaa;");
    }
    if (!(timerCycle%5)) updatePlots();
    updateStr();
}
//------------------------------------------------------------------------------
