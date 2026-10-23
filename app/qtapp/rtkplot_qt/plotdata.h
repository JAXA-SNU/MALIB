//------------------------------------------------------------------------------
// plotdata.h : solution / observation data model for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotdata + the TIMEPOS class of plotmain.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef PLOTDATA_H
#define PLOTDATA_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "rtklib.h"

// plot types (same numbering as the VCL original) -----------------------------
enum {
    PLOT_TRK, PLOT_SOLP, PLOT_SOLV, PLOT_SOLA, PLOT_NSAT,
    PLOT_RES, PLOT_RESE, PLOT_OBS, PLOT_SKY, PLOT_DOP,
    PLOT_SNR, PLOT_SNRE, PLOT_MPS
};

//------------------------------------------------------------------------------
// time-position series (port of the VCL TIMEPOS class)
//   x/y/z hold the value triplet, xs/ys/zs the standard deviations,
//   xys the xy covariance and q the solution quality.
//------------------------------------------------------------------------------
class TimePos
{
public:
    explicit TimePos(int nmax, bool withStd);

    int n = 0;
    QVector<gtime_t> t;
    QVector<double>  x, y, z, xs, ys, zs, xys;
    QVector<int>     q;

    TimePos tdiff() const;                  // differentiate w.r.t. time
    TimePos diff(const TimePos &other, int qflag) const;

private:
    bool std = false;
};

//------------------------------------------------------------------------------
// data store shared by the plot widget and the main window
//------------------------------------------------------------------------------
class PlotData
{
public:
    PlotData();
    ~PlotData();

    // loading ------------------------------------------------------------
    bool readSol(const QStringList &files, int sel, QString &msg);
    bool readObs(const QStringList &files, QString &msg);
    bool readNav(const QStringList &files, QString &msg);
    void clearSol(int sel);
    void clearObs();

    // conversion ---------------------------------------------------------
    TimePos solToPos(int sel, int index, int qflag, int type) const;
    TimePos solToNsat(int sel, int index, int qflag) const;

    // origin used to convert ECEF to local ENU.  mode is one of the ORG_*
    // values of plotopt.h; ooPos is lat/lon/hgt (deg,deg,m) for ORG_LLHPOS
    // and X/Y/Z-ECEF (m) for ORG_XYZPOS.
    void   updateOrigin(int mode = 2, const double *ooPos = nullptr);
    void   setOrigin(const double *rr);
    const double *origin() const { return oPos; }

    solbuf_t solData[2] = {};
    obs_t    obs = {};
    nav_t    nav = {};
    sta_t    sta = {};
    int      week = 0;
    QStringList solFiles[2], obsFiles, navFiles;

private:
    double oPos[3] = {0};                   // origin position (ecef)
};

// solution quality colors (index by sol_t::stat) ------------------------------
extern const char *SOLQ_LABEL[8];

#endif // PLOTDATA_H
