//------------------------------------------------------------------------------
// plotopt.h : plot options for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotopt (RTKLIB 2.4.3 b35).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef PLOTOPT_H
#define PLOTOPT_H

#include <QColor>
#include <QDialog>
#include <QFont>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSettings;

// plot origin (same values as the VCL original) --------------------------------
#define ORG_STARTPOS 0                  // start position
#define ORG_ENDPOS   1                  // end position
#define ORG_AVEPOS   2                  // average position
#define ORG_FITPOS   3                  // linear-fit position
#define ORG_REFPOS   4                  // reference (base) position
#define ORG_LLHPOS   5                  // given lat/lon/height
#define ORG_XYZPOS   6                  // given X/Y/Z-ECEF

//------------------------------------------------------------------------------
struct PlotOpt
{
    // --- appearance ---------------------------------------------------------
    int     timeFormat   = 1;           // 0:ww ssss 1:GPST 2:UTC 3:JST
    int     latLonFormat = 0;           // 0:ddd.dddddd 1:ddd mm ss.ss
    int     showStats    = 1;           // AVE/STD/RMS overlay
    int     showArrow    = 0;           // direction arrow on the track
    int     showErr      = 0;           // 0:off 1:bar/circle 2:dotted
    int     showGLabel   = 1;           // 0:off 1:on 2:on + axis label
    int     showScale    = 1;           // scale bar on the track
    int     showCompass  = 0;           // compass rose on the track
    int     showGrid     = 1;           // 0:off 1:grid 2:grid + grid label
    int     autoScale    = 1;           // re-fit as data arrives
    double  yRange       = 5.0;         // fixed y range (m) when autoScale off
    int     plotStyle    = 0;           // 0:mark+line 1:mark 2:line 3:none
    int     markSize     = 3;
    QFont   font;

    // --- colours ------------------------------------------------------------
    //   mColor[sel][q] : q = 0:none 1:fix 2:float 3:sbas 4:dgps 5:single 6:ppp 7:dr
    //   cColor[]       : 0:background 1:grid 2:text 3:line
    QColor  mColor[2][8];
    QColor  cColor[4];

    // --- origin -------------------------------------------------------------
    int     origin  = ORG_AVEPOS;
    double  ooPos[3] = {0, 0, 0};       // lat/lon/hgt (deg,deg,m) or ecef (m)

    // --- real time ----------------------------------------------------------
    int     rtBuffSize = 10800;         // max epochs kept while connected
    int     refCycle   = 100;           // stream poll / refresh cycle (ms)

    // --- not used by the plot types implemented so far ----------------------
    double  elMask = 0.0, maxDop = 30.0, maxMP = 10.0;
    int     cycleSlip = 1, showEph = 0, showHalfC = 0, hideLowSat = 0;
    int     navSys = 0, rcvPos = 0, animCycle = 1, timeSyncOut = 0;
    int     timeSyncPort = 10071;
    QString excSats, rnxOpts, shapeFile, tleFile, tleSatFile;

    PlotOpt();
    void load(QSettings &s);
    void save(QSettings &s) const;
};

//------------------------------------------------------------------------------
class PlotOptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlotOptDialog(QWidget *parent = nullptr);

    PlotOpt opt;                        // in/out

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void okClicked();
    void originChanged();
    void pickColor();
    void pickFont();

private:
    QPushButton *colorButton(QColor *target);
    void updateEnable();

    QComboBox *cTimeForm, *cLatLonForm, *cStats, *cCycSlip, *cHalfC, *cEph;
    QComboBox *cElMask, *cElMaskP, *cHideLow, *cMaxDop, *cMaxMP, *cRcvPos;
    QComboBox *cErr, *cArrow, *cGLabel, *cGrid, *cCompass, *cScale;
    QComboBox *cAutoScale, *cYRange, *cOrigin, *cStyle, *cMarkSize;
    QCheckBox *kSys[6], *kTimeSync;
    QLineEdit *eBuffSize, *eSyncPort, *eRefPos[3], *eExcSats, *ePlotOpts;
    QLineEdit *eRnxOpts, *eShapeFile, *eTleFile, *eTleSatFile;
    QLineEdit *eAnimCycle, *eRefCycle;
    QPushButton *bMColor[2][6], *bCColor[4], *bFont;
    QColor   *colorOf(QPushButton *b);
    QColor    work[2][8], workC[4];     // edited colours until OK
    QFont     workFont;
};

#endif // PLOTOPT_H
