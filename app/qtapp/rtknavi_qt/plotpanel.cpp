//------------------------------------------------------------------------------
// plotpanel.cpp : RTKNAVI-Qt plot panel
//
// QPainter port of navimain.cpp DrawPlot/DrawSnr/DrawSat/DrawSky/DrawBL/
// DrawTrk/DrawText/DrawArrow/SnrColor. The ground-track plot re-implements
// the TGraph parts (grid, marks, scale bar) directly with QPainter.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/navimain (DrawPlot/DrawSnr/DrawSat/DrawBL/DrawTrk).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>

#include "plotpanel.h"

#define MINSNR      10                  // minimum snr
#define MAXSNR      60                  // maximum snr
#define MINBLLEN    0.01                // minimum baseline length to show
#define MAXSCALE    18                  // max scale index of track plot

NaviData navi = {};

static const QString CHARDEG = QString::fromUtf8("°");

// system colors: G R E J C I S + unknown (VCL color_sys) ----------------------
static const QColor SYS_COLOR[]={
    QColor(0,128,0),QColor(255,170,0),QColor(255,0,255),QColor(0,0,255),
    QColor(255,0,0),QColor(0,128,128),QColor(128,128,128)
};
static const QColor SYS_TEXT[]={
    QColor(0,128,0),QColor(128,128,0),QColor(160,0,160),QColor(0,0,128),
    QColor(128,0,0),QColor(0,128,128),QColor(128,128,128)
};
static const QColor STAT_COLOR[]={
    QColor(192,192,192),QColor(0,128,0),QColor(255,170,0),QColor(255,0,255),
    QColor(0,0,255),QColor(255,0,0),QColor(0,128,128),QColor(128,96,0)
};
static const double TRK_SCALE[]={
    0.00021,0.00047,0.001,0.0021,0.0047,0.01,0.021,0.047,0.1,0.21,0.47,
    1.0,2.1,4.7,10.0,21.0,47.0,100.0,210.0,470.0,1000.0,2100.0,4700.0,10000.0
};

//------------------------------------------------------------------------------
PlotPanel::PlotPanel(int initType, QWidget *parent)
    : QFrame(parent), plotType(initType)
{
    // thin sunken box around each panel, same as the original RTKNAVI
    setFrameStyle(QFrame::Panel | QFrame::Sunken);
    setLineWidth(1);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    setMinimumSize(160, 120);

    auto mkbtn = [this](const QString &text) {
        auto *b = new QPushButton(text, this);
        b->setFlat(true);
        b->setFixedSize(18, 16);
        b->setStyleSheet("color:gray;background:white;border:none;");
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    btnFreq   = mkbtn(QString::fromUtf8("▾"));
    btnType   = mkbtn(QString::fromUtf8("▾"));
    btnExpand = mkbtn("+");
    btnShrink = mkbtn(QString::fromUtf8("−"));
    btnFreq->setToolTip("frequency / mode");
    btnType->setToolTip("plot type");
    btnExpand->setToolTip("zoom in");
    btnShrink->setToolTip("zoom out");

    connect(btnType,   &QPushButton::clicked, this, &PlotPanel::btnTypeClicked);
    connect(btnFreq,   &QPushButton::clicked, this, &PlotPanel::btnFreqClicked);
    connect(btnExpand, &QPushButton::clicked, this, &PlotPanel::btnExpandClicked);
    connect(btnShrink, &QPushButton::clicked, this, &PlotPanel::btnShrinkClicked);
    updateButtons();
}
//------------------------------------------------------------------------------
void PlotPanel::setState(int type, int freq, int trk, int scale)
{
    if (type  >= 0 && type <= 6)         plotType = type;
    if (freq  >= 0 && freq <= NFREQ + 1) freqType = freq;
    if (trk   >= 0 && trk <= 1)          trkType = trk;
    if (scale >= 0 && scale <= MAXSCALE) trkScale = scale;
    updateButtons();
    update();
}
//------------------------------------------------------------------------------
void PlotPanel::updateButtons()
{
    btnExpand->setVisible(plotType == 6);
    btnShrink->setVisible(plotType == 6);
}
//------------------------------------------------------------------------------
void PlotPanel::resizeEvent(QResizeEvent *event)
{
    QRect r = contentsRect();
    int x = r.right() - 19;
    btnType->move(x, r.top() + 1);   x -= 20;
    btnFreq->move(x, r.top() + 1);
    btnExpand->move(r.left() + 1, r.top() + 19);
    btnShrink->move(r.left() + 1, r.top() + 37);
    QFrame::resizeEvent(event);
}
//------------------------------------------------------------------------------
void PlotPanel::btnTypeClicked()
{
    if (++plotType > 6) plotType = 0;
    updateButtons();
    update();
}
//------------------------------------------------------------------------------
void PlotPanel::btnFreqClicked()
{
    // VCL BtnFreqTypeXClick: meaning depends on plot type
    if (plotType == 6) {
        if (++trkType > 1) trkType = 0;
    }
    else if (plotType == 5) {
        if (++blMode > 1) blMode = 0;
    }
    else {
        if (++freqType > NFREQ + 1) freqType = 0;
    }
    update();
}
//------------------------------------------------------------------------------
void PlotPanel::btnExpandClicked()
{
    if (trkScale > 0) trkScale--;
    update();
}
//------------------------------------------------------------------------------
void PlotPanel::btnShrinkClicked()
{
    if (trkScale < MAXSCALE) trkScale++;
    update();
}
//------------------------------------------------------------------------------
// port of VCL DrawPlot()
void PlotPanel::paintEvent(QPaintEvent *event)
{
    QPainter c(this);
    QRect r = contentsRect();
    c.fillRect(r, Qt::white);
    c.translate(r.topLeft());           // draw inside the frame
    c.setFont(font());

    QString fstr[NFREQ + 2];
    for (int i = 0; i < NFREQ; i++) fstr[i + 1] = QString("L%1").arg(i + 1);
    fstr[NFREQ + 1] = " SYS";

    int w = r.width() - 2, h = r.height() - 2, x = 4;
    int tm = c.fontMetrics().height() * 5 / 4;
    QString s1, s2;
    const QColor gray(128, 128, 128);

    switch (plotType) {
    case 0:                             // snr rover+base
        if (w <= 3 * h) {
            drawSnr(c, w, (h - tm) / 2, 0, tm, 0);
            drawSnr(c, w, (h - tm) / 2, 0, tm + (h - tm) / 2, 1);
            drawTextA(c, x, 1, "Rover:Base " + fstr[freqType] + " SNR (dBHz)", gray, 1, 2);
        }
        else {
            drawSnr(c, w / 2, h - tm, 0, tm, 0);
            drawSnr(c, w / 2, h - tm, w / 2, tm, 1);
            drawTextA(c, x, 1, "Rover " + fstr[freqType] + " SNR (dBHz)", gray, 1, 2);
            drawTextA(c, w / 2 + x, 1, "Base " + fstr[freqType] + " SNR (dBHz)", gray, 1, 2);
        }
        break;
    case 1:                             // snr rover
        drawSnr(c, w, h - tm, 0, tm, 0);
        drawTextA(c, x, 1, "Rover " + fstr[freqType] + " SNR (dBHz)", gray, 1, 2);
        break;
    case 2:                             // skyplot rover
        drawSat(c, w, h, 0, 0, 0);
        drawTextA(c, x, 1, "Rover " + fstr[freqType], gray, 1, 2);
        break;
    case 3:                             // skyplot + snr
        if (w >= h * 2) {
            drawSat(c, h, h, 0, 0, 0);
            drawSnr(c, w - h, h - tm, h, tm, 0);
            drawTextA(c, x, 1, "Rover " + fstr[freqType], gray, 1, 2);
            drawTextA(c, x + h, 1, "SNR (dBHz)", gray, 1, 2);
        }
        else {
            drawSat(c, w, h / 2, 0, 0, 0);
            drawSnr(c, w, (h - tm) / 2, 0, tm + (h - tm) / 2, 1);
            drawTextA(c, x, 1, "Rover " + fstr[freqType], gray, 1, 2);
        }
        break;
    case 4:                             // skyplot rover+base
        if (w >= h) {
            drawSat(c, w / 2, h, 0, 0, 0);
            drawSat(c, w / 2, h, w / 2, 0, 1);
            drawTextA(c, x, 1, "Rover " + fstr[freqType], gray, 1, 2);
            drawTextA(c, x + w / 2, 1, "Base " + fstr[freqType], gray, 1, 2);
        }
        else {
            drawSat(c, w, h / 2, 0, 0, 0);
            drawSat(c, w, h / 2, 0, h / 2, 1);
            drawTextA(c, x, 1, "Rover " + fstr[freqType], gray, 1, 2);
            drawTextA(c, x, h / 2 + 1, "Base " + fstr[freqType], gray, 1, 2);
        }
        break;
    case 5:                             // baseline
        drawBL(c, w, h);
        drawTextA(c, x, 1, "Baseline", gray, 1, 2);
        break;
    case 6:                             // ground track
        drawTrk(c, w, h);
        drawTextA(c, x, 3, "Gnd Trk", gray, 1, 2);
        break;
    }
    c.resetTransform();
    QFrame::paintEvent(event);          // frame on top
}
//------------------------------------------------------------------------------
// port of VCL DrawText() (ha 0:center,1:left,2:right / va 0:center,1:bottom,2:top)
void PlotPanel::drawTextA(QPainter &c, int x, int y, const QString &s,
                          const QColor &color, int ha, int va)
{
    QFontMetrics fm = c.fontMetrics();
    int cx = fm.horizontalAdvance(s), cy = fm.height();
    if      (ha == 0) x -= cx / 2;
    else if (ha == 2) x -= cx;
    if      (va == 0) y -= cy / 2;
    else if (va == 1) y -= cy;
    c.setPen(color);
    c.drawText(x, y + fm.ascent(), s);
}
//------------------------------------------------------------------------------
// port of VCL DrawArrow()
void PlotPanel::drawArrow(QPainter &c, int x, int y, int siz, int ang,
                          const QColor &color)
{
    QPoint p1[4], p2[4];
    p1[0] = QPoint(0, siz / 2);
    p1[1] = QPoint(siz / 2, -siz / 2);
    p1[2] = QPoint(-siz / 2, -siz / 2);
    p1[3] = QPoint(0, siz / 2);
    for (int i = 0; i < 4; i++) {
        double a = -ang * D2R;
        p2[i].setX(x + (int)(p1[i].x() * cos(a) - p1[i].y() * sin(a) + 0.5));
        p2[i].setY(y - (int)(p1[i].x() * sin(a) + p1[i].y() * cos(a) + 0.5));
    }
    c.setPen(color);
    c.setBrush(color);
    c.drawPolygon(p2, 3);
}
//------------------------------------------------------------------------------
// port of VCL SnrColor()
QColor PlotPanel::snrColor(int snr) const
{
    static const QColor color[] = {
        QColor(0,128,0),QColor(255,165,0),QColor(255,0,255),
        QColor(0,0,255),QColor(255,0,0),QColor(128,128,128)
    };
    if (snr < 25) return color[5];
    if (snr < 27) return color[4];
    if (snr > 47) return color[0];
    double a = (snr - 27.5) / 5.0;
    int i = (int)a; a -= i;
    const QColor &c1 = color[3 - i], &c2 = color[4 - i];
    return QColor((int)(a * c1.red()   + (1 - a) * c2.red()),
                  (int)(a * c1.green() + (1 - a) * c2.green()),
                  (int)(a * c1.blue()  + (1 - a) * c2.blue()));
}
//------------------------------------------------------------------------------
// port of VCL DrawSnr()
void PlotPanel::drawSnr(QPainter &c, int w, int h, int x0, int y0, int index)
{
    int i, j, k, l, n, x1, y1, y2, y3, tm, bm, hh, ww, www, snr[NFREQ + 1];
    int mask[7] = {0};
    char id[16];
    const char sys[] = "GREJCIS";
    const char *q;
    const QColor gray(128,128,128), silver(192,192,192);
    int fs = c.fontMetrics().height();

    tm = fs * 3 / 8;
    bm = fs * 7 / 8;
    y0 += tm;
    hh = h - tm - bm;

    for (int sv = MINSNR + 10; sv < MAXSNR; sv += 10) {
        y1 = y0 + hh - (sv - MINSNR) * hh / (MAXSNR - MINSNR);
        c.setPen(silver);
        c.drawLine(x0 + 2, y1, x0 + w - 2, y1);
        drawTextA(c, x0 + w - 4, y1, QString::number(sv), gray, 2, 0);
    }
    y1 = y0 + hh;
    c.setPen(gray);
    c.setBrush(Qt::NoBrush);
    c.drawRect(x0 + 2, y0, w - 4, hh);

    for (i = 0; i < navi.nsat[index] && i < MAXSAT; i++) {
        ww = (w - 16) / navi.nsat[index];
        www = ww - 2 < 8 ? ww - 2 : 8;
        x1 = x0 + i * (w - 16) / navi.nsat[index] + ww / 2;
        satno2id(navi.sat[index][i], id);
        l = (q = strchr(sys, id[0])) ? (int)(q - sys) : 6;

        for (j = snr[0] = 0; j < NFREQ; j++) {
            snr[j + 1] = navi.snr[index][i][j];
            if ((freqType && freqType == j + 1) ||
                ((!freqType || freqType > NFREQ) && snr[j + 1] > snr[0])) {
                snr[0] = snr[j + 1];
            }
        }
        for (j = 0; j < NFREQ + 2; j++) {
            k = j < NFREQ + 1 ? j : 0;
            y3 = j < NFREQ + 1 ? 0 : 2;
            y2 = y1 - y3;
            if (snr[k] > 0) y2 -= (snr[k] - MINSNR) * hh / (MAXSNR - MINSNR) - y3;
            y2 = y2 < 2 ? 2 : (y1 < y2 ? y1 : y2);

            if (j == 0) {
                c.setPen(gray);
                c.setBrush(freqType < NFREQ + 1 ? snrColor(snr[k]) : SYS_COLOR[l]);
                if (!navi.vsat[index][i]) c.setBrush(silver);
                c.drawRect(x1, y2, www, y1 - y2);
            }
            else if (snr[k] > 0) {
                c.setPen(j < NFREQ + 1 ? silver : gray);
                c.setBrush(Qt::NoBrush);
                c.drawRect(x1, y2, www, y1 - y2);
            }
        }
        drawTextA(c, x1 + www / 2, y1 + 1, QString(id + 1), SYS_TEXT[l], 0, 2);
        mask[l] = 1;
    }
    for (i = n = 0; i < 7; i++) if (mask[i]) n++;
    for (i = j = 0; i < 7; i++) {
        if (!mask[i]) continue;
        drawTextA(c, x0 + w - fs * 3 / 2 + fs * 3 / 4 * (-n + j++), y0 + 1,
                  QString(sys[i]), SYS_TEXT[i], 0, 2);
    }
}
//------------------------------------------------------------------------------
// port of VCL DrawSky()
void PlotPanel::drawSky(QPainter &c, int w, int h, int x0, int y0)
{
    static const char *label[] = {"N", "E", "S", "W"};
    QPoint p(x0 + w / 2, y0 + h / 2);
    double r = qMin(w * 0.95, h * 0.95) / 2;
    const QColor gray(128,128,128), silver(192,192,192);

    c.setBrush(Qt::white);
    for (int e = 0; e < 90; e += 30) {
        int d = (int)(r * (90 - e) / 90);
        c.setPen(e == 0 ? gray : silver);
        c.drawEllipse(p, d, d);
    }
    for (int a = 0; a < 360; a += 45) {
        int x = (int)(r * sin(a * D2R));
        int y = (int)(r * cos(a * D2R));
        c.setPen(silver);
        c.drawLine(p.x(), p.y(), p.x() + x, p.y() - y);
        if (a % 90 == 0) drawTextA(c, p.x() + x, p.y() - y, label[a / 90], gray, 0, 0);
    }
}
//------------------------------------------------------------------------------
// port of VCL DrawSat()
void PlotPanel::drawSat(QPainter &c, int w, int h, int x0, int y0, int index)
{
    QPoint p(w / 2, h / 2);
    double r = qMin(w * 0.95, h * 0.95) / 2, azel[MAXSAT * 2], dop[4];
    int i, j, k, l, d, x, y, snr[NFREQ + 1], ns = 0;
    char id[16];
    const char sys[] = "GREJCIS";
    const char *q;
    const QColor gray(128,128,128), silver(192,192,192);
    int fs = c.fontMetrics().height();

    drawSky(c, w, h, x0, y0);

    for (i = 0, k = navi.nsat[index] - 1; i < navi.nsat[index] && i < MAXSAT; i++, k--) {
        if (navi.el[index][k] <= 0.0) continue;
        for (j = snr[0] = 0; j < NFREQ; j++) {
            snr[j + 1] = navi.snr[index][k][j];
            if ((freqType && freqType == j + 1) ||
                ((!freqType || freqType > NFREQ) && snr[j + 1] > snr[0])) {
                snr[0] = snr[j + 1];
            }
        }
        if (navi.vsat[index][k] && (snr[freqType] > 0 || freqType > NFREQ)) {
            azel[ns * 2] = navi.az[index][k]; azel[1 + ns * 2] = navi.el[index][k];
            ns++;
        }
        satno2id(navi.sat[index][k], id);
        l = (q = strchr(sys, id[0])) ? (int)(q - sys) : 6;
        x = (int)(p.x() + r * (90 - navi.el[index][k] * R2D) / 90 * sin(navi.az[index][k])) + x0;
        y = (int)(p.y() - r * (90 - navi.el[index][k] * R2D) / 90 * cos(navi.az[index][k])) + y0;
        d = fs * 3 / 4;
        QColor bg = !navi.vsat[index][k] ? silver :
                    (freqType < NFREQ + 1 ? snrColor(snr[freqType]) : SYS_COLOR[l]);
        QColor text = Qt::white;
        c.setBrush(bg);
        c.setPen(gray);
        if (freqType < NFREQ + 1 && snr[freqType] <= 0) {
            c.setBrush(Qt::NoBrush);
            c.setPen(silver);
            text = silver;
        }
        c.drawEllipse(QPoint(x, y), d, d);
        drawTextA(c, x, y, QString(id), text, 0, 0);
    }
    dops(ns, azel, 0.0, dop);
    drawTextA(c, x0 + 3, y0 + h, QString::asprintf("#Sat:%2d/%2d", ns, navi.nsat[index]), gray, 1, 1);
    drawTextA(c, x0 + w - 3, y0 + h, QString::asprintf("GDOP: %.1f", dop[0]), gray, 2, 1);
}
//------------------------------------------------------------------------------
// port of VCL DrawBL()
void PlotPanel::drawBL(QPainter &c, int w, int h)
{
    static const char *label[] = {"N", "E", "S", "W"};
    QPoint p(w / 2, h / 2), p1, p2, pp;
    double r = qMin(w * 0.95, h * 0.95) / 2;
    double *rr = navi.solRov + navi.pSol * 3, *rb = navi.solRef + navi.pSol * 3;
    double bl[3] = {0}, pos[3], enu[3], len = 0.0, pitch = 0.0, yaw = 0.0;
    double cp, qq, az = 0.0;
    QColor col = Qt::white;
    const QColor gray(128,128,128), silver(192,192,192);
    int i, d1 = 10, d2 = 16, d3 = 10, cy = 0, sy = 0, cya = 0, sya = 0;
    int a, x1, x2, y1, y2, r1, digit;

    if (PMODE_DGPS <= navi.prcopt->mode && navi.prcopt->mode <= PMODE_FIXED) {
        int stat = navi.solStat[navi.pSol];
        col = navi.svr->state && stat && navi.solCurrentStat
                  ? STAT_COLOR[stat <= 7 ? stat : 0] : QColor(Qt::white);
        if (norm(rr, 3) > 0.0 && norm(rb, 3) > 0.0) {
            for (i = 0; i < 3; i++) bl[i] = rr[i] - rb[i];
        }
        if ((len = norm(bl, 3)) > 0.0) {
            ecef2pos(rb, pos); ecef2enu(pos, bl, enu);
            pitch = asin(enu[2] / len);
            yaw = atan2(enu[0], enu[1]); if (yaw < 0.0) yaw += 2.0 * PI;
            if (blMode) az = yaw;
        }
    }
    if (len >= MINBLLEN) {
        cp  = cos(pitch);
        cy  = (int)((r - d1 - d2 / 2) * cp * cos(yaw - az));
        sy  = (int)((r - d1 - d2 / 2) * cp * sin(yaw - az));
        cya = (int)(((r - d1 - d2 / 2) * cp - d2 / 2 - 4) * cos(yaw - az));
        sya = (int)(((r - d1 - d2 / 2) * cp - d2 / 2 - 4) * sin(yaw - az));
    }
    p1 = QPoint(p.x() - sy, p.y() + cy);        // base
    p2 = QPoint(p.x() + sy, p.y() - cy);        // rover

    c.setPen(gray);
    c.setBrush(Qt::white);
    c.drawEllipse(p, (int)r, (int)r);
    r1 = (int)(r - d1 / 2);
    c.drawEllipse(p, r1, r1);

    pp = pitch < 0.0 ? p2 : p1;
    c.setPen(silver);
    c.drawLine(p, pp);
    if (pitch < 0.0) {
        c.setBrush(Qt::white);
        c.drawEllipse(pp, d2 / 2, d2 / 2);
        drawArrow(c, p.x() + sya, p.y() - cya, d3, (int)((yaw - az) * R2D), silver);
    }
    c.setBrush(col);
    c.setPen(gray);
    c.drawEllipse(pp, d2 / 2 - 2, d2 / 2 - 2);
    for (a = 0; a < 360; a += 5) {
        qq = a % 90 == 0 ? 0 : (a % 30 == 0 ? r - d1 * 3 : (a % 10 == 0 ? r - d1 * 2 : r - d1));
        x1 = (int)(r * sin(a * D2R - az));
        y1 = (int)(r * cos(a * D2R - az));
        x2 = (int)(qq * sin(a * D2R - az));
        y2 = (int)(qq * cos(a * D2R - az));
        c.setPen(silver);
        c.drawLine(p.x() + x1, p.y() - y1, p.x() + x2, p.y() - y2);
        if (a % 90 == 0) {
            drawTextA(c, p.x() + x1, p.y() - y1, label[a / 90], gray, 0, 0);
        }
        if (a == 0) {
            x1 = (int)((r - d1 * 3 / 2) * sin(a * D2R - az));
            y1 = (int)((r - d1 * 3 / 2) * cos(a * D2R - az));
            drawArrow(c, p.x() + x1, p.y() - y1, d3, -(int)(az * R2D), silver);
        }
    }
    pp = pitch >= 0.0 ? p2 : p1;
    c.setPen(gray);
    c.drawLine(p, pp);
    if (pitch >= 0.0) {
        c.setBrush(Qt::white);
        c.drawEllipse(pp, d2 / 2, d2 / 2);
        drawArrow(c, p.x() + sya, p.y() - cya, d3, (int)((yaw - az) * R2D), gray);
    }
    c.setBrush(col);
    c.drawEllipse(pp, d2 / 2 - 2, d2 / 2 - 2);
    digit = len < 1000.0 ? 3 : (len < 10000.0 ? 2 : (len < 100000.0 ? 1 : 0));
    drawTextA(c, p.x(), p.y(), QString::asprintf("%.*f m", digit, len), gray, 0, 0);
    drawTextA(c, 3, h, QString::asprintf("Y: %.1f", yaw * R2D) + CHARDEG, gray, 1, 1);
    drawTextA(c, w - 3, h, QString::asprintf("P: %.1f", pitch * R2D) + CHARDEG, gray, 2, 1);
}
//------------------------------------------------------------------------------
// port of VCL DrawTrk() (TGraph replaced by direct QPainter drawing)
void PlotPanel::drawTrk(QPainter &c, int w, int h)
{
    const QColor gray(128,128,128), silver(192,192,192);
    double scl = TRK_SCALE[trkScale];   // m per tick-ish unit (VCL graph scale)
    double mpp = scl / 40.0;            // meters per pixel (tick ~40px)
    double ref[3], pos[3] = {0}, dr[3], enu[3];
    int i, j, k = 0, n = 0;

    static double xbuf[SOLBUF_SIZE], ybuf[SOLBUF_SIZE];
    static int cbuf[SOLBUF_SIZE];

    if (norm(navi.trkOri, 3) < 1E-6) {
        if (norm(navi.solRef + navi.pSol * 3, 3) > 1E-6) {
            matcpy(navi.trkOri, navi.solRef + navi.pSol * 3, 3, 1);
        }
        else {
            matcpy(navi.trkOri, navi.solRov + navi.pSol * 3, 3, 1);
        }
    }
    if (norm(navi.solRef + navi.pSol * 3, 3) > 1E-6) {
        matcpy(ref, navi.solRef + navi.pSol * 3, 3, 1);
    }
    else {
        matcpy(ref, navi.trkOri, 3, 1);
    }
    if (norm(ref, 3) > 1E-6) ecef2pos(ref, pos);

    for (i = navi.pSolS; i != navi.pSolE;) {
        for (j = 0; j < 3; j++) dr[j] = navi.solRov[j + i * 3] - ref[j];
        if (i == navi.pSol) k = n;
        ecef2enu(pos, dr, enu);
        xbuf[n] = enu[0];
        ybuf[n] = enu[1];
        cbuf[n++] = navi.solStat[i];
        if (++i >= SOLBUF_SIZE) i = 0;
    }
    double cx = n > 0 ? xbuf[k] : 0.0, cy = n > 0 ? ybuf[k] : 0.0;
    auto toX = [&](double xx) { return (int)(w / 2 + (xx - cx) / mpp); };
    auto toY = [&](double yy) { return (int)(h / 2 - (yy - cy) / mpp); };

    // grid (tick = scl meters)
    double tick = scl;
    if (trkType == 1) {                 // circles
        c.setPen(silver);
        c.setBrush(Qt::NoBrush);
        int x0 = toX(0.0), y0 = toY(0.0);
        for (i = 1; i <= 10; i++) {
            int rr = (int)(i * tick / mpp);
            c.drawEllipse(QPoint(x0, y0), rr, rr);
        }
        c.setPen(gray);
        c.drawLine(x0 - 5, y0, x0 + 5, y0);
        c.drawLine(x0, y0 - 5, x0, y0 + 5);
    }
    else {                              // grid lines
        double gx0 = cx - w / 2 * mpp, gx1 = cx + w / 2 * mpp;
        double gy0 = cy - h / 2 * mpp, gy1 = cy + h / 2 * mpp;
        c.setPen(QPen(silver, 1, Qt::DotLine));
        for (double gx = floor(gx0 / tick) * tick; gx <= gx1; gx += tick) {
            c.drawLine(toX(gx), 0, toX(gx), h);
        }
        for (double gy = floor(gy0 / tick) * tick; gy <= gy1; gy += tick) {
            c.drawLine(0, toY(gy), w, toY(gy));
        }
        // origin axes
        c.setPen(silver);
        c.drawLine(toX(0.0), 0, toX(0.0), h);
        c.drawLine(0, toY(0.0), w, toY(0.0));
    }
    // track polyline + marks
    if (n > 1) {
        c.setPen(silver);
        for (i = 1; i < n; i++) {
            c.drawLine(toX(xbuf[i - 1]), toY(ybuf[i - 1]), toX(xbuf[i]), toY(ybuf[i]));
        }
    }
    c.setPen(Qt::NoPen);
    for (i = 0; i < n; i++) {
        c.setBrush(STAT_COLOR[cbuf[i] <= 7 ? cbuf[i] : 0]);
        c.drawEllipse(QPoint(toX(xbuf[i]), toY(ybuf[i])), 2, 2);
    }
    // current position mark
    if (n > 0) {
        QPoint pc(toX(xbuf[k]), toY(ybuf[k]));
        QColor mk = navi.svr->state ? QColor(Qt::black) : gray;
        c.setPen(mk);
        c.setBrush(Qt::NoBrush);
        c.drawEllipse(pc, 8, 8);
        c.drawLine(pc.x() - 10, pc.y(), pc.x() + 10, pc.y());
        c.drawLine(pc.x(), pc.y() - 10, pc.x(), pc.y() + 10);
        c.setBrush(navi.svr->state ? STAT_COLOR[cbuf[k] <= 7 ? cbuf[k] : 0] : QColor(Qt::white));
        c.setPen(mk);
        c.drawEllipse(pc, 5, 5);
    }
    // scale bar
    int barw = (int)(tick / mpp);
    int bx = w - 35 - barw, by = h - 12;
    c.setPen(gray);
    c.drawLine(bx, by, bx + barw, by);
    c.drawLine(bx, by - 3, bx, by + 3);
    c.drawLine(bx + barw, by - 3, bx + barw, by + 3);
    QString label;
    if      (tick < 0.01)   label = QString::asprintf("%.0f mm", tick * 1000.0);
    else if (tick < 1.0)    label = QString::asprintf("%.0f cm", tick * 100.0);
    else if (tick < 1000.0) label = QString::asprintf("%.0f m",  tick);
    else                    label = QString::asprintf("%.0f km", tick / 1000.0);
    drawTextA(c, bx + barw / 2, by - 4, label, gray, 0, 1);
    // ref position
    if (norm(ref, 3) > 1E-6) {
        drawTextA(c, 3, h - 1,
                  QString::asprintf("%.9f", pos[0] * R2D) + CHARDEG +
                  QString::asprintf(" %.9f", pos[1] * R2D) + CHARDEG, gray, 1, 1);
    }
}
//------------------------------------------------------------------------------
