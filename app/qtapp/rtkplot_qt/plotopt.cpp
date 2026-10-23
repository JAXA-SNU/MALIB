//------------------------------------------------------------------------------
// plotopt.cpp : plot options for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotopt (RTKLIB 2.4.3 b35).  The controls are
// laid out like the original dialog; the ones belonging to plot types that are
// not implemented yet (observation / sky / SNR-MP / map / TLE) are shown but
// disabled so that it is obvious what does and does not take effect.
//
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>

#include "appstyle.h"
#include "plotopt.h"
#include "rtklib.h"

// the disabled controls belong to plot types that are not ported yet
#define TIP_NOTYET  "not effective yet: needs the observation / sky plot types"

//------------------------------------------------------------------------------
PlotOpt::PlotOpt()
{
    // default mark colours, same as the VCL original (TColor is BGR there)
    const QColor m0[8] = {
        QColor(192,192,192), QColor(  0,128,  0), QColor(255,170,  0),
        QColor(255,  0,255), QColor(  0,  0,255), QColor(255,  0,  0),
        QColor(  0,128,128), QColor(128,128,128)
    };
    const QColor m1[8] = {
        QColor(192,192,192), QColor(  0, 64,128), QColor(128,128,  0),
        QColor(128,  0,255), QColor(  0,128,255), QColor(255,128,128),
        QColor(128,128,255), QColor(128,128,128)
    };
    for (int i = 0; i < 8; i++) { mColor[0][i] = m0[i]; mColor[1][i] = m1[i]; }
    cColor[0] = Qt::white;                          // background
    cColor[1] = QColor(192,192,192);                // grid
    cColor[2] = Qt::black;                          // text
    cColor[3] = QColor(192,192,192);                // line

    navSys = SYS_GPS | SYS_GLO | SYS_GAL | SYS_QZS;

    // QFont defaults to the application font; make sure it carries a point
    // size so that the Font button and the saved string are meaningful
    if (font.pointSize() <= 0) font.setPointSize(9);
}
//------------------------------------------------------------------------------
void PlotOpt::load(QSettings &s)
{
    s.beginGroup("plot");
    timeFormat   = s.value("timeform",   timeFormat  ).toInt();
    latLonFormat = s.value("latlonform", latLonFormat).toInt();
    showStats    = s.value("showstats",  showStats   ).toInt();
    showArrow    = s.value("showarrow",  showArrow   ).toInt();
    showErr      = s.value("showerr",    showErr     ).toInt();
    showGLabel   = s.value("showglabel", showGLabel  ).toInt();
    showScale    = s.value("showscale",  showScale   ).toInt();
    showCompass  = s.value("showcompass",showCompass ).toInt();
    showGrid     = s.value("showgrid",   showGrid    ).toInt();
    autoScale    = s.value("autoscale",  autoScale   ).toInt();
    yRange       = s.value("yrange",     yRange      ).toDouble();
    plotStyle    = s.value("plotstyle",  plotStyle   ).toInt();
    markSize     = s.value("marksize",   markSize    ).toInt();
    if (s.contains("font")) font.fromString(s.value("font").toString());

    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 8; i++)
            mColor[k][i] = QColor(s.value(QString("mcolor%1").arg(k*8+i),
                                          mColor[k][i].name()).toString());
    for (int i = 0; i < 4; i++)
        cColor[i] = QColor(s.value(QString("ccolor%1").arg(i),
                                   cColor[i].name()).toString());

    origin  = s.value("origin", origin).toInt();
    for (int i = 0; i < 3; i++)
        ooPos[i] = s.value(QString("oopos%1").arg(i), ooPos[i]).toDouble();

    rtBuffSize = s.value("rtbuffsize", rtBuffSize).toInt();
    refCycle   = s.value("refcycle",   refCycle  ).toInt();

    elMask   = s.value("elmask",  elMask ).toDouble();
    maxDop   = s.value("maxdop",  maxDop ).toDouble();
    maxMP    = s.value("maxmp",   maxMP  ).toDouble();
    cycleSlip= s.value("cycslip", cycleSlip).toInt();
    showEph  = s.value("showeph", showEph).toInt();
    showHalfC= s.value("showhalfc", showHalfC).toInt();
    hideLowSat = s.value("hidelowsat", hideLowSat).toInt();
    navSys   = s.value("navsys",  navSys ).toInt();
    rcvPos   = s.value("rcvpos",  rcvPos ).toInt();
    animCycle= s.value("animcycle", animCycle).toInt();
    timeSyncOut  = s.value("timesyncout",  timeSyncOut ).toInt();
    timeSyncPort = s.value("timesyncport", timeSyncPort).toInt();
    excSats   = s.value("excsats",   excSats  ).toString();
    rnxOpts   = s.value("rnxopts",   rnxOpts  ).toString();
    shapeFile = s.value("shapefile", shapeFile).toString();
    tleFile   = s.value("tlefile",   tleFile  ).toString();
    tleSatFile= s.value("tlesatfile",tleSatFile).toString();
    s.endGroup();
}
//------------------------------------------------------------------------------
void PlotOpt::save(QSettings &s) const
{
    s.beginGroup("plot");
    s.setValue("timeform",   timeFormat);
    s.setValue("latlonform", latLonFormat);
    s.setValue("showstats",  showStats);
    s.setValue("showarrow",  showArrow);
    s.setValue("showerr",    showErr);
    s.setValue("showglabel", showGLabel);
    s.setValue("showscale",  showScale);
    s.setValue("showcompass",showCompass);
    s.setValue("showgrid",   showGrid);
    s.setValue("autoscale",  autoScale);
    s.setValue("yrange",     yRange);
    s.setValue("plotstyle",  plotStyle);
    s.setValue("marksize",   markSize);
    s.setValue("font",       font.toString());

    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 8; i++)
            s.setValue(QString("mcolor%1").arg(k*8+i), mColor[k][i].name());
    for (int i = 0; i < 4; i++)
        s.setValue(QString("ccolor%1").arg(i), cColor[i].name());

    s.setValue("origin", origin);
    for (int i = 0; i < 3; i++) s.setValue(QString("oopos%1").arg(i), ooPos[i]);

    s.setValue("rtbuffsize", rtBuffSize);
    s.setValue("refcycle",   refCycle);

    s.setValue("elmask",  elMask);
    s.setValue("maxdop",  maxDop);
    s.setValue("maxmp",   maxMP);
    s.setValue("cycslip", cycleSlip);
    s.setValue("showeph", showEph);
    s.setValue("showhalfc", showHalfC);
    s.setValue("hidelowsat", hideLowSat);
    s.setValue("navsys",  navSys);
    s.setValue("rcvpos",  rcvPos);
    s.setValue("animcycle", animCycle);
    s.setValue("timesyncout",  timeSyncOut);
    s.setValue("timesyncport", timeSyncPort);
    s.setValue("excsats",   excSats);
    s.setValue("rnxopts",   rnxOpts);
    s.setValue("shapefile", shapeFile);
    s.setValue("tlefile",   tleFile);
    s.setValue("tlesatfile",tleSatFile);
    s.endGroup();
}
//------------------------------------------------------------------------------
static QComboBox *combo(const QStringList &items, int minw = 110)
{
    auto *c = new QComboBox();
    c->addItems(items);
    c->setMinimumWidth(minw);
    return c;
}
static QLineEdit *edit(int minw = 110)
{
    auto *e = new QLineEdit();
    e->setMinimumWidth(minw);
    return e;
}
static void addRow(QGridLayout *g, int row, const QString &label, QWidget *w)
{
    g->addWidget(new QLabel(label), row, 0);
    g->addWidget(w, row, 1);
}
static void setSwatch(QPushButton *b, const QColor &c)
{
    b->setStyleSheet(QString("background-color:%1;border:1px solid #808080;")
                         .arg(c.name()));
}
//------------------------------------------------------------------------------
PlotOptDialog::PlotOptDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Options");

    // ---- column 1 ----------------------------------------------------------
    auto *g1 = new QGridLayout();
    cTimeForm  = combo({"ww ssss GPST", "h:m:s GPST", "h:m:s UTC", "h:m:s JST"});
    cLatLonForm= combo({"ddd.dddddd", "ddd mm ss.ss"});
    cStats     = combo({"OFF", "ON"});
    cCycSlip   = combo({"OFF", "LLI Flag", "Poly Fit"});
    cHalfC     = combo({"OFF", "ON"});
    cEph       = combo({"OFF", "ON"});
    cElMask    = combo({"0", "5", "10", "15", "20", "30", "45"});
    cElMask->setEditable(true);
    cElMaskP   = combo({"OFF", "ON"});
    cHideLow   = combo({"OFF", "ON"});
    cMaxDop    = combo({"10", "20", "30", "50", "100"});
    cMaxDop->setEditable(true);
    cMaxMP     = combo({"1", "5", "10", "20", "50"});
    cMaxMP->setEditable(true);
    cRcvPos    = combo({"RINEX Header Position", "Average of Single Position",
                        "Lat/Lon/Hgt in Options"});
    int r = 0;
    addRow(g1, r++, "Time Format",        cTimeForm);
    addRow(g1, r++, "Lat/Lon Format",     cLatLonForm);
    addRow(g1, r++, "Show Statistics",    cStats);
    addRow(g1, r++, "Cycle-Slip",         cCycSlip);
    addRow(g1, r++, "Parity Unknown",     cHalfC);
    addRow(g1, r++, "Ephemeris",          cEph);
    addRow(g1, r++, "Elevation Mask (°)", cElMask);
    addRow(g1, r++, "Elev Mask Pattern",  cElMaskP);
    addRow(g1, r++, "Hide Low Satellite", cHideLow);
    addRow(g1, r++, "Max NSAT/DOP",       cMaxDop);
    addRow(g1, r++, "Max Multipath",      cMaxMP);
    addRow(g1, r++, "Receiver Position",  cRcvPos);

    auto *gSys = new QGroupBox("Satellite System");
    auto *lSys = new QGridLayout(gSys);
    static const char *sysName[6] = {"GPS", "GLO", "GAL", "QZS", "BDS", "SBS"};
    for (int i = 0; i < 6; i++) {
        kSys[i] = new QCheckBox(sysName[i]);
        lSys->addWidget(kSys[i], i/4, i%4);
    }
    eExcSats  = edit(150);
    ePlotOpts = edit(150);

    auto *v1 = new QVBoxLayout();
    v1->addLayout(g1);
    v1->addWidget(gSys);
    auto *g1b = new QGridLayout();
    addRow(g1b, 0, "Excluded Sats\n(+Sn: Included)", eExcSats);
    addRow(g1b, 1, "Plot Options", ePlotOpts);
    v1->addLayout(g1b);
    v1->addStretch();

    // ---- column 2 ----------------------------------------------------------
    auto *g2 = new QGridLayout();
    cErr      = combo({"OFF", "Bar/Circle", "Dot"});
    cArrow    = combo({"OFF", "ON"});
    cGLabel   = combo({"OFF", "ON", "ON + Axis Label"});
    cGrid     = combo({"OFF", "Grid", "Grid + Label"});
    cCompass  = combo({"OFF", "ON"});
    cScale    = combo({"OFF", "ON"});
    cAutoScale= combo({"OFF", "ON"});
    cYRange   = combo({"0.02m", "0.05m", "0.1m", "0.2m", "0.5m", "1m", "2m",
                       "5m", "10m", "20m", "50m", "100m"});
    cYRange->setEditable(true);
    eBuffSize = edit();
    kTimeSync = new QCheckBox("Time Sync Port");
    eSyncPort = edit(80);
    cOrigin   = combo({"Start Position", "End Position", "Average Position",
                       "Linear Fit Position", "Base Station Position",
                       "Lat/Lon/Hgt", "X/Y/Z-ECEF"});
    r = 0;
    addRow(g2, r++, "Error Bar/Circle", cErr);
    addRow(g2, r++, "Direction Arrow",  cArrow);
    addRow(g2, r++, "Graph Label",      cGLabel);
    addRow(g2, r++, "Grid/Grid Label",  cGrid);
    addRow(g2, r++, "Compass",          cCompass);
    addRow(g2, r++, "Scale",            cScale);
    addRow(g2, r++, "Auto Fit",         cAutoScale);
    addRow(g2, r++, "Y-Range (±)", cYRange);
    addRow(g2, r++, "RT Buffer Size",   eBuffSize);
    g2->addWidget(kTimeSync, r, 0);
    g2->addWidget(eSyncPort, r++, 1);
    addRow(g2, r++, "Coordinate Origin", cOrigin);

    for (int i = 0; i < 3; i++) eRefPos[i] = edit(100);
    auto *lRef = new QHBoxLayout();
    for (int i = 0; i < 3; i++) lRef->addWidget(eRefPos[i]);
    auto *bRef = new QPushButton("...");
    bRef->setFixedWidth(28);
    bRef->setEnabled(false);
    bRef->setToolTip("station position file lookup is not implemented yet");
    lRef->addWidget(bRef);

    eRnxOpts = edit(200); eShapeFile = edit(200);
    eTleFile = edit(200); eTleSatFile = edit(200);

    auto *v2 = new QVBoxLayout();
    v2->addLayout(g2);
    auto *g2b = new QGridLayout();
    g2b->addWidget(new QLabel("Lat/Lon/Hgt"), 0, 0);
    g2b->addLayout(lRef, 0, 1);
    addRow(g2b, 1, "RINEX Opts",  eRnxOpts);
    addRow(g2b, 2, "Shape File",  eShapeFile);
    addRow(g2b, 3, "TLE Data",    eTleFile);
    addRow(g2b, 4, "TLE Sat No",  eTleSatFile);
    v2->addLayout(g2b);
    v2->addStretch();

    // ---- column 3 ----------------------------------------------------------
    auto *g3 = new QGridLayout();
    for (int k = 0; k < 2; k++) {
        auto *row = new QHBoxLayout();
        row->setSpacing(1);
        for (int i = 0; i < 6; i++) {
            bMColor[k][i] = new QPushButton();
            bMColor[k][i]->setFixedSize(22, 20);
            bMColor[k][i]->setToolTip(QString("solution %1, quality %2")
                                          .arg(k+1).arg(i+1));
            connect(bMColor[k][i], &QPushButton::clicked, this,
                    &PlotOptDialog::pickColor);
            row->addWidget(bMColor[k][i]);
        }
        row->addStretch();
        g3->addWidget(new QLabel(QString("Mark Color %1 (1-6)").arg(k+1)), k, 0);
        g3->addLayout(row, k, 1);
    }
    static const char *cName[4] = {"Background Color", "Grid Color",
                                   "Text Color", "Line Color"};
    static const int cOrder[4] = {3, 2, 1, 0};      // line, text, grid, bg
    for (int i = 0; i < 4; i++) {
        int idx = cOrder[i];
        bCColor[idx] = new QPushButton();
        bCColor[idx]->setFixedHeight(20);
        bCColor[idx]->setMinimumWidth(90);
        connect(bCColor[idx], &QPushButton::clicked, this, &PlotOptDialog::pickColor);
        auto *row = new QHBoxLayout();
        row->setSpacing(2);
        row->addWidget(bCColor[idx]);
        g3->addWidget(new QLabel(cName[idx]), 2 + i, 0);
        g3->addLayout(row, 2 + i, 1);
    }
    cStyle    = combo({"Mark/Line", "Mark", "Line", "None"});
    cMarkSize = combo({"1", "2", "3", "4", "5", "6", "8", "10"});
    bFont     = new QPushButton();
    bFont->setMinimumWidth(140);
    connect(bFont, &QPushButton::clicked, this, &PlotOptDialog::pickFont);
    eAnimCycle = edit(80);
    eRefCycle  = edit(80);
    r = 6;
    addRow(g3, r++, "Plot Style", cStyle);
    addRow(g3, r++, "Mark Size",  cMarkSize);
    addRow(g3, r++, "Font",       bFont);
    addRow(g3, r++, "Animation Interval", eAnimCycle);
    addRow(g3, r++, "Update Cycle (ms)",  eRefCycle);

    auto *v3 = new QVBoxLayout();
    v3->addLayout(g3);
    v3->addStretch();

    // ---- assemble ----------------------------------------------------------
    auto *cols = new QHBoxLayout();
    cols->setSpacing(UI_SPACING * 4);
    cols->addLayout(v1);
    cols->addLayout(v2);
    cols->addLayout(v3);

    auto *bOK = new QPushButton("OK");
    auto *bCancel = new QPushButton("Cancel");
    bOK->setDefault(true);
    bOK->setMinimumWidth(120);
    bCancel->setMinimumWidth(120);
    connect(bOK, &QPushButton::clicked, this, &PlotOptDialog::okClicked);
    connect(bCancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(bOK);
    btns->addWidget(bCancel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN*2, UI_MARGIN*2, UI_MARGIN*2, UI_MARGIN*2);
    vbox->setSpacing(UI_SPACING * 2);
    vbox->addLayout(cols);
    vbox->addLayout(btns);

    connect(cOrigin, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotOptDialog::originChanged);

    // controls whose plot types are not ported yet
    for (QWidget *w : {(QWidget *)cCycSlip, (QWidget *)cHalfC, (QWidget *)cEph,
                       (QWidget *)cElMask, (QWidget *)cElMaskP, (QWidget *)cHideLow,
                       (QWidget *)cMaxDop, (QWidget *)cMaxMP, (QWidget *)cRcvPos,
                       (QWidget *)eExcSats, (QWidget *)ePlotOpts,
                       (QWidget *)eRnxOpts, (QWidget *)eShapeFile,
                       (QWidget *)eTleFile, (QWidget *)eTleSatFile,
                       (QWidget *)kTimeSync, (QWidget *)eSyncPort,
                       (QWidget *)eAnimCycle, (QWidget *)gSys}) {
        w->setEnabled(false);
        w->setToolTip(TIP_NOTYET);
    }
    eAnimCycle->setToolTip("animation is not implemented yet");
    kTimeSync->setToolTip("time sync port is not implemented yet");
    eSyncPort->setToolTip(kTimeSync->toolTip());
}
//------------------------------------------------------------------------------
void PlotOptDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;

    cTimeForm  ->setCurrentIndex(opt.timeFormat);
    cLatLonForm->setCurrentIndex(opt.latLonFormat);
    cStats     ->setCurrentIndex(opt.showStats);
    cCycSlip   ->setCurrentIndex(opt.cycleSlip);
    cHalfC     ->setCurrentIndex(opt.showHalfC);
    cEph       ->setCurrentIndex(opt.showEph);
    cElMask    ->setCurrentText(QString::number(opt.elMask));
    cElMaskP   ->setCurrentIndex(0);
    cHideLow   ->setCurrentIndex(opt.hideLowSat);
    cMaxDop    ->setCurrentText(QString::number(opt.maxDop));
    cMaxMP     ->setCurrentText(QString::number(opt.maxMP));
    cRcvPos    ->setCurrentIndex(opt.rcvPos);
    for (int i = 0; i < 6; i++) {
        static const int sysBit[6] = {SYS_GPS, SYS_GLO, SYS_GAL, SYS_QZS,
                                      SYS_CMP, SYS_SBS};
        kSys[i]->setChecked(opt.navSys & sysBit[i]);
    }
    eExcSats ->setText(opt.excSats);

    cErr      ->setCurrentIndex(opt.showErr);
    cArrow    ->setCurrentIndex(opt.showArrow);
    cGLabel   ->setCurrentIndex(opt.showGLabel);
    cGrid     ->setCurrentIndex(opt.showGrid);
    cCompass  ->setCurrentIndex(opt.showCompass);
    cScale    ->setCurrentIndex(opt.showScale);
    cAutoScale->setCurrentIndex(opt.autoScale);
    cYRange   ->setCurrentText(QString("%1m").arg(opt.yRange));
    eBuffSize ->setText(QString::number(opt.rtBuffSize));
    eSyncPort ->setText(QString::number(opt.timeSyncPort));
    cOrigin   ->setCurrentIndex(opt.origin);
    for (int i = 0; i < 3; i++)
        eRefPos[i]->setText(QString::number(opt.ooPos[i], 'f',
                                            opt.origin == ORG_XYZPOS ? 4 :
                                            i == 2 ? 4 : 9));
    eRnxOpts  ->setText(opt.rnxOpts);
    eShapeFile->setText(opt.shapeFile);
    eTleFile  ->setText(opt.tleFile);
    eTleSatFile->setText(opt.tleSatFile);

    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 8; i++) work[k][i] = opt.mColor[k][i];
        for (int i = 0; i < 6; i++) setSwatch(bMColor[k][i], work[k][i+1]);
    }
    for (int i = 0; i < 4; i++) { workC[i] = opt.cColor[i]; setSwatch(bCColor[i], workC[i]); }

    cStyle->setCurrentIndex(opt.plotStyle);
    cMarkSize->setCurrentText(QString::number(opt.markSize));
    workFont = opt.font;
    bFont->setText(QString("%1 %2pt").arg(workFont.family()).arg(workFont.pointSize()));
    eAnimCycle->setText(QString::number(opt.animCycle));
    eRefCycle ->setText(QString::number(opt.refCycle));

    updateEnable();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
void PlotOptDialog::okClicked()
{
    opt.timeFormat   = cTimeForm->currentIndex();
    opt.latLonFormat = cLatLonForm->currentIndex();
    opt.showStats    = cStats->currentIndex();
    opt.cycleSlip    = cCycSlip->currentIndex();
    opt.showHalfC    = cHalfC->currentIndex();
    opt.showEph      = cEph->currentIndex();
    opt.elMask       = cElMask->currentText().toDouble();
    opt.hideLowSat   = cHideLow->currentIndex();
    opt.maxDop       = cMaxDop->currentText().toDouble();
    opt.maxMP        = cMaxMP->currentText().toDouble();
    opt.rcvPos       = cRcvPos->currentIndex();
    static const int sysBit[6] = {SYS_GPS, SYS_GLO, SYS_GAL, SYS_QZS,
                                  SYS_CMP, SYS_SBS};
    opt.navSys = 0;
    for (int i = 0; i < 6; i++) if (kSys[i]->isChecked()) opt.navSys |= sysBit[i];
    opt.excSats = eExcSats->text();

    opt.showErr     = cErr->currentIndex();
    opt.showArrow   = cArrow->currentIndex();
    opt.showGLabel  = cGLabel->currentIndex();
    opt.showGrid    = cGrid->currentIndex();
    opt.showCompass = cCompass->currentIndex();
    opt.showScale   = cScale->currentIndex();
    opt.autoScale   = cAutoScale->currentIndex();
    {   // "5m" / "0.05m" / "5"
        QString s = cYRange->currentText();
        s.remove('m');
        double v = s.toDouble();
        if (v > 0.0) opt.yRange = v;
    }
    opt.rtBuffSize   = qMax(eBuffSize->text().toInt(), 100);
    opt.timeSyncPort = eSyncPort->text().toInt();
    opt.origin       = cOrigin->currentIndex();
    for (int i = 0; i < 3; i++) opt.ooPos[i] = eRefPos[i]->text().toDouble();
    opt.rnxOpts   = eRnxOpts->text();
    opt.shapeFile = eShapeFile->text();
    opt.tleFile   = eTleFile->text();
    opt.tleSatFile= eTleSatFile->text();

    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 8; i++) opt.mColor[k][i] = work[k][i];
    for (int i = 0; i < 4; i++) opt.cColor[i] = workC[i];

    opt.plotStyle = cStyle->currentIndex();
    opt.markSize  = qMax(cMarkSize->currentText().toInt(), 1);
    opt.font      = workFont;
    opt.animCycle = qMax(eAnimCycle->text().toInt(), 1);
    opt.refCycle  = qBound(10, eRefCycle->text().toInt(), 10000);

    accept();
}
//------------------------------------------------------------------------------
QColor *PlotOptDialog::colorOf(QPushButton *b)
{
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 6; i++)
            if (bMColor[k][i] == b) return &work[k][i+1];
    for (int i = 0; i < 4; i++)
        if (bCColor[i] == b) return &workC[i];
    return nullptr;
}
//------------------------------------------------------------------------------
void PlotOptDialog::pickColor()
{
    auto *b = qobject_cast<QPushButton *>(sender());
    QColor *c = b ? colorOf(b) : nullptr;
    if (!c) return;

    QColor n = QColorDialog::getColor(*c, this, "Color");
    if (!n.isValid()) return;
    *c = n;
    setSwatch(b, n);
}
//------------------------------------------------------------------------------
void PlotOptDialog::pickFont()
{
    bool ok = false;
    QFont f = QFontDialog::getFont(&ok, workFont, this, "Font");
    if (!ok) return;
    workFont = f;
    bFont->setText(QString("%1 %2pt").arg(f.family()).arg(f.pointSize()));
}
//------------------------------------------------------------------------------
void PlotOptDialog::originChanged()
{
    updateEnable();
}
//------------------------------------------------------------------------------
void PlotOptDialog::updateEnable()
{
    int o = cOrigin->currentIndex();
    bool needPos = o == ORG_LLHPOS || o == ORG_XYZPOS;
    for (int i = 0; i < 3; i++) eRefPos[i]->setEnabled(needPos);
    cYRange->setEnabled(cAutoScale->currentIndex() == 0);
}
//------------------------------------------------------------------------------
