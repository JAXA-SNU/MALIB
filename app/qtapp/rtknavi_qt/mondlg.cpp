//------------------------------------------------------------------------------
// mondlg.cpp : RTK monitor dialog (Qt port of winapp/rtknavi/mondlg)
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/mondlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "appstyle.h"
#include "mondlg.h"

#define MON_CYCLE   1000                // table update cycle (ms)

// view types ------------------------------------------------------------------
enum { MON_RTK, MON_SAT, MON_STR, MON_OBS, MON_SSR, MON_NAV, MON_CONSOLE };

static const int SYS_TBL[] = {
    SYS_ALL, SYS_GPS, SYS_GLO, SYS_GAL, SYS_QZS, SYS_CMP, SYS_IRN, SYS_SBS
};

//------------------------------------------------------------------------------
MonitorDialog::MonitorDialog(rtksvr_t *svr_, stream_t *moni_, QWidget *parent)
    : QDialog(parent), svr(svr_), moni(moni_)
{
    setWindowTitle("RTK Monitor");

    selType = new QComboBox();
    selType->addItem("RTK",         MON_RTK);
    selType->addItem("Sat Status",  MON_SAT);
    selType->addItem("Streams",     MON_STR);
    selType->addItem("Obs Data",    MON_OBS);
    selType->addItem("SSR Corrections", MON_SSR);
    selType->addItem("Nav Data",    MON_NAV);
    selType->addItem("Error/Warning", MON_CONSOLE);

    selSys = new QComboBox();
    selSys->addItems({"ALL","GPS","GLONASS","Galileo","QZSS","BDS","NavIC","SBAS"});

    selSsr = new QComboBox();          // 1.3.0: main / sub SSR planes
    selSsr->addItem("SSR MAIN", 0);
    selSsr->addItem("SSR SUB",  1);

    selStr = new QComboBox();
    for (int i = 0; i < NSTRIN; i++) selStr->addItem(QString("Input %1").arg(i + 1), i);

    selSat = new QComboBox();
    selSat->addItems({"ALL", "Only OK"});

    label   = new QLabel("");
    btnPause = new QPushButton("Pause");
    btnPause->setCheckable(true);
    btnClear = new QPushButton("Clear");

    auto *top = new QHBoxLayout();
    top->addWidget(selType);
    top->addWidget(selSys);
    top->addWidget(selSat);
    top->addWidget(selStr);
    top->addWidget(selSsr);
    top->addWidget(label, 1);
    top->addWidget(btnPause);
    top->addWidget(btnClear);

    table = new QTableWidget();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(18);
    table->setAlternatingRowColors(true);

    console = new QPlainTextEdit();
    console->setReadOnly(true);
    console->setMaximumBlockCount(2000);
    console->setFont(QFont("monospace"));

    stack = new QStackedWidget();
    stack->addWidget(table);
    stack->addWidget(console);

    auto *btnClose = new QPushButton("&Close");
    auto *bot = new QHBoxLayout();
    bot->addStretch();
    bot->addWidget(btnClose);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(top);
    vbox->addWidget(stack, 1);
    vbox->addLayout(bot);

    connect(selType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MonitorDialog::typeChanged);
    connect(btnClear, &QPushButton::clicked, this, &MonitorDialog::btnClearClicked);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MonitorDialog::tick);
    timer->start(MON_CYCLE);

    typeChanged();
    resize(980, 520);
}
//------------------------------------------------------------------------------
void MonitorDialog::typeChanged()
{
    int t = selType->currentData().toInt();
    selSys->setVisible(t == MON_SAT || t == MON_OBS || t == MON_SSR || t == MON_NAV);
    selSat->setVisible(t == MON_SAT);
    selStr->setVisible(t == MON_OBS);
    selSsr->setVisible(t == MON_SSR);
    btnClear->setVisible(t == MON_CONSOLE);
    stack->setCurrentIndex(t == MON_CONSOLE ? 1 : 0);
    table->clear();
    table->setRowCount(0);
    table->setColumnCount(0);
    label->setText("");
    tick();
}
//------------------------------------------------------------------------------
void MonitorDialog::btnClearClicked()
{
    console->clear();
}
//------------------------------------------------------------------------------
void MonitorDialog::setTable(const QStringList &head, const QList<int> &width)
{
    if (table->columnCount() != head.size()) {
        table->setColumnCount(head.size());
        table->setHorizontalHeaderLabels(head);
        for (int i = 0; i < width.size() && i < head.size(); i++) {
            table->setColumnWidth(i, width[i]);
        }
    }
}
//------------------------------------------------------------------------------
static void put(QTableWidget *t, int r, int c, const QString &s)
{
    QTableWidgetItem *it = t->item(r, c);
    if (!it) { it = new QTableWidgetItem; t->setItem(r, c, it); }
    if (it->text() != s) it->setText(s);
}
//------------------------------------------------------------------------------
void MonitorDialog::tick()
{
    if (btnPause->isChecked()) return;

    switch (selType->currentData().toInt()) {
    case MON_RTK:     showRtk();     break;
    case MON_SAT:     showSat();     break;
    case MON_STR:     showStr();     break;
    case MON_OBS:     showObs();     break;
    case MON_SSR:     showSsr();     break;
    case MON_NAV:     showNav();     break;
    case MON_CONSOLE: showConsole(); break;
    }
}
//------------------------------------------------------------------------------
// server / solution status (port of VCL ShowRtk)
void MonitorDialog::showRtk()
{
    static const char *svrstate[] = {"Stop", "Run"};
    static const char *solstr[]   = {"-","Fix","Float","SBAS","DGPS","Single","PPP","DR"};
    rtk_t rtk;
    int i, cputime, state, nb[MAXSTRRTK] = {0};
    uint32_t nmsg[NSTRIN][12] = {{0}};
    char tstr[64];

    setTable({"Parameter", "Value"}, {260, 460});

    rtksvrlock(svr);
    rtk = svr->rtk;
    state = svr->state;
    cputime = svr->cputime;
    for (i = 0; i < NSTRIN; i++) {
        nb[i] = svr->nb[i];
        for (int j = 0; j < 12; j++) nmsg[i][j] = svr->nmsg[i][j];
    }
    rtksvrunlock(svr);

    QList<QPair<QString, QString>> rows;
    rows << qMakePair(QString("MALIB Version"),
                      QString("%1 %2").arg(VER_MALIB, PATCH_LEVEL_MALIB));
    rows << qMakePair(QString("RTKLIB Version"), QString(VER_RTKLIB));
    rows << qMakePair(QString("Server State"), QString(svrstate[state ? 1 : 0]));
    rows << qMakePair(QString("Processing Cycle (ms)"), QString::number(cputime));
    for (i = 0; i < NSTRIN; i++) {
        rows << qMakePair(QString("Input Stream %1 Bytes").arg(i + 1),
                          QString::number(nb[i]));
    }
    // observation / navigation message counts per input stream
    static const char *msgname[12] = {
        "obs","nav","gnav","ion","sbs","pos","dgps","ssr","err","lex","stat","other"
    };
    for (i = 0; i < NSTRIN; i++) {
        QStringList parts;
        for (int j = 0; j < 12; j++) {
            if (nmsg[i][j]) parts << QString("%1:%2").arg(msgname[j]).arg(nmsg[i][j]);
        }
        rows << qMakePair(QString("Input Stream %1 Messages").arg(i + 1),
                          parts.isEmpty() ? "-" : parts.join("  "));
    }
    time2str(rtk.sol.time, tstr, 3);
    rows << qMakePair(QString("Solution Time (GPST)"),
                      rtk.sol.time.time ? QString(tstr) : "-");
    // MALIB: PPP modes report a fixed solution as PPP-FIX
    QString st = solstr[rtk.sol.stat <= 7 ? rtk.sol.stat : 0];
    if ((rtk.opt.mode == PMODE_PPP_KINEMA || rtk.opt.mode == PMODE_PPP_STATIC ||
         rtk.opt.mode == PMODE_PPP_FIXED) && rtk.sol.stat == SOLQ_FIX) st = "PPP-FIX";
    rows << qMakePair(QString("Solution Status"), st);
    rows << qMakePair(QString("Number of Valid Satellites"), QString::number(rtk.sol.ns));
    rows << qMakePair(QString("Age of Differential (s)"),
                      QString::asprintf("%.1f", rtk.sol.age));
    rows << qMakePair(QString("Ratio for AR Validation"),
                      QString::asprintf("%.1f", rtk.sol.ratio));
    double pos[3] = {0};
    if (norm(rtk.sol.rr, 3) > 0.0) ecef2pos(rtk.sol.rr, pos);
    rows << qMakePair(QString("Position (deg,deg,m)"),
                      QString::asprintf("%.9f  %.9f  %.3f",
                                        pos[0] * R2D, pos[1] * R2D, pos[2]));
    rows << qMakePair(QString("Position XYZ-ECEF (m)"),
                      QString::asprintf("%.3f  %.3f  %.3f",
                                        rtk.sol.rr[0], rtk.sol.rr[1], rtk.sol.rr[2]));
    rows << qMakePair(QString("Velocity XYZ-ECEF (m/s)"),
                      QString::asprintf("%.3f  %.3f  %.3f",
                                        rtk.sol.rr[3], rtk.sol.rr[4], rtk.sol.rr[5]));
    rows << qMakePair(QString("Number of Estimated States"), QString::number(rtk.nx));

    table->setRowCount(rows.size());
    for (i = 0; i < rows.size(); i++) {
        put(table, i, 0, rows[i].first);
        put(table, i, 1, rows[i].second);
    }
}
//------------------------------------------------------------------------------
// per-satellite status (port of VCL ShowSat)
void MonitorDialog::showSat()
{
    rtk_t rtk;
    int i, j, k, n, nfreq, sys = SYS_TBL[selSys->currentIndex()];
    char id[32];

    rtksvrlock(svr);
    rtk = svr->rtk;
    nfreq = svr->rtk.opt.nf;
    rtksvrunlock(svr);
    if (nfreq < 1) nfreq = 1;
    if (nfreq > NFREQ) nfreq = NFREQ;

    QStringList head = {"SAT", "Status", "Az (deg)", "El (deg)"};
    QList<int> width = {46, 52, 62, 62};
    for (k = 0; k < nfreq; k++) { head << QString("L%1").arg(k+1);      width << 38; }
    for (k = 0; k < nfreq; k++) { head << QString("Fix%1").arg(k+1);    width << 50; }
    for (k = 0; k < nfreq; k++) { head << QString("P%1 Res(m)").arg(k+1);  width << 66; }
    for (k = 0; k < nfreq; k++) { head << QString("L%1 Res(m)").arg(k+1);  width << 66; }
    for (k = 0; k < nfreq; k++) { head << QString("Slip%1").arg(k+1);   width << 48; }
    for (k = 0; k < nfreq; k++) { head << QString("Lock%1").arg(k+1);   width << 48; }
    for (k = 0; k < nfreq; k++) { head << QString("Outage%1").arg(k+1); width << 54; }
    for (k = 0; k < nfreq; k++) { head << QString("Reject%1").arg(k+1); width << 54; }
    head << "PHW(cyc)"; width << 60;
    setTable(head, width);

    for (i = 0, n = 0; i < MAXSAT; i++) {
        if (!(satsys(i + 1, NULL) & sys)) continue;
        if (selSat->currentIndex() == 1 && !rtk.ssat[i].vs) continue;
        n++;
    }
    table->setRowCount(n);

    for (i = 0, n = 0; i < MAXSAT; i++) {
        if (!(satsys(i + 1, NULL) & sys)) continue;
        const ssat_t *ss = rtk.ssat + i;
        if (selSat->currentIndex() == 1 && !ss->vs) continue;
        j = 0;
        satno2id(i + 1, id);
        put(table, n, j++, id);
        put(table, n, j++, ss->vs ? "OK" : "-");
        double az = ss->azel[0] * R2D; if (az < 0.0) az += 360.0;
        put(table, n, j++, QString::asprintf("%.1f", az));
        put(table, n, j++, QString::asprintf("%.1f", ss->azel[1] * R2D));
        for (k = 0; k < nfreq; k++) put(table, n, j++, ss->vsat[k] ? "OK" : "-");
        for (k = 0; k < nfreq; k++) {
            int fx = ss->fix[k];
            put(table, n, j++, fx == 1 ? "FLOAT" : fx == 2 ? "FIX" : fx == 3 ? "HOLD" : "-");
        }
        for (k = 0; k < nfreq; k++) put(table, n, j++, QString::asprintf("%.2f", ss->resp[k]));
        for (k = 0; k < nfreq; k++) put(table, n, j++, QString::asprintf("%.4f", ss->resc[k]));
        for (k = 0; k < nfreq; k++) put(table, n, j++, QString::number(ss->slipc[k]));
        for (k = 0; k < nfreq; k++) put(table, n, j++, QString::number(ss->lock[k]));
        for (k = 0; k < nfreq; k++) put(table, n, j++, QString::number(ss->outc[k]));
        for (k = 0; k < nfreq; k++) put(table, n, j++, QString::number(ss->rejc[k]));
        put(table, n, j++, QString::asprintf("%.2f", ss->phw));
        n++;
    }
    label->setText(QString("%1 satellites").arg(n));
}
//------------------------------------------------------------------------------
// stream status (port of VCL ShowStr, extended to the 1.3.0 stream set)
void MonitorDialog::showStr()
{
    static const char *ch[MAXSTRRTK + 1] = {
        "Input Rover", "Input Corr 1", "Input Corr 2", "Input Corr 3",
        "Output Solution 1", "Output Solution 2",
        "Log Rover", "Log Corr 1", "Log Corr 2", "Log Corr 3", "Monitor"
    };
    static const char *type[] = {
        "-","Serial","File","TCP Server","TCP Client","NTRIP Server",
        "NTRIP Client","FTP","HTTP","NTRIP Caster","UDP Server","UDP Client",""
    };
    static const char *outformat[] = {"Lat/Lon/Height","X/Y/Z-ECEF","E/N/U-Baseline","NMEA-0183","STAT"};
    static const char *state[] = {"Error","-","OK"};

    stream_t stream[MAXSTRRTK + 1];
    int i, format[MAXSTRRTK + 1] = {0};

    setTable({"STR","Stream","Type","Format","Mode","State","Input(bytes)",
              "Input(bps)","Output(bytes)","Output(bps)","Path","Message"},
             {40,120,90,110,44,44,86,80,90,84,240,240});

    rtksvrlock(svr);
    for (i = 0; i < MAXSTRRTK; i++) stream[i] = svr->stream[i];
    for (i = 0; i < NSTRIN; i++) format[i] = svr->format[i];
    for (i = 0; i < NSTROUT; i++) format[IDXSTROUT + i] = svr->solopt[i].posf;
    stream[MAXSTRRTK] = *moni;
    format[MAXSTRRTK] = SOLF_LLH;
    rtksvrunlock(svr);

    table->setRowCount(MAXSTRRTK + 1);
    for (i = 0; i <= MAXSTRRTK; i++) {
        int j = 0, t = stream[i].type;
        put(table, i, j++, QString("(%1)").arg(i + 1));
        put(table, i, j++, ch[i]);
        put(table, i, j++, (t >= 0 && t <= 11) ? type[t] : "-");
        QString form = "-";
        if (t) {
            if (i < NSTRIN) form = formatstrs[format[i]] ? formatstrs[format[i]] : "-";
            else if (i < IDXSTRLOG || i == MAXSTRRTK) {
                int f = format[i];
                form = (f >= 0 && f < 5) ? outformat[f] : "-";
            }
        }
        put(table, i, j++, form);
        QString mode;
        if (stream[i].mode & STR_MODE_R) mode = "R";
        if (stream[i].mode & STR_MODE_W) mode += (mode.isEmpty() ? "" : "/") + QString("W");
        put(table, i, j++, mode);
        put(table, i, j++, state[stream[i].state + 1 >= 0 && stream[i].state + 1 <= 2
                                 ? stream[i].state + 1 : 1]);
        put(table, i, j++, QString::number(stream[i].inb));
        put(table, i, j++, QString::number(stream[i].inr));
        put(table, i, j++, QString::number(stream[i].outb));
        put(table, i, j++, QString::number(stream[i].outr));
        // mask the password in user:passwd@host
        QString path = QString::fromLatin1(stream[i].path);
        int at = path.indexOf('@');
        if (at > 0) {
            int colon = path.lastIndexOf(':', at);
            if (colon >= 0) path.replace(colon + 1, at - colon - 1,
                                         QString(at - colon - 1, '*'));
        }
        put(table, i, j++, path);
        put(table, i, j++, QString::fromLatin1(stream[i].msg));
    }
}
//------------------------------------------------------------------------------
// observation data of the selected input stream
void MonitorDialog::showObs()
{
    obs_t obs = {0};
    obsd_t data[MAXOBS];
    int i, n = 0, idx = selStr->currentData().toInt();
    int sys = SYS_TBL[selSys->currentIndex()];
    char id[32], tstr[64];

    QStringList head = {"Time", "SAT"};
    QList<int> width = {170, 46};
    for (i = 0; i < NFREQ; i++) {
        head << QString("P%1 (m)").arg(i+1) << QString("L%1 (cyc)").arg(i+1)
             << QString("D%1 (Hz)").arg(i+1) << QString("S%1").arg(i+1)
             << QString("C%1").arg(i+1);
        width << 110 << 120 << 76 << 44 << 44;
    }
    setTable(head, width);

    obs.data = data;
    rtksvrlock(svr);
    for (i = 0; i < svr->obs[idx][0].n && n < MAXOBS; i++) {
        if (!(satsys(svr->obs[idx][0].data[i].sat, NULL) & sys)) continue;
        data[n++] = svr->obs[idx][0].data[i];
    }
    rtksvrunlock(svr);

    table->setRowCount(n);
    for (i = 0; i < n; i++) {
        int j = 0;
        time2str(data[i].time, tstr, 3);
        satno2id(data[i].sat, id);
        put(table, i, j++, tstr);
        put(table, i, j++, id);
        for (int k = 0; k < NFREQ; k++) {
            put(table, i, j++, QString::asprintf("%.3f", data[i].P[k]));
            put(table, i, j++, QString::asprintf("%.3f", data[i].L[k]));
            put(table, i, j++, QString::asprintf("%.3f", data[i].D[k]));
            put(table, i, j++, QString::asprintf("%.1f", data[i].SNR[k] * SNR_UNIT));
            put(table, i, j++, QString::fromLatin1(code2obs(data[i].code[k])));
        }
    }
    label->setText(QString("%1 observations").arg(n));
}
//------------------------------------------------------------------------------
// SSR (MADOCA-PPP) corrections (port of VCL ShowRtcmSsr, 1.3.0 main/sub planes)
void MonitorDialog::showSsr()
{
    gtime_t time;
    static ssr_t ssr[MAXSAT];
    int sat[MAXSAT];
    int i, j, k, n, plane = selSsr->currentData().toInt();
    int sys = SYS_TBL[selSys->currentIndex()];
    char id[32], tstr[64];

    setTable({"SAT","Status","UDI(s)","IOD","URA","Datum","T0",
              "D0-A(m)","D0-C(m)","D0-R(m)","D1-A(mm/s)","D1-C(mm/s)","D1-R(mm/s)",
              "C0(m)","C1(mm/s)","C2(mm/s2)","Code Bias(m)","Phase Bias(m)"},
             {46,54,54,44,40,48,150,74,74,74,84,84,84,74,80,86,240,240});

    rtksvrlock(svr);
    time = svr->rtk.sol.time;
    for (i = n = 0; i < MAXSAT; i++) {
        if (!(satsys(i + 1, NULL) & sys)) continue;
        ssr[n] = svr->nav.ssr[plane][i];
        sat[n++] = i + 1;
    }
    rtksvrunlock(svr);

    table->setRowCount(n);
    int nvalid = 0;
    for (i = 0; i < n; i++) {
        j = 0;
        satno2id(sat[i], id);
        put(table, i, j++, id);
        bool valid = ssr[i].t0[0].time && fabs(timediff(time, ssr[i].t0[0])) <= 1800.0;
        if (valid) nvalid++;
        put(table, i, j++, valid ? "OK" : "-");
        put(table, i, j++, QString::asprintf("%.0f", ssr[i].udi[0]));
        put(table, i, j++, QString::number(ssr[i].iode));
        put(table, i, j++, QString::number(ssr[i].ura));
        put(table, i, j++, QString::number(ssr[i].refd));
        if (ssr[i].t0[0].time) time2str(ssr[i].t0[0], tstr, 0); else strcpy(tstr, "-");
        put(table, i, j++, tstr);
        for (k = 0; k < 3; k++) put(table, i, j++, QString::asprintf("%.3f", ssr[i].deph[k]));
        for (k = 0; k < 3; k++) put(table, i, j++, QString::asprintf("%.3f", ssr[i].ddeph[k] * 1E3));
        put(table, i, j++, QString::asprintf("%.3f", ssr[i].dclk[0]));
        put(table, i, j++, QString::asprintf("%.3f", ssr[i].dclk[1] * 1E3));
        put(table, i, j++, QString::asprintf("%.5f", ssr[i].dclk[2] * 1E3));
        QString cb, pb;
        for (k = 0; k < MAXCODE; k++) {
            if (ssr[i].cbias[k] != 0.0) {
                cb += QString::asprintf("%s:%.3f ", code2obs(k + 1), ssr[i].cbias[k]);
            }
            if (ssr[i].pbias[k] != 0.0) {
                pb += QString::asprintf("%s:%.3f ", code2obs(k + 1), ssr[i].pbias[k]);
            }
        }
        put(table, i, j++, cb);
        put(table, i, j++, pb);
    }
    label->setText(QString("%1 / %2 satellites valid  (%3)")
                       .arg(nvalid).arg(n).arg(plane ? "SUB" : "MAIN"));
}
//------------------------------------------------------------------------------
// broadcast ephemeris summary
void MonitorDialog::showNav()
{
    // nav.eph is a heap array of nav.n entries (NOT indexed by satellite),
    // so it must be walked by count, never by MAXSAT.
    static eph_t eph[MAXSAT * 4];
    int i, n = 0, sys = SYS_TBL[selSys->currentIndex()];
    char id[32], tstr[64];

    setTable({"SAT","IODE","IODC","SVA","SVH","Toe","Toc","Ttr","A(m)","e","i0(deg)"},
             {46,54,54,44,44,150,150,150,120,90,80});

    rtksvrlock(svr);
    if (svr->nav.eph) {
        for (i = 0; i < svr->nav.n && n < (int)(sizeof(eph) / sizeof(eph[0])); i++) {
            if (!svr->nav.eph[i].sat) continue;
            if (!(satsys(svr->nav.eph[i].sat, NULL) & sys)) continue;
            eph[n++] = svr->nav.eph[i];
        }
    }
    rtksvrunlock(svr);

    table->setRowCount(n);
    int nvalid = n;
    for (i = 0; i < n; i++) {
        int j = 0;
        satno2id(eph[i].sat, id);
        put(table, i, j++, id);
        put(table, i, j++, QString::number(eph[i].iode));
        put(table, i, j++, QString::number(eph[i].iodc));
        put(table, i, j++, QString::number(eph[i].sva));
        put(table, i, j++, QString::number(eph[i].svh));
        for (gtime_t t : {eph[i].toe, eph[i].toc, eph[i].ttr}) {
            if (t.time) time2str(t, tstr, 0); else strcpy(tstr, "-");
            put(table, i, j++, tstr);
        }
        put(table, i, j++, QString::asprintf("%.3f", eph[i].A));
        put(table, i, j++, QString::asprintf("%.8f", eph[i].e));
        put(table, i, j++, QString::asprintf("%.4f", eph[i].i0 * R2D));
    }
    label->setText(QString("%1 ephemeris").arg(nvalid));
}
//------------------------------------------------------------------------------
// error / warning messages from the server
void MonitorDialog::showConsole()
{
    char buff[MAXERRMSG];
    int n;

    rtksvrlock(svr);
    n = svr->rtk.neb;
    if (n > 0) {
        if (n >= (int)sizeof(buff)) n = sizeof(buff) - 1;
        memcpy(buff, svr->rtk.errbuf, n);
        buff[n] = '\0';
        svr->rtk.neb = 0;
    }
    rtksvrunlock(svr);

    if (n > 0) {
        QString s = QString::fromLatin1(buff);
        for (const QString &line : s.split('\n', Qt::SkipEmptyParts)) {
            console->appendPlainText(line);
        }
    }
}
//------------------------------------------------------------------------------
