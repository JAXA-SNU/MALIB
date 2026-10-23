//------------------------------------------------------------------------------
// graph.cpp : 2-D graph plotting engine
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/appcmn/graph (RTKLIB 2.4.3 b35).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "rtklib.h"

#include "graph.h"

#define MINSIZE     10          // min width/height
#define MINSCALE    2E-5        // min scale factor (pixel/unit)
#define MAXSCALE    1E7         // max scale factor (pixel/unit)
#define MAXCIRCLES  100         // max number of circles
#define SIZEORIGIN  6

//------------------------------------------------------------------------------
Graph::Graph(QPainter *p) : painter(p)
{
}
//------------------------------------------------------------------------------
void Graph::setSize(int w, int h)
{
    width = w; height = h;
}
//------------------------------------------------------------------------------
bool Graph::isInArea(const QPoint &p) const
{
    return x <= p.x() && p.x() < x + width && y <= p.y() && p.y() < y + height;
}
//------------------------------------------------------------------------------
bool Graph::toPoint(double px, double py, QPoint &p) const
{
    const double xt = 0.1;
    double sx = x + (width  - 1) / 2.0 + (px - xCent) / xScale;
    double sy = y + (height - 1) / 2.0 - (py - yCent) / yScale;
    // clip to avoid numerical exceptions on extreme zoom
    if      (sx < -1E6) sx = -1E6;
    else if (sx >  1E6) sx =  1E6;
    if      (sy < -1E6) sy = -1E6;
    else if (sy >  1E6) sy =  1E6;
    p.setX((int)floor(sx + 0.5));
    p.setY((int)floor(sy + 0.5));
    return x - xt < sx && sx < x + width - 1 + xt &&
           y - xt < sy && sy < y + height - 1 + xt;
}
//------------------------------------------------------------------------------
void Graph::toPos(const QPoint &p, double &px, double &py) const
{
    px = xCent + (p.x() - x - (width  - 1) / 2.0) * xScale;
    py = yCent - (p.y() - y - (height - 1) / 2.0) * yScale;
}
//------------------------------------------------------------------------------
void Graph::setPos(const QPoint &p1, const QPoint &p2)
{
    int w = p2.x() - p1.x() + 1, h = p2.y() - p1.y() + 1;
    if (w < MINSIZE) w = MINSIZE;
    if (h < MINSIZE) h = MINSIZE;
    if (fit) {
        xScale *= (double)(width  - 1) / (w - 1);
        yScale *= (double)(height - 1) / (h - 1);
    }
    x = p1.x(); y = p1.y(); width = w; height = h;
}
//------------------------------------------------------------------------------
void Graph::getPos(QPoint &p1, QPoint &p2) const
{
    p1 = QPoint(x, y);
    p2 = QPoint(x + width - 1, y + height - 1);
}
//------------------------------------------------------------------------------
void Graph::setCent(double px, double py) { xCent = px; yCent = py; }
void Graph::getCent(double &px, double &py) const { px = xCent; py = yCent; }
//------------------------------------------------------------------------------
void Graph::setRight(double px, double py)
{
    xCent = px - (double)(width - 13) * xScale * 0.5; yCent = py;
}
void Graph::getRight(double &px, double &py) const
{
    px = xCent + (double)(width - 13) * xScale * 0.5; py = yCent;
}
//------------------------------------------------------------------------------
void Graph::setScale(double xs, double ys)
{
    if (xs < MINSCALE) xs = MINSCALE; else if (xs > MAXSCALE) xs = MAXSCALE;
    if (ys < MINSCALE) ys = MINSCALE; else if (ys > MAXSCALE) ys = MAXSCALE;
    xScale = xs; yScale = ys;
}
void Graph::getScale(double &xs, double &ys) const { xs = xScale; ys = yScale; }
//------------------------------------------------------------------------------
void Graph::setLim(const double *xl, const double *yl)
{
    if (xl[0] < xl[1]) { xCent = (xl[0]+xl[1])/2.0; xScale = (xl[1]-xl[0])/(width -1); }
    if (yl[0] < yl[1]) { yCent = (yl[0]+yl[1])/2.0; yScale = (yl[1]-yl[0])/(height-1); }
}
//------------------------------------------------------------------------------
void Graph::getLim(double *xl, double *yl) const
{
    toPos(QPoint(x, y), xl[0], yl[1]);
    toPos(QPoint(x + width - 1, y + height - 1), xl[1], yl[0]);
}
//------------------------------------------------------------------------------
void Graph::setTick(double xt, double yt) { xTick = xt; yTick = yt; }
void Graph::getTick(double &xt, double &yt) const
{
    xt = xTick > 0.0 ? xTick
       : (xLPos == 5 || xLPos == 6 ? autoTickTime(xScale) : autoTick(xScale));
    yt = yTick > 0.0 ? yTick : autoTick(yScale);
}
//------------------------------------------------------------------------------
double Graph::autoTick(double scale) const
{
    double t[] = {1.0, 2.0, 5.0, 10.0}, tick = 30.0 * scale;
    double order = pow(10.0, floor(log10(tick)));
    for (int i = 0; i < 4; i++) if (tick <= t[i] * order) return t[i] * order;
    return 10.0;
}
//------------------------------------------------------------------------------
double Graph::autoTickTime(double scale) const
{
    static const double t[] = {
        0.1,0.2,0.5,1.0,3.0,6.0,12.0,30.0,60.0,300.0,900.0,1800.0,3600.0,
        7200.0,10800.0,21600.0,43200.0,86400.0,86400.0*2,86400.0*7,86400.0*14,
        86400.0*35,86400.0*70
    };
    double tick = 60.0 * scale;
    for (size_t i = 0; i < sizeof(t)/sizeof(*t); i++) if (tick <= t[i]) return t[i];
    return 86400.0 * 140;
}
//------------------------------------------------------------------------------
QString Graph::numText(double px, double dx) const
{
    int n = (int)(0.9 - log10(dx));
    return QString::asprintf("%.*f", n < 0 ? 0 : n, px);
}
//------------------------------------------------------------------------------
QString Graph::timeText(double px, double dx) const
{
    char str[64];

    if (timeFormat == 0) {                  // gps week + time of week
        return QString::asprintf("%d %.*f", week, dx < 0.01 ? 3 : dx < 1.0 ? 2 : 0,
                                 px);
    }
    gtime_t t = gpst2time(week, px);
    if (timeFormat == 2) t = gpst2utc(t);   // utc
    else if (timeFormat == 3) t = timeadd(gpst2utc(t), 9*3600.0);   // jst

    time2str(t, str, 1);
    int b = dx < 86400.0 ? 11 : (dx < 86400.0*30 ? 5 : 2);
    int w = dx < 60.0 ? (dx < 1.0 ? 10 : 8) : 5;
    return QString::asprintf("%*.*s", w, w, str + b);
}
//------------------------------------------------------------------------------
bool Graph::onAxis(const QPoint &p) const
{
    return x <= p.x() && p.x() <= x + width - 1 &&
           y <= p.y() && p.y() <= y + height - 1;
}
//------------------------------------------------------------------------------
void Graph::drawGrid(double xt, double yt)
{
    double xl[2], yl[2];
    QPoint p;
    getLim(xl, yl);

    if (xGrid) {
        for (int i = (int)ceil(xl[0]/xt); i*xt <= xl[1]; i++) {
            toPoint(i*xt, 0.0, p);
            painter->setPen(QPen(color[1], 1, i != 0 ? Qt::DotLine : Qt::SolidLine));
            painter->drawLine(p.x(), y, p.x(), y + height - 1);
        }
    }
    if (yGrid) {
        for (int i = (int)ceil(yl[0]/yt); i*yt <= yl[1]; i++) {
            toPoint(0.0, i*yt, p);
            painter->setPen(QPen(color[1], 1, i != 0 ? Qt::DotLine : Qt::SolidLine));
            painter->drawLine(x, p.y(), x + width - 1, p.y());
        }
    }
    drawMark(0.0, 0.0, 0, color[1], SIZEORIGIN, 0);
}
//------------------------------------------------------------------------------
void Graph::drawGridLabel(double xt, double yt)
{
    double xl[2], yl[2];
    QPoint p;
    getLim(xl, yl);

    if (xLPos) {
        for (int i = (int)ceil(xl[0]/xt); i*xt <= xl[1]; i++) {
            if (xLPos <= 4) {
                toPoint(i*xt, yl[0], p);
                if (xLPos == 1) p.setY(p.y() - 1);
                int ha = xLPos <= 2 ? 0 : (xLPos == 3 ? 2 : 1);
                int va = xLPos >= 3 ? 0 : (xLPos == 1 ? 2 : 1);
                drawText(p, numText(i*xt, xt), color[2], ha, va, xLPos >= 3 ? 90 : 0);
            }
            else if (xLPos == 6) {
                toPoint(i*xt, yl[0], p);
                drawText(p, timeText(i*xt, xt), color[2], 0, 2, 0);
            }
            else if (xLPos == 7) {
                if (i == 0) continue;
                toPoint(i*xt, 0.0, p);
                drawText(p, numText(i*xt, xt), color[2], 0, 2, 0);
            }
        }
    }
    if (yLPos) {
        for (int i = (int)ceil(yl[0]/yt); i*yt <= yl[1]; i++) {
            if (yLPos <= 4) {
                toPoint(xl[0], i*yt, p);
                int ha = yLPos >= 3 ? 0 : (yLPos == 1 ? 2 : 1);
                int va = yLPos <= 2 ? 0 : (yLPos == 3 ? 1 : 2);
                drawText(p, numText(i*yt, yt), color[2], ha, va, yLPos >= 3 ? 90 : 0);
            }
            else if (yLPos == 7) {
                if (i == 0) continue;
                toPoint(0.0, i*yt, p);
                p.setX(p.x() + 2);
                drawText(p, numText(i*yt, yt), color[2], 1, 0, 0);
            }
        }
    }
}
//------------------------------------------------------------------------------
void Graph::drawBox()
{
    if (!box) return;
    painter->setPen(QPen(color[1], 1, Qt::SolidLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(x, y, width - 1, height - 1);
}
//------------------------------------------------------------------------------
void Graph::drawLabel()
{
    if (!xLabel.isEmpty()) {
        drawText(QPoint(x + width/2, y + height + (xLPos % 2 ? 10 : 2)),
                 xLabel, color[2], 0, 2, 0);
    }
    if (!yLabel.isEmpty()) {
        drawText(QPoint(x - (yLPos % 2 ? 20 : 2), y + height/2),
                 yLabel, color[2], 0, 1, 90);
    }
    if (!title.isEmpty()) {
        drawText(QPoint(x + width/2, y - 1), title, color[2], 0, 1, 0);
    }
}
//------------------------------------------------------------------------------
void Graph::drawAxis(int label, int glabel)
{
    double xt, yt;
    getTick(xt, yt);

    painter->fillRect(x, y, width, height, color[0]);
    drawGrid(xt, yt);
    // thin out labels when ticks get dense
    if (xt / xScale < 50.0 && xLPos <= 2) xt *= (xLPos == 5 ? 4.0 : 2.0);
    if (yt / yScale < 50.0 && yLPos <= 2) yt *= 2.0;
    if (glabel) drawGridLabel(xt, yt);
    drawBox();
    if (label) drawLabel();
}
//------------------------------------------------------------------------------
void Graph::drawMark(const QPoint &p, int mark, const QColor &c, int size, int rot)
{
    drawMark(p, mark, c, color[0], size, rot);
}
//------------------------------------------------------------------------------
void Graph::drawMark(double px, double py, int mark, const QColor &c, int size,
                     int rot)
{
    QPoint p;
    if (!toPoint(px, py, p)) return;
    drawMark(p, mark, c, color[0], size, rot);
}
//------------------------------------------------------------------------------
// mark: 0:dot 1:circle 2:rect 3:cross 4:line 5:plus 10:arrow 11:hscale
void Graph::drawMark(const QPoint &p, int mark, const QColor &c, const QColor &bg,
                     int size, int rot)
{
    if (!painter) return;
    int s = size < 1 ? 1 : size, h = s / 2;

    painter->save();
    painter->setPen(c);
    switch (mark) {
    case 0:                                 // dot (filled circle)
        painter->setBrush(c);
        painter->drawEllipse(p, h, h);
        break;
    case 1:                                 // circle
        painter->setBrush(bg == color[0] ? QBrush(Qt::NoBrush) : QBrush(bg));
        painter->drawEllipse(p, h, h);
        break;
    case 2:                                 // rectangle
        painter->setBrush(bg == color[0] ? QBrush(Qt::NoBrush) : QBrush(bg));
        painter->drawRect(p.x() - h, p.y() - h, s, s);
        break;
    case 3:                                 // cross
        painter->drawLine(p.x() - h, p.y() - h, p.x() + h, p.y() + h);
        painter->drawLine(p.x() - h, p.y() + h, p.x() + h, p.y() - h);
        break;
    case 4: {                               // line (rotated)
        double a = rot * D2R;
        int dx = (int)(h * cos(a)), dy = (int)(h * sin(a));
        painter->drawLine(p.x() - dx, p.y() + dy, p.x() + dx, p.y() - dy);
        break;
    }
    case 5:                                 // plus
        painter->drawLine(p.x() - h, p.y(), p.x() + h, p.y());
        painter->drawLine(p.x(), p.y() - h, p.x(), p.y() + h);
        break;
    case 10: {                              // arrow
        double a = rot * D2R;
        QPoint tri[3] = {
            QPoint(p.x() + (int)(h*sin(a)),                p.y() - (int)(h*cos(a))),
            QPoint(p.x() + (int)(h*sin(a+2.5)),            p.y() - (int)(h*cos(a+2.5))),
            QPoint(p.x() + (int)(h*sin(a-2.5)),            p.y() - (int)(h*cos(a-2.5)))
        };
        painter->setBrush(c);
        painter->drawPolygon(tri, 3);
        break;
    }
    case 11:                                // horizontal scale bar
        painter->drawLine(p.x() - h, p.y(), p.x() + h, p.y());
        painter->drawLine(p.x() - h, p.y() - 3, p.x() - h, p.y() + 3);
        painter->drawLine(p.x() + h, p.y() - 3, p.x() + h, p.y() + 3);
        break;
    default:
        painter->setBrush(c);
        painter->drawEllipse(p, h, h);
        break;
    }
    painter->restore();
}
//------------------------------------------------------------------------------
void Graph::drawMarks(const double *px, const double *py, const QColor *c, int n,
                      int mark, int size, int rot)
{
    QPoint p;
    for (int i = 0; i < n; i++) {
        if (!toPoint(px[i], py[i], p)) continue;
        drawMark(p, mark, c[i], color[0], size, rot);
    }
}
//------------------------------------------------------------------------------
void Graph::drawCircle(const QPoint &p, const QColor &c, int rx, int ry, int style)
{
    if (!painter) return;
    painter->save();
    painter->setPen(QPen(c, 1, style ? Qt::DotLine : Qt::SolidLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(p, rx, ry);
    painter->restore();
}
//------------------------------------------------------------------------------
// concentric range rings around the origin (used by the track plot)
void Graph::drawCircles(int label)
{
    double xl[2], yl[2], xt, yt;
    getLim(xl, yl);
    getTick(xt, yt);
    double t = xt > yt ? xt : yt;
    double rmax = sqrt(xl[1]*xl[1] + yl[1]*yl[1]);

    QPoint org;
    toPoint(0.0, 0.0, org);
    for (int i = 1; i <= MAXCIRCLES; i++) {
        double r = i * t;
        if (r > rmax) break;
        QPoint p;
        toPoint(r, 0.0, p);
        int rx = p.x() - org.x();
        toPoint(0.0, r, p);
        int ry = org.y() - p.y();
        drawCircle(org, color[1], rx, ry, 1);
        if (label && xLPos) {
            drawText(QPoint(org.x() + rx, org.y()), numText(r, t), color[2], 2, 0, 0);
        }
    }
    painter->setPen(color[1]);
    painter->drawLine(org.x(), y, org.x(), y + height - 1);
    painter->drawLine(x, org.y(), x + width - 1, org.y());
}
//------------------------------------------------------------------------------
// ha 0:center 1:left 2:right / va 0:center 1:bottom 2:top / rot in degrees
void Graph::drawText(const QPoint &p, const QString &s, const QColor &c,
                     int ha, int va, int rot)
{
    drawText(p, s, c, color[0], ha, va, rot);
}
//------------------------------------------------------------------------------
void Graph::drawText(double px, double py, const QString &s, const QColor &c,
                     int ha, int va, int rot)
{
    QPoint p;
    if (!toPoint(px, py, p)) return;
    drawText(p, s, c, color[0], ha, va, rot);
}
//------------------------------------------------------------------------------
void Graph::drawText(const QPoint &p, const QString &s, const QColor &c,
                     const QColor &bg, int ha, int va, int rot)
{
    if (!painter || s.isEmpty()) return;

    QFontMetrics fm(painter->font());
    int w = fm.horizontalAdvance(s), h = fm.height();

    painter->save();
    painter->translate(p);
    if (rot) painter->rotate(-rot);

    int tx = 0, ty = 0;
    if      (ha == 0) tx = -w / 2;
    else if (ha == 2) tx = -w;
    if      (va == 0) ty = -h / 2;
    else if (va == 1) ty = -h;

    if (bg != color[0]) painter->fillRect(tx, ty, w, h, bg);
    painter->setPen(c);
    painter->drawText(tx, ty + fm.ascent(), s);
    painter->restore();
}
//------------------------------------------------------------------------------
void Graph::drawPoly(const QPoint *p, int n, const QColor &c, int style)
{
    if (!painter || n < 2) return;
    painter->save();
    painter->setPen(QPen(c, 1, style == 1 ? Qt::DotLine :
                               style == 2 ? Qt::DashLine : Qt::SolidLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(p, n);
    painter->restore();
}
//------------------------------------------------------------------------------
void Graph::drawPoly(const double *px, const double *py, int n, const QColor &c,
                     int style)
{
    if (n < 2) return;
    QVector<QPoint> pts;
    pts.reserve(n);
    QPoint p;
    for (int i = 0; i < n; i++) {
        toPoint(px[i], py[i], p);           // keep off-screen points for clipping
        pts.append(p);
    }
    drawPoly(pts.constData(), pts.size(), c, style);
}
//------------------------------------------------------------------------------
void Graph::drawPatch(const double *px, const double *py, int n,
                      const QColor &c1, const QColor &c2, int style)
{
    if (!painter || n < 2) return;
    QVector<QPoint> pts;
    pts.reserve(n);
    QPoint p;
    for (int i = 0; i < n; i++) { toPoint(px[i], py[i], p); pts.append(p); }

    painter->save();
    painter->setPen(style ? QPen(c1) : QPen(Qt::NoPen));
    painter->setBrush(c2);
    painter->drawPolygon(pts.constData(), pts.size());
    painter->restore();
}
//------------------------------------------------------------------------------
// sky plot frame: elevation rings every 30 deg and azimuth spokes every 45 deg
void Graph::drawSkyPlot(const QPoint &p, const QColor &c1, const QColor &c2,
                        int size)
{
    if (!painter) return;
    static const char *lab[] = {"N", "E", "S", "W"};
    int r = size / 2;

    painter->save();
    painter->setBrush(c2);
    painter->setPen(c1);
    painter->drawEllipse(p, r, r);
    painter->setBrush(Qt::NoBrush);
    for (int e = 30; e < 90; e += 30) {
        int d = (int)(r * (90 - e) / 90.0);
        painter->setPen(QPen(c1, 1, Qt::DotLine));
        painter->drawEllipse(p, d, d);
    }
    painter->setPen(QPen(c1, 1, Qt::DotLine));
    for (int a = 0; a < 360; a += 45) {
        int dx = (int)(r * sin(a * D2R)), dy = (int)(r * cos(a * D2R));
        painter->drawLine(p, QPoint(p.x() + dx, p.y() - dy));
    }
    painter->restore();
    for (int a = 0; a < 360; a += 90) {
        int dx = (int)(r * sin(a * D2R)), dy = (int)(r * cos(a * D2R));
        drawText(QPoint(p.x() + dx, p.y() - dy), lab[a/90], color[2], 0, 0, 0);
    }
}
//------------------------------------------------------------------------------
void Graph::drawSkyPlot(double px, double py, const QColor &c1, const QColor &c2,
                        double size)
{
    QPoint p, q;
    toPoint(px, py, p);
    toPoint(px + size/2.0, py, q);
    drawSkyPlot(p, c1, c2, (q.x() - p.x()) * 2);
}
//------------------------------------------------------------------------------
int Graph::clipPoint(const QPoint &p0, int area, QPoint &p1) const
{
    (void)p0; (void)area; (void)p1;
    return 0;                               // Qt clips at the painter level
}
//------------------------------------------------------------------------------
