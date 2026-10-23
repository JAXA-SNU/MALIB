//------------------------------------------------------------------------------
// plotdata.cpp : solution / observation data model for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/plotdata + TIMEPOS/SolToPos of plotcmn.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QFileInfo>
#include <cmath>

#include "plotdata.h"

#define MAXTDIFF    60.0            // max time difference for differentiation
#define SQR(x)      ((x)*(x))
#define TIMETOL     0.005       // time matching tolerance (s)

const char *SOLQ_LABEL[8] = {
    "-", "FIX", "FLOAT", "SBAS", "DGPS", "SINGLE", "PPP", "DR"
};

//------------------------------------------------------------------------------
TimePos::TimePos(int nmax, bool withStd) : std(withStd)
{
    if (nmax < 1) nmax = 1;
    t.resize(nmax); x.resize(nmax); y.resize(nmax); z.resize(nmax);
    q.resize(nmax);
    if (std) { xs.resize(nmax); ys.resize(nmax); zs.resize(nmax); xys.resize(nmax); }
}
//------------------------------------------------------------------------------
// differentiate w.r.t. time (position -> velocity -> acceleration)
TimePos TimePos::tdiff() const
{
    TimePos p(n > 0 ? n : 1, std);

    for (int i = 0; i < n - 1; i++) {
        double tt = timediff(t[i+1], t[i]);
        if (tt == 0.0 || fabs(tt) > MAXTDIFF) continue;

        p.t[p.n] = timeadd(t[i], tt / 2.0);
        p.x[p.n] = (x[i+1] - x[i]) / tt;
        p.y[p.n] = (y[i+1] - y[i]) / tt;
        p.z[p.n] = (z[i+1] - z[i]) / tt;
        if (std) {
            p.xs [p.n] = SQR(xs [i+1]) + SQR(xs [i]);
            p.ys [p.n] = SQR(ys [i+1]) + SQR(ys [i]);
            p.zs [p.n] = SQR(zs [i+1]) + SQR(zs [i]);
            p.xys[p.n] = SQR(xys[i+1]) + SQR(xys[i]);
        }
        p.q[p.n] = qMax(q[i], q[i+1]);
        p.n++;
    }
    return p;
}
//------------------------------------------------------------------------------
// difference between two series, matched by time
TimePos TimePos::diff(const TimePos &o, int qflag) const
{
    TimePos p(qMax(n, o.n), true);

    for (int i = 0, j = 0; i < n && j < o.n; i++, j++) {
        double tt = timediff(t[i], o.t[j]);
        if      (tt < -TIMETOL) { j--; continue; }
        else if (tt >  TIMETOL) { i--; continue; }
        else if (qflag && (q[i] != qflag || o.q[j] != qflag)) continue;

        p.t  [p.n] = t[i];
        p.x  [p.n] = x[i] - o.x[j];
        p.y  [p.n] = y[i] - o.y[j];
        p.z  [p.n] = z[i] - o.z[j];
        if (std && o.std) {
            p.xs [p.n] = SQR(xs [i]) + SQR(o.xs [j]);
            p.ys [p.n] = SQR(ys [i]) + SQR(o.ys [j]);
            p.zs [p.n] = SQR(zs [i]) + SQR(o.zs [j]);
            p.xys[p.n] = SQR(xys[i]) + SQR(o.xys[j]);
        }
        p.q  [p.n] = qMax(q[i], o.q[j]);
        p.n++;
    }
    return p;
}
//------------------------------------------------------------------------------
PlotData::PlotData()
{
    for (int i = 0; i < 2; i++) solData[i] = solbuf_t{};
}
//------------------------------------------------------------------------------
PlotData::~PlotData()
{
    clearSol(0); clearSol(1); clearObs();
}
//------------------------------------------------------------------------------
void PlotData::clearSol(int sel)
{
    freesolbuf(&solData[sel]);
    solData[sel] = solbuf_t{};
    solFiles[sel].clear();
}
//------------------------------------------------------------------------------
void PlotData::clearObs()
{
    freeobs(&obs);
    freenav(&nav, 0xFF);
    obs = obs_t{};
    nav = nav_t{};
    obsFiles.clear();
}
//------------------------------------------------------------------------------
bool PlotData::readSol(const QStringList &files, int sel, QString &msg)
{
    if (files.isEmpty()) return false;

    QVector<QByteArray> keep;
    QVector<char *> paths;
    for (const QString &f : files) {
        keep.append(f.toLocal8Bit());
        paths.append(keep.last().data());
    }
    gtime_t ts = {0}, te = {0};
    solbuf_t sol = {};

    if (!readsolt(paths.data(), paths.size(), ts, te, 0.0, 0, &sol)) {
        msg = QString("no solution data: %1").arg(files.first());
        return false;
    }
    freesolbuf(&solData[sel]);
    solData[sel] = sol;
    solFiles[sel] = files;

    if (sel == 0 || solData[0].n <= 0) {
        if (solData[sel].n > 0) time2gpst(solData[sel].data[0].time, &week);
        updateOrigin();
    }
    msg = QString("%1: %2 solutions").arg(QFileInfo(files.first()).fileName())
                                     .arg(solData[sel].n);
    return true;
}
//------------------------------------------------------------------------------
bool PlotData::readObs(const QStringList &files, QString &msg)
{
    if (files.isEmpty()) return false;

    QVector<QByteArray> keep;
    QVector<char *> paths;
    for (const QString &f : files) {
        keep.append(f.toLocal8Bit());
        paths.append(keep.last().data());
    }
    obs_t o = {};
    nav_t n = {};
    sta_t s = {};
    gtime_t ts = {0}, te = {0};

    for (int i = 0; i < paths.size(); i++) {
        if (readrnxt(paths[i], 1, ts, te, 0.0, "", &o, &n, &s) < 0) {
            msg = QString("rinex read error: %1").arg(files[i]);
            freeobs(&o); freenav(&n, 0xFF);
            return false;
        }
    }
    if (o.n <= 0) {
        msg = QString("no observation data: %1").arg(files.first());
        freeobs(&o); freenav(&n, 0xFF);
        return false;
    }
    sortobs(&o);
    uniqnav(&n);

    clearObs();
    obs = o; nav = n; sta = s;
    obsFiles = files;
    msg = QString("%1: %2 observations").arg(QFileInfo(files.first()).fileName())
                                        .arg(obs.n);
    return true;
}
//------------------------------------------------------------------------------
bool PlotData::readNav(const QStringList &files, QString &msg)
{
    if (files.isEmpty()) return false;
    gtime_t ts = {0}, te = {0};
    int nread = 0;

    for (const QString &f : files) {
        QByteArray p = f.toLocal8Bit();
        if (readrnxt(p.data(), 1, ts, te, 0.0, "", NULL, &nav, NULL) >= 0) nread++;
    }
    if (!nread) { msg = "no navigation data"; return false; }
    uniqnav(&nav);
    navFiles = files;
    msg = QString("%1 ephemeris").arg(nav.n);
    return true;
}
//------------------------------------------------------------------------------
void PlotData::updateOrigin(int mode, const double *ooPos)
{
    const solbuf_t *sol = &solData[0];
    double opos[3] = {0}, pos[3];
    int n = 0;

    switch (mode) {
    case 0:                                     // ORG_STARTPOS
        if (sol->n > 0 && sol->data[0].type == 0)
            for (int i = 0; i < 3; i++) opos[i] = sol->data[0].rr[i];
        break;
    case 1:                                     // ORG_ENDPOS
        if (sol->n > 0 && sol->data[sol->n-1].type == 0)
            for (int i = 0; i < 3; i++) opos[i] = sol->data[sol->n-1].rr[i];
        break;
    case 3:                                     // ORG_FITPOS -> average
    case 2:                                     // ORG_AVEPOS
        for (int i = 0; i < sol->n; i++) {
            if (sol->data[i].type) continue;
            for (int j = 0; j < 3; j++) opos[j] += sol->data[i].rr[j];
            n++;
        }
        if (n > 0) for (int i = 0; i < 3; i++) opos[i] /= n;
        break;
    case 4:                                     // ORG_REFPOS (base station)
        if (norm((double *)sol->rb, 3) > 0.0)
            for (int i = 0; i < 3; i++) opos[i] = sol->rb[i];
        break;
    case 5:                                     // ORG_LLHPOS
        if (ooPos) {
            pos[0] = ooPos[0] * D2R; pos[1] = ooPos[1] * D2R; pos[2] = ooPos[2];
            pos2ecef(pos, opos);
        }
        break;
    case 6:                                     // ORG_XYZPOS
        if (ooPos) for (int i = 0; i < 3; i++) opos[i] = ooPos[i];
        break;
    default: break;
    }
    if (norm(opos, 3) > 0.0) {
        for (int i = 0; i < 3; i++) oPos[i] = opos[i];
        return;
    }
    // fall back to the first solution, then to the observation header position
    if (sol->n > 0 && sol->data[0].type == 0) {
        for (int i = 0; i < 3; i++) oPos[i] = sol->data[0].rr[i];
        return;
    }
    if (norm(sta.pos, 3) > 0.0)
        for (int i = 0; i < 3; i++) oPos[i] = sta.pos[i];
}
//------------------------------------------------------------------------------
void PlotData::setOrigin(const double *rr)
{
    for (int i = 0; i < 3; i++) oPos[i] = rr[i];
}
//------------------------------------------------------------------------------
// solution -> ENU series relative to the origin (port of SolToPos/PosToXyz)
//   type 0:position 1:velocity 2:acceleration
TimePos PlotData::solToPos(int sel, int index, int qflag, int type) const
{
    const solbuf_t *sol = &solData[sel];
    TimePos pos(index < 0 ? qMax(sol->n, 1) : 3, true);
    double opos[3], p[3], r[3], enu[3];

    for (int i = 0; i < 3; i++) opos[i] = oPos[i];
    ecef2pos(opos, p);

    for (int i = (index < 0 ? 0 : index); i < sol->n; i++) {
        const sol_t *d = sol->data + i;
        if (qflag && d->stat != qflag) continue;

        if (d->type == 0) {                 // ecef -> local enu
            for (int j = 0; j < 3; j++) r[j] = d->rr[j] - opos[j];
            ecef2enu(p, r, enu);
        }
        else {                              // already enu
            for (int j = 0; j < 3; j++) enu[j] = d->rr[j];
        }
        if (enu[2] < -RE_WGS84) continue;

        pos.t  [pos.n] = d->time;
        pos.x  [pos.n] = enu[0];
        pos.y  [pos.n] = enu[1];
        pos.z  [pos.n] = enu[2];
        // covariance: qr is stored as {xx,yy,zz,xy,yz,zx}
        pos.xs [pos.n] = d->qr[0] > 0 ? sqrt(d->qr[0]) : 0.0;
        pos.ys [pos.n] = d->qr[1] > 0 ? sqrt(d->qr[1]) : 0.0;
        pos.zs [pos.n] = d->qr[2] > 0 ? sqrt(d->qr[2]) : 0.0;
        pos.xys[pos.n] = d->qr[3];
        pos.q  [pos.n] = d->stat;
        pos.n++;

        if (index >= 0 && pos.n >= 3) break;
    }
    if (type != 1 && type != 2) return pos;         // position

    TimePos vel = pos.tdiff();
    if (type == 1) return vel;                      // velocity
    return vel.tdiff();                             // acceleration
}
//------------------------------------------------------------------------------
// number of satellites / age / ratio (port of SolToNsat)
TimePos PlotData::solToNsat(int sel, int index, int qflag) const
{
    const solbuf_t *sol = &solData[sel];
    TimePos ns(index < 0 ? qMax(sol->n, 1) : 3, true);

    for (int i = (index < 0 ? 0 : index); i < sol->n; i++) {
        const sol_t *d = sol->data + i;
        if (qflag && d->stat != qflag) continue;

        ns.t[ns.n] = d->time;
        ns.x[ns.n] = d->ns;
        ns.y[ns.n] = d->age;
        ns.z[ns.n] = d->ratio;
        ns.q[ns.n] = d->stat;
        ns.n++;

        if (index >= 0 && ns.n >= 3) break;
    }
    return ns;
}
//------------------------------------------------------------------------------
