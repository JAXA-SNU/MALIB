//------------------------------------------------------------------------------
// plotwidget.cpp : plot canvas for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotdraw.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

#include "plotwidget.h"

// solution quality colors (same order as RTKPLOT) ------------------------------
static const QColor MARK_COLOR[8] = {
    QColor(192,192,192),                    // 0: none
    QColor(  0,128,  0),                    // 1: fix
    QColor(255,165,  0),                    // 2: float
    QColor(255,  0,255),                    // 3: sbas
    QColor(  0,  0,255),                    // 4: dgps
    QColor(255,  0,  0),                    // 5: single
    QColor(  0,128,128),                    // 6: ppp
    QColor(128, 96,  0)                     // 7: dr
};
static const char *AXIS_LABEL[3] = {"E-W", "N-S", "U-D"};
static const char *NSAT_LABEL[3] = {"# of Satellites", "Age of Diff (s)",
                                    "Ratio Factor"};
#define Y_MARGIN_L  50                      // left margin for the y labels
#define SQR(x)      ((x)*(x))

//------------------------------------------------------------------------------
PlotWidget::PlotWidget(PlotData *d, QWidget *parent) : QFrame(parent), data(d)
{
    setFrameStyle(QFrame::Panel | QFrame::Sunken);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    setMinimumSize(320, 240);
    setMouseTracking(true);
}
//------------------------------------------------------------------------------
void PlotWidget::setPlotType(int t)
{
    type = t;
    needFit = true;
    update();
}
//------------------------------------------------------------------------------
void PlotWidget::setQFlag(int q)
{
    qFlag = q;
    if (autoFit) needFit = true;
    update();
}
//------------------------------------------------------------------------------
void PlotWidget::setShowSol(int sel, bool on)
{
    showSol[sel] = on;
    if (autoFit) needFit = true;
    update();
}
//------------------------------------------------------------------------------
void PlotWidget::setAutoFit(bool on)
{
    autoFit = on;
    if (on) { needFit = true; update(); }
}
//------------------------------------------------------------------------------
void PlotWidget::dataChanged()
{
    if (autoFit) needFit = true;
    update();
}
//------------------------------------------------------------------------------
void PlotWidget::manualChange()
{
    if (autoFit) { autoFit = false; emit autoFitOff(); }
}
//------------------------------------------------------------------------------
void PlotWidget::setOpt(const PlotOpt *o)
{
    opt = o;
    setAutoFit(o->autoScale != 0);
    showInfo = o->showStats != 0;
    setFont(o->font);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, o->cColor[0]);
    setPalette(pal);
    needFit = true;
    update();
}
//------------------------------------------------------------------------------
QColor PlotWidget::markColor(int sel, int q) const
{
    int i = q >= 0 && q <= 7 ? q : 0;
    return opt ? opt->mColor[sel & 1][i] : MARK_COLOR[i];
}
//------------------------------------------------------------------------------
// background / grid / text colours and the grid + label switches
void PlotWidget::applyColors(Graph &g)
{
    if (!opt) return;
    g.color[0] = opt->cColor[0];
    g.color[1] = opt->cColor[1];
    g.color[2] = opt->cColor[2];
    g.xGrid = g.yGrid = opt->showGrid > 0 ? 1 : 0;
    g.timeFormat = opt->timeFormat;
}
//------------------------------------------------------------------------------
TimePos PlotWidget::seriesOf(int sel, int mode) const
{
    return data->solToPos(sel, -1, qFlag, mode);
}
//------------------------------------------------------------------------------
void PlotWidget::resizeEvent(QResizeEvent *event)
{
    layoutGraphs();
    QFrame::resizeEvent(event);
}
//------------------------------------------------------------------------------
// place the graph rectangles for the current plot type
void PlotWidget::layoutGraphs()
{
    QRect r = contentsRect();
    if (r.width() < 2 || r.height() < 2) return;

    graphT.setSize(r.width(), r.height());
    graphT.setPos(QPoint(2, 2), QPoint(r.width()-3, r.height()-3));

    int h = (r.height() - 4) / 3;
    for (int i = 0; i < 3; i++) {
        graphG[i].setSize(r.width(), r.height());
        graphG[i].setPos(QPoint(Y_MARGIN_L, 2 + i*h),
                         QPoint(r.width()-3, 2 + (i+1)*h - 2));
    }
    laidOut = true;
}
//------------------------------------------------------------------------------
// fit both axes to the data extent
void PlotWidget::fitRange()
{
    if (!laidOut) layoutGraphs();
    if (!laidOut) { needFit = true; return; }

    if (type == PLOT_TRK) {
        double xl[2] = {1E30, -1E30}, yl[2] = {1E30, -1E30};
        bool any = false;
        for (int s = 0; s < 2; s++) {
            if (!showSol[s] || data->solData[s].n <= 0) continue;
            TimePos pos = seriesOf(s, 0);
            for (int i = 0; i < pos.n; i++) {
                xl[0] = qMin(xl[0], pos.x[i]); xl[1] = qMax(xl[1], pos.x[i]);
                yl[0] = qMin(yl[0], pos.y[i]); yl[1] = qMax(yl[1], pos.y[i]);
                any = true;
            }
        }
        if (!any) return;
        // square aspect so the track is not distorted
        double w = qMax(xl[1]-xl[0], yl[1]-yl[0]) * 0.55 + 1E-3;
        QPoint p1, p2;
        graphT.getPos(p1, p2);
        graphT.setCent((xl[0]+xl[1])/2.0, (yl[0]+yl[1])/2.0);
        double s = 2.0 * w / qMax(qMin(p2.x()-p1.x(), p2.y()-p1.y()), 1);
        graphT.setScale(s, s);
        needFit = false;
        return;
    }

    int mode = type == PLOT_SOLV ? 1 : type == PLOT_SOLA ? 2 : 0;
    double ts = 1E30, te = -1E30;
    double lo[3] = {1E30, 1E30, 1E30}, hi[3] = {-1E30, -1E30, -1E30};
    bool any = false;

    for (int s = 0; s < 2; s++) {
        if (!showSol[s] || data->solData[s].n <= 0) continue;
        TimePos pos = type == PLOT_NSAT ? data->solToNsat(s, -1, qFlag)
                                        : seriesOf(s, mode);
        for (int i = 0; i < pos.n; i++) {
            double t = time2gpst(pos.t[i], NULL);
            ts = qMin(ts, t); te = qMax(te, t);
            double v[3] = {pos.x[i], pos.y[i], pos.z[i]};
            for (int k = 0; k < 3; k++) {
                lo[k] = qMin(lo[k], v[k]); hi[k] = qMax(hi[k], v[k]);
            }
            any = true;
        }
    }
    if (!any) return;
    if (te - ts < 1E-3) { ts -= 1.0; te += 1.0; }

    // "Auto Fit = OFF" in the options pins the value axis to +-Y-Range
    bool fixedY = opt && !opt->autoScale && type != PLOT_NSAT;

    for (int k = 0; k < 3; k++) {
        if (lo[k] > hi[k]) { lo[k] = -1.0; hi[k] = 1.0; }
        if (hi[k] - lo[k] < 1E-9) { lo[k] -= 1E-3; hi[k] += 1E-3; }
        double m = (hi[k] - lo[k]) * 0.08;
        double xl[2] = {ts, te}, yl[2] = {lo[k] - m, hi[k] + m};
        if (fixedY) { yl[0] = -opt->yRange; yl[1] = opt->yRange; }
        graphG[k].setLim(xl, yl);
    }
    needFit = false;
}
//------------------------------------------------------------------------------
void PlotWidget::fitHoriz()
{
    if (type == PLOT_TRK) { fitRange(); return; }

    // keep the y ranges, refit the time axis only
    double cy[3], sy[3], dummy;
    for (int k = 0; k < 3; k++) {
        graphG[k].getCent(dummy, cy[k]);
        graphG[k].getScale(dummy, sy[k]);
    }
    fitRange();
    for (int k = 0; k < 3; k++) {
        double cx, sx;
        graphG[k].getCent(cx, dummy);
        graphG[k].getScale(sx, dummy);
        graphG[k].setCent(cx, cy[k]);
        graphG[k].setScale(sx, sy[k]);
    }
}
//------------------------------------------------------------------------------
void PlotWidget::fitVert()
{
    if (type == PLOT_TRK) { fitRange(); return; }

    double cx[3], sx[3], dummy;
    for (int k = 0; k < 3; k++) {
        graphG[k].getCent(cx[k], dummy);
        graphG[k].getScale(sx[k], dummy);
    }
    fitRange();
    for (int k = 0; k < 3; k++) {
        double cy, sy;
        graphG[k].getCent(dummy, cy);
        graphG[k].getScale(dummy, sy);
        graphG[k].setCent(cx[k], cy);
        graphG[k].setScale(sx[k], sy);
    }
}
//------------------------------------------------------------------------------
void PlotWidget::centerOrigin()
{
    if (type == PLOT_TRK) {
        double xs, ys;
        graphT.getScale(xs, ys);
        graphT.setCent(0.0, 0.0);
        graphT.setScale(xs, ys);
    }
    else {
        for (int k = 0; k < 3; k++) {
            double cx, cy;
            graphG[k].getCent(cx, cy);
            graphG[k].setCent(cx, 0.0);
        }
    }
    update();
}
//------------------------------------------------------------------------------
void PlotWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    QRect r = contentsRect();
    p.fillRect(r, opt ? opt->cColor[0] : QColor(Qt::white));
    p.translate(r.topLeft());
    p.setFont(font());

    if (!laidOut) layoutGraphs();
    graphT.setPainter(&p);
    applyColors(graphT);
    for (int k = 0; k < 3; k++) {
        graphG[k].setPainter(&p);
        graphG[k].week = data->week;
        applyColors(graphG[k]);
    }
    if (needFit) fitRange();

    switch (type) {
    case PLOT_TRK:  drawTrack();     break;
    case PLOT_SOLP: drawSolution(0); break;
    case PLOT_SOLV: drawSolution(1); break;
    case PLOT_SOLA: drawSolution(2); break;
    case PLOT_NSAT: drawNsat();      break;
    default:
        graphT.drawText(QPoint(r.width()/2, r.height()/2),
                        "this plot type is not implemented yet",
                        QColor(128,128,128), 0, 0, 0);
        break;
    }
    p.resetTransform();
    QFrame::paintEvent(event);
}
//------------------------------------------------------------------------------
// ground track (port of DrawTrk)
void PlotWidget::drawTrack()
{
    int glabel = opt ? (opt->showGrid >= 2 ? 1 : 0) : 1;
    graphT.xLPos = graphT.yLPos = 7;        // labels on the axes
    graphT.title.clear();
    graphT.xLabel.clear(); graphT.yLabel.clear();

    if (trkCircles) graphT.drawCircles(glabel);
    else            graphT.drawAxis(opt ? opt->showGLabel > 0 : 1, glabel);

    int style = opt ? opt->plotStyle : 0;
    int msize = opt ? opt->markSize : 3;
    QColor lineCol = opt ? opt->cColor[3] : QColor(200,200,200);
    QColor textCol = opt ? opt->cColor[2] : QColor(Qt::black);

    int total = 0;
    TimePos last(1, true);
    for (int s = 0; s < 2; s++) {
        if (!showSol[s] || data->solData[s].n <= 0) continue;
        TimePos pos = seriesOf(s, 0);
        if (pos.n <= 0) continue;

        if (opt && opt->showErr) {          // error circle per epoch
            for (int i = 0; i < pos.n; i++) {
                QPoint q;
                if (!graphT.toPoint(pos.x[i], pos.y[i], q)) continue;
                double xs, ys;
                graphT.getScale(xs, ys);
                int rx = (int)(pos.xs[i]/xs + 0.5), ry = (int)(pos.ys[i]/ys + 0.5);
                if (rx < 1 && ry < 1) continue;
                graphT.drawCircle(q, opt->cColor[1], rx, ry, opt->showErr == 1 ? 0 : 1);
            }
        }
        if (!(style % 2))                   // 0:mark+line 2:line
            graphT.drawPoly(pos.x.constData(), pos.y.constData(), pos.n, lineCol, 0);
        if (style < 2) {                    // 0:mark+line 1:mark
            QVector<QColor> cols(pos.n);
            for (int i = 0; i < pos.n; i++) cols[i] = markColor(s, pos.q[i]);
            graphT.drawMarks(pos.x.constData(), pos.y.constData(), cols.constData(),
                             pos.n, 0, msize, 0);
        }
        if (opt && opt->showArrow && pos.n >= 2) {   // heading of the last leg
            double dx = pos.x[pos.n-1] - pos.x[pos.n-2];
            double dy = pos.y[pos.n-1] - pos.y[pos.n-2];
            QPoint q;
            if ((dx != 0.0 || dy != 0.0) &&
                graphT.toPoint(pos.x[pos.n-1], pos.y[pos.n-1], q)) {
                graphT.drawMark(q, 10, lineCol, 15, (int)(atan2(dx, dy)*R2D));
            }
        }
        // highlight the last epoch
        graphT.drawMark(pos.x[pos.n-1], pos.y[pos.n-1], 1, textCol, msize*2+6, 0);
        total += pos.n;
        if (last.n == 0) last = pos;
    }
    drawTrackExtras();
    if (showInfo && last.n > 0) drawInfoTrack(last);

    // say why nothing is drawn when the quality filter excluded everything:
    // MADOCA-PPP solutions are PPP (Q=6), so filtering on FLOAT shows nothing
    if (!total && qFlag)
        emit statusChanged(QString("0 points  (quality filter = %1)")
                               .arg(SOLQ_LABEL[qFlag & 7]));
    else
        emit statusChanged(QString("%1 points").arg(total));
}
//------------------------------------------------------------------------------
// scale bar and compass rose on the track plot (port of DrawTrkScale/Compass)
void PlotWidget::drawTrackExtras()
{
    if (!opt) return;
    QPoint p1, p2;
    graphT.getPos(p1, p2);
    QColor c = opt->cColor[2];

    if (opt->showScale) {
        double xt, yt;
        graphT.getTick(xt, yt);
        double xs = graphT.xScaleValue();
        int len = (int)(xt / xs + 0.5);
        if (len > 4) {
            QPoint q(p2.x() - 35 - len/2, p2.y() - 20);
            graphT.drawMark(q, 11, c, len, 0);
            QString s = xt < 0.01  ? QString("%1 mm").arg(xt*1000, 0, 'g', 3)
                      : xt < 1.0   ? QString("%1 cm").arg(xt*100,  0, 'g', 3)
                      : xt < 1000.0? QString("%1 m").arg(xt,       0, 'g', 3)
                                   : QString("%1 km").arg(xt/1000, 0, 'g', 3);
            graphT.drawText(QPoint(q.x(), q.y() - 3), s, c, 0, 2, 0);
        }
    }
    if (opt->showCompass) {
        QPoint q(p1.x() + 25, p1.y() + 30);
        graphT.drawMark(q, 1, c, 36, 0);            // ring
        graphT.drawMark(q, 10, c, 30, 0);           // north arrow
        graphT.drawText(QPoint(q.x(), q.y() - 22), "N", c, 0, 2, 0);
    }
}
//------------------------------------------------------------------------------
// position / velocity / acceleration vs time (port of DrawSol)
void PlotWidget::drawSolution(int mode)
{
    static const char *unit[3] = {"m", "m/s", "m/s^2"};

    QVector<TimePos> series;
    for (int s = 0; s < 2; s++) {
        if (!showSol[s] || data->solData[s].n <= 0) continue;
        series.append(seriesOf(s, mode));
    }
    for (int axis = 0; axis < 3; axis++) {
        Graph &g = graphG[axis];
        g.xLPos = axis == 2 ? 6 : 0;         // time labels on the bottom panel
        g.yLPos = 1;
        g.xLabel.clear();
        g.yLabel = opt && !opt->showGLabel ? QString()
                 : QString("%1 (%2)").arg(AXIS_LABEL[axis]).arg(unit[mode]);
        g.drawAxis(opt ? opt->showGLabel > 0 : 1,
                   opt ? (opt->showGrid >= 2 ? 1 : 1) : 1);

        int style = opt ? opt->plotStyle : 0;
        int msize = opt ? opt->markSize : 2;
        QColor lineCol = opt ? opt->cColor[3] : QColor(200,200,200);

        for (int si = 0; si < series.size(); si++) {
            const TimePos &pos = series[si];
            if (pos.n <= 0) continue;
            QVector<double> tx(pos.n), vy(pos.n);
            QVector<QColor> cols(pos.n);
            for (int i = 0; i < pos.n; i++) {
                tx[i] = time2gpst(pos.t[i], NULL);
                vy[i] = axis == 0 ? pos.x[i] : axis == 1 ? pos.y[i] : pos.z[i];
                cols[i] = markColor(si, pos.q[i]);
            }
            if (opt && opt->showErr) {              // +-1 sigma bar
                const QVector<double> &sd = axis == 0 ? pos.xs :
                                            axis == 1 ? pos.ys : pos.zs;
                for (int i = 0; i < pos.n; i++) {
                    if (sd[i] <= 0.0) continue;
                    QPoint a, b;
                    if (!g.toPoint(tx[i], vy[i]-sd[i], a)) continue;
                    if (!g.toPoint(tx[i], vy[i]+sd[i], b)) continue;
                    QPoint seg[2] = {a, b};
                    g.drawPoly(seg, 2, opt->cColor[1], opt->showErr == 1 ? 0 : 1);
                }
            }
            if (!(style % 2))
                g.drawPoly(tx.constData(), vy.constData(), pos.n, lineCol, 0);
            if (style < 2)
                g.drawMarks(tx.constData(), vy.constData(), cols.constData(),
                            pos.n, 0, msize, 0);
        }
        if (showInfo && !series.isEmpty())
            drawInfoSol(axis, series.first(), axis, unit[mode]);
    }
}
//------------------------------------------------------------------------------
// number of satellites / age / ratio (port of DrawNsat)
void PlotWidget::drawNsat()
{
    QVector<TimePos> series;
    for (int s = 0; s < 2; s++) {
        if (!showSol[s] || data->solData[s].n <= 0) continue;
        series.append(data->solToNsat(s, -1, qFlag));
    }
    for (int k = 0; k < 3; k++) {
        Graph &g = graphG[k];
        g.xLPos = k == 2 ? 6 : 0;
        g.yLPos = 1;
        g.xLabel.clear();
        g.yLabel = opt && !opt->showGLabel ? QString() : NSAT_LABEL[k];
        g.drawAxis(opt ? opt->showGLabel > 0 : 1, 1);

        int style = opt ? opt->plotStyle : 0;
        int msize = opt ? opt->markSize : 2;
        QColor lineCol = opt ? opt->cColor[3] : QColor(200,200,200);

        for (int si = 0; si < series.size(); si++) {
            const TimePos &ns = series[si];
            if (ns.n <= 0) continue;
            QVector<double> tx(ns.n), vy(ns.n);
            QVector<QColor> cols(ns.n);
            for (int i = 0; i < ns.n; i++) {
                tx[i] = time2gpst(ns.t[i], NULL);
                vy[i] = k == 0 ? ns.x[i] : k == 1 ? ns.y[i] : ns.z[i];
                cols[i] = markColor(si, ns.q[i]);
            }
            if (!(style % 2))
                g.drawPoly(tx.constData(), vy.constData(), ns.n, lineCol, 0);
            if (style < 2)
                g.drawMarks(tx.constData(), vy.constData(), cols.constData(),
                            ns.n, 0, msize, 0);
        }
    }
}
//------------------------------------------------------------------------------
// ORI / AVE / STD / RMS overlay on the track plot (port of DrawInfoTrk)
void PlotWidget::drawInfoTrack(const TimePos &pos)
{
    double ave[3] = {0}, std[3] = {0}, rms[3] = {0};
    if (pos.n <= 0) return;

    for (int i = 0; i < pos.n; i++) {
        ave[0] += pos.x[i]; ave[1] += pos.y[i]; ave[2] += pos.z[i];
    }
    for (int k = 0; k < 3; k++) ave[k] /= pos.n;
    for (int i = 0; i < pos.n; i++) {
        double v[3] = {pos.x[i], pos.y[i], pos.z[i]};
        for (int k = 0; k < 3; k++) {
            std[k] += SQR(v[k] - ave[k]);
            rms[k] += SQR(v[k]);
        }
    }
    for (int k = 0; k < 3; k++) {
        std[k] = pos.n > 1 ? sqrt(std[k] / (pos.n - 1)) : 0.0;
        rms[k] = sqrt(rms[k] / pos.n);
    }
    double opos[3], p[3];
    for (int k = 0; k < 3; k++) opos[k] = data->origin()[k];
    ecef2pos(opos, p);

    QPoint p1, p2;
    graphT.getPos(p1, p2);
    // right aligned, top aligned: va=1 would put the first line a whole line
    // above the graph and it got clipped by the top edge
    QPoint tp(p2.x() - 5, p1.y() + 3);
    QColor c = opt ? opt->cColor[2] : QColor(Qt::black);
    QStringList s;
    if (opt && opt->latLonFormat) {         // ddd mm ss.ss
        double dms1[3], dms2[3];
        deg2dms(p[0]*R2D, dms1, 2);
        deg2dms(p[1]*R2D, dms2, 2);
        s << QString::asprintf("ORI = %.0f° %.0f' %.2f\" N  "
                               "%.0f° %.0f' %.2f\" E  %.4fm",
                               dms1[0], dms1[1], dms1[2],
                               dms2[0], dms2[1], dms2[2], p[2]);
    }
    else {
        s << QString::asprintf("ORI = %.8f°N %.8f°E %.4fm",
                               p[0]*R2D, p[1]*R2D, p[2]);
    }
    s << QString::asprintf("AVE = E:%.4fm N:%.4fm U:%.4fm", ave[0], ave[1], ave[2]);
    s << QString::asprintf("STD = E:%.4fm N:%.4fm U:%.4fm", std[0], std[1], std[2]);
    s << QString::asprintf("RMS = E:%.4fm N:%.4fm U:%.4fm 2D:%.4fm",
                           rms[0], rms[1], rms[2], sqrt(SQR(rms[0]) + SQR(rms[1])));
    QFontMetrics fm(font());
    for (int i = 0; i < s.size(); i++) {
        graphT.drawText(QPoint(tp.x(), tp.y() + i*fm.height()), s[i], c, 2, 2, 0);
    }
}
//------------------------------------------------------------------------------
// AVE / STD / RMS overlay on one time-series panel (port of DrawInfoSol)
void PlotWidget::drawInfoSol(int panel, const TimePos &pos, int axis,
                             const char *unit)
{
    if (pos.n <= 0) return;
    const QVector<double> &v = axis == 0 ? pos.x : axis == 1 ? pos.y : pos.z;

    double ave = 0.0, std = 0.0, rms = 0.0;
    for (int i = 0; i < pos.n; i++) ave += v[i];
    ave /= pos.n;
    for (int i = 0; i < pos.n; i++) { std += SQR(v[i] - ave); rms += SQR(v[i]); }
    std = pos.n > 1 ? sqrt(std / (pos.n - 1)) : 0.0;
    rms = sqrt(rms / pos.n);

    QPoint p1, p2;
    graphG[panel].getPos(p1, p2);
    QString s = QString::asprintf("AVE=%.4f%s STD=%.4f%s RMS=%.4f%s",
                                  ave, unit, std, unit, rms, unit);
    graphG[panel].drawText(QPoint(p2.x() - 5, p1.y() + 3), s,
                           opt ? opt->cColor[2] : QColor(Qt::black), 2, 2, 0);
}
//------------------------------------------------------------------------------
// which time-series panel contains p (widget coordinates)
int PlotWidget::panelAt(const QPoint &p) const
{
    QPoint q = p - contentsRect().topLeft();
    for (int k = 0; k < 3; k++) {
        QPoint p1, p2;
        graphG[k].getPos(p1, p2);
        if (q.y() >= p1.y() && q.y() <= p2.y()) return k;
    }
    return -1;
}
//------------------------------------------------------------------------------
void PlotWidget::panX(int dpix)
{
    for (int k = 0; k < 3; k++) {
        double cx, cy, xs, ys;
        graphG[k].getCent(cx, cy);
        graphG[k].getScale(xs, ys);
        graphG[k].setCent(cx - dpix*xs, cy);
    }
}
//------------------------------------------------------------------------------
void PlotWidget::zoomX(double f)
{
    for (int k = 0; k < 3; k++) {
        double xs, ys;
        graphG[k].getScale(xs, ys);
        graphG[k].setScale(xs*f, ys);
    }
}
//------------------------------------------------------------------------------
//   left  drag : pan (scroll)
//   right drag : stretch the time axis, keeping the value axis as it is
void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
        return;
    dragPos = event->pos();
    dragPanel = isTimeSeries() ? panelAt(event->pos()) : -1;
    dragging = true;
    dragZoom = event->button() == Qt::RightButton;
    setCursor(dragZoom ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
}
//------------------------------------------------------------------------------
void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging) {
        QPoint d = event->pos() - dragPos;
        dragPos = event->pos();
        manualChange();

        if (dragZoom) {
            // same feel as the VCL original: 100 px of travel halves/doubles
            double f = pow(2.0, -d.x() / 100.0);
            if (type == PLOT_TRK) {
                double xs, ys;
                graphT.getScale(xs, ys);
                graphT.setScale(xs*f, ys*f);
            }
            else zoomX(f);                  // time axis only, y untouched
            update();
            return;
        }
        if (type == PLOT_TRK) {
            double xs, ys, cx, cy;
            graphT.getScale(xs, ys);
            graphT.getCent(cx, cy);
            graphT.setCent(cx - d.x()*xs, cy + d.y()*ys);
        }
        else {
            panX(d.x());                    // time axis: all panels together
            if (dragPanel >= 0) {           // y axis: only the panel grabbed
                double cx, cy, xs, ys;
                graphG[dragPanel].getCent(cx, cy);
                graphG[dragPanel].getScale(xs, ys);
                graphG[dragPanel].setCent(cx, cy + d.y()*ys);
            }
        }
        update();
        return;
    }
    // report the position under the cursor
    QPoint q = event->pos() - contentsRect().topLeft();
    double px, py;
    if (type == PLOT_TRK) {
        graphT.toPos(q, px, py);
        emit statusChanged(QString::asprintf("E:%.4fm N:%.4fm", px, py));
    }
    else {
        int k = panelAt(event->pos());
        if (k < 0) return;
        graphG[k].toPos(q, px, py);
        emit statusChanged(QString("%1  %2")
                               .arg(graphG[k].timeText(px, 1.0))
                               .arg(py, 0, 'f', 4));
    }
}
//------------------------------------------------------------------------------
void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    (void)event;
    dragging = false;
    dragZoom = false;
    dragPanel = -1;
    setCursor(Qt::ArrowCursor);
}
//------------------------------------------------------------------------------
// double click centres the view at the clicked point (port of DispDblClick)
void PlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    QPoint q = event->pos() - contentsRect().topLeft();
    double px, py;
    manualChange();

    if (type == PLOT_TRK) {
        graphT.toPos(q, px, py);
        graphT.setCent(px, py);
    }
    else {
        int k = panelAt(event->pos());
        if (k < 0) return;
        graphG[k].toPos(q, px, py);
        for (int i = 0; i < 3; i++) {       // x is shared, y is per panel
            double cx, cy;
            graphG[i].getCent(cx, cy);
            graphG[i].setCent(px, i == k ? py : cy);
        }
    }
    update();
}
//------------------------------------------------------------------------------
//   wheel        : zoom the time axis (both axes on the track plot)
//   shift+wheel  : zoom the value axis of the panel under the cursor
void PlotWidget::wheelEvent(QWheelEvent *event)
{
    double f = event->angleDelta().y() > 0 ? 1.0/1.2 : 1.2;
    manualChange();

    if (type == PLOT_TRK) {
        double xs, ys;
        graphT.getScale(xs, ys);
        graphT.setScale(xs*f, ys*f);
    }
    else if (event->modifiers() & Qt::ShiftModifier) {
        int k = panelAt(event->position().toPoint());
        for (int i = 0; i < 3; i++) {
            if (k >= 0 && i != k) continue;
            double xs, ys;
            graphG[i].getScale(xs, ys);
            graphG[i].setScale(xs, ys*f);
        }
    }
    else zoomX(f);

    update();
    event->accept();
}
//------------------------------------------------------------------------------
