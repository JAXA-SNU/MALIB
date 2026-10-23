//------------------------------------------------------------------------------
// plotwidget.h : plot canvas for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotdraw (track / position / velocity /
// acceleration / nsat plots).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QFrame>

#include "graph.h"
#include "plotdata.h"
#include "plotopt.h"

class PlotWidget : public QFrame
{
    Q_OBJECT
public:
    explicit PlotWidget(PlotData *data, QWidget *parent = nullptr);

    void setPlotType(int type);
    int  plotType() const { return type; }
    void setQFlag(int q);
    void setShowSol(int sel, bool on);
    void setTrackCircles(bool on) { trkCircles = on; update(); }
    void setOpt(const PlotOpt *o);          // options dialog result
    void setShowInfo(bool on)     { showInfo = on; update(); }
    bool showsInfo() const        { return showInfo; }

    // scale/centre handling.  fit is only done on demand (data load, plot type
    // change, Fit button) so that pan/zoom survives a repaint.
    void fitRange();                        // fit both axes to the data
    void fitHoriz();                        // fit the time/x axis only
    void fitVert();                         // fit the y axis only
    void centerOrigin();
    void dataChanged();                     // buffers changed (load / realtime)

    // when true, newly arriving real-time data keeps the view fitted
    void setAutoFit(bool on);
    bool autoFits() const { return autoFit; }

signals:
    void statusChanged(const QString &text);
    void autoFitOff();                      // user panned/zoomed manually

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void layoutGraphs();                    // setPos() for the current type
    void drawTrack();
    void drawSolution(int mode);            // 0:pos 1:vel 2:acc
    void drawNsat();
    void drawInfoTrack(const TimePos &pos);
    void drawInfoSol(int panel, const TimePos &pos, int axis, const char *unit);
    QColor markColor(int sel, int q) const;
    void drawTrackExtras();                 // scale bar / compass
    void applyColors(Graph &g);
    bool isTimeSeries() const { return type != PLOT_TRK; }
    int  panelAt(const QPoint &p) const;    // 0..2, -1 outside
    void panX(int dpix);                    // shift all time panels in x
    void zoomX(double f);
    void manualChange();                    // user interaction turns auto-fit off
    TimePos seriesOf(int sel, int mode) const;

    PlotData *data;
    const PlotOpt *opt = nullptr;
    Graph     graphT;                       // ground track
    Graph     graphG[3];                    // time-series panels
    int  type = PLOT_TRK;
    int  qFlag = 0;                          // 0:all, else SOLQ_*
    bool showSol[2] = {true, false};
    bool trkCircles = false;
    bool showInfo = true;
    bool autoFit = true;
    bool laidOut = false;
    bool needFit = true;

    QPoint dragPos;
    int    dragPanel = -1;
    bool   dragging = false;
    bool   dragZoom = false;   // right button: time-axis stretch
};

#endif // PLOTWIDGET_H
