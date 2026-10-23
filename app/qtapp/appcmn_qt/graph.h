//------------------------------------------------------------------------------
// graph.h : 2-D graph plotting engine
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
#ifndef GRAPH_H
#define GRAPH_H

#include <QColor>
#include <QPoint>
#include <QString>

class QPainter;

class Graph
{
public:
    explicit Graph(QPainter *painter = nullptr);

    void setPainter(QPainter *p) { painter = p; }

    // display options (same meaning as the VCL original)
    int box = 1;                    // draw the bounding box
    int fit = 1;                    // rescale on resize
    int xGrid = 1, yGrid = 1;
    // grid label position
    //   0:off 1:outer 2:inner 3:outer-rot 4:inner-rot 5/6:time 7:on-axis
    int xLPos = 1, yLPos = 1;
    int week = 0;                   // gps week for time labels
    int timeFormat = 1;             // 0:ww ssss 1:GPST 2:UTC 3:JST
    QString title, xLabel, yLabel;
    QColor color[3] = {Qt::white, QColor(128,128,128), Qt::black}; // bg/grid/text

    // geometry -----------------------------------------------------------
    void setSize(int w, int h);
    void setPos(const QPoint &p1, const QPoint &p2);
    void getPos(QPoint &p1, QPoint &p2) const;
    void setCent(double x, double y);
    void getCent(double &x, double &y) const;
    void setRight(double x, double y);
    void getRight(double &x, double &y) const;
    double xScaleValue() const { return xScale; }
    double yScaleValue() const { return yScale; }
    void setScale(double xs, double ys);
    void getScale(double &xs, double &ys) const;
    void setLim(const double *xl, const double *yl);
    void getLim(double *xl, double *yl) const;
    void setTick(double xt, double yt);
    void getTick(double &xt, double &yt) const;

    bool isInArea(const QPoint &p) const;
    bool toPoint(double x, double y, QPoint &p) const;
    void toPos(const QPoint &p, double &x, double &y) const;
    bool onAxis(const QPoint &p) const;

    QString numText(double x, double dx) const;
    QString timeText(double x, double dx) const;

    // drawing ------------------------------------------------------------
    void drawAxis(int label, int glabel);
    void drawMark(const QPoint &p, int mark, const QColor &c, int size, int rot);
    void drawMark(double x, double y, int mark, const QColor &c, int size, int rot);
    void drawMark(const QPoint &p, int mark, const QColor &c, const QColor &bg,
                  int size, int rot);
    void drawMarks(const double *x, const double *y, const QColor *c, int n,
                   int mark, int size, int rot);
    void drawCircle(const QPoint &p, const QColor &c, int rx, int ry, int style);
    void drawCircles(int label);
    void drawText(const QPoint &p, const QString &s, const QColor &c,
                  int ha, int va, int rot);
    void drawText(double x, double y, const QString &s, const QColor &c,
                  int ha, int va, int rot);
    void drawText(const QPoint &p, const QString &s, const QColor &c,
                  const QColor &bg, int ha, int va, int rot);
    void drawPoly(const QPoint *p, int n, const QColor &c, int style);
    void drawPoly(const double *x, const double *y, int n, const QColor &c, int style);
    void drawPatch(const double *x, const double *y, int n,
                   const QColor &c1, const QColor &c2, int style);
    void drawSkyPlot(const QPoint &p, const QColor &c1, const QColor &c2, int size);
    void drawSkyPlot(double x, double y, const QColor &c1, const QColor &c2,
                     double size);

private:
    double autoTick(double scale) const;
    double autoTickTime(double scale) const;
    void drawBox();
    void drawLabel();
    void drawGrid(double xt, double yt);
    void drawGridLabel(double xt, double yt);
    int  clipPoint(const QPoint &p0, int area, QPoint &p1) const;

    QPainter *painter;
    int x = 0, y = 0, width = 100, height = 100;
    double xCent = 0.0, yCent = 0.0;
    double xScale = 0.02, yScale = 0.02;
    double xTick = 0.0, yTick = 0.0;
};

#endif // GRAPH_H
