//------------------------------------------------------------------------------
// plotpanel.h : RTKNAVI-Qt plot panel (QPainter port of navimain DrawPlot)
//
// plot types (same as VCL PlotType):
//   0: SNR rover+base  1: SNR rover  2: skyplot rover  3: skyplot+SNR
//   4: skyplot rover+base  5: baseline  6: ground track
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/navimain (DrawPlot group).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef PLOTPANEL_H
#define PLOTPANEL_H

#include <QFrame>

#include "rtklib.h"

#define SOLBUF_SIZE 1000                // solution ring buffer size

// shared display data (filled by MainWindow, read by PlotPanel) ---------------
struct NaviData {
    // solution ring buffer (VCL SolStat/Time/SolRov/... arrays)
    int     solStat[SOLBUF_SIZE];
    gtime_t time[SOLBUF_SIZE];
    int     nvsat[SOLBUF_SIZE];
    double  age[SOLBUF_SIZE], ratio[SOLBUF_SIZE];
    double  solRov[SOLBUF_SIZE*3], solRef[SOLBUF_SIZE*3], velRov[SOLBUF_SIZE*3];
    double  qr[SOLBUF_SIZE*9];
    int     pSol, pSolS, pSolE;         // current/start/end of ring
    // observation data for plots (VCL Nsat/Sat/Az/El/Snr/Vsat)
    int     nsat[2];
    int     sat[2][MAXSAT], vsat[2][MAXSAT];
    double  az[2][MAXSAT], el[2][MAXSAT];
    int     snr[2][MAXSAT][NFREQ];
    // track plot origin
    double  trkOri[3];
    // live state
    rtksvr_t *svr;
    prcopt_t *prcopt;
    int     solCurrentStat;
};

extern NaviData navi;

class QPushButton;

class PlotPanel : public QFrame
{
    Q_OBJECT
public:
    explicit PlotPanel(int initType, QWidget *parent = nullptr);

    // state persistence
    int  type() const { return plotType; }
    int  freq() const { return freqType; }
    int  trk()  const { return trkType; }
    int  scale() const { return trkScale; }
    void setState(int type, int freq, int trk, int scale);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void btnTypeClicked();
    void btnFreqClicked();
    void btnExpandClicked();
    void btnShrinkClicked();

private:
    QColor snrColor(int snr) const;
    void drawTextA(QPainter &c, int x, int y, const QString &s,
                   const QColor &color, int ha, int va);
    void drawArrow(QPainter &c, int x, int y, int siz, int ang,
                   const QColor &color);
    void drawSnr(QPainter &c, int w, int h, int x0, int y0, int index);
    void drawSky(QPainter &c, int w, int h, int x0, int y0);
    void drawSat(QPainter &c, int w, int h, int x0, int y0, int index);
    void drawBL(QPainter &c, int w, int h);
    void drawTrk(QPainter &c, int w, int h);
    void updateButtons();

    int plotType;                       // 0-6
    int freqType  = 0;                  // 0:max,1..NFREQ:Lx,NFREQ+1:SYS
    int trkType   = 0;                  // 0:axis,1:circles
    int blMode    = 0;
    int trkScale  = 5;                  // index into scale table

    QPushButton *btnType, *btnFreq, *btnExpand, *btnShrink;
};

#endif // PLOTPANEL_H
