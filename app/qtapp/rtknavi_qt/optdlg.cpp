//------------------------------------------------------------------------------
// optdlg.cpp : options dialog
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTabWidget>
#include <QVBoxLayout>

#include "appstyle.h"

#include "basefix.h"
#include "optdlg.h"
#include "textconv.h"
#include "refdlg.h"
#include "snrwdlg.h"

// generated tabs: prefix -> tab title (Setting1-3/Output/Statistics are
// hand-built; anything not placed there falls through to these)
static const struct { const char *prefix; const char *tab; } GEN_TABS[] = {
    {"", "Othe&r"},                     // fallback for anything not placed
};

static const QStringList GNSS_LABELS = {
    "GPS", "SBAS", "GLONASS", "Galileo", "QZSS", "BDS", "NavIC"
};
static const QList<int> GNSS_BITS = {1, 2, 4, 8, 16, 32, 64};

//------------------------------------------------------------------------------
opt_t *OptDialog::find(const char *name)
{
    return searchopt(name, sysopts);
}
//------------------------------------------------------------------------------
// build the editor widget for one option and register it for load/apply
QWidget *OptDialog::editorFor(const char *name, int width)
{
    opt_t *opt = find(name);
    if (!opt) return new QLabel(QString("(%1: not in this version)").arg(name));

    placed.insert(name);
    Row row;
    row.opt = opt;
    QWidget *w;
    QString comment = QString::fromLatin1(opt->comment ? opt->comment : "");

    if (opt->format == 3 && comment.contains(':')) {
        // keep the numeric value with each entry: str2enum() mis-parses
        // negative values and prefix-colliding labels (see BASELIB_ISSUES.md)
        row.combo = new QComboBox();
        for (const QString &item : comment.split(',')) {
            QString num = item.section(':', 0, 0).trimmed();
            QString label = item.section(':', 1).trimmed();
            bool ok = false;
            int val = num.toInt(&ok);
            if (!label.isEmpty() && ok) row.combo->addItem(label, val);
        }
        w = row.combo;
    }
    else {
        row.edit = new QLineEdit();
        w = row.edit;
    }
    if (width) w->setMaximumWidth(width);
    w->setToolTip(QString(name) + (comment.isEmpty() ? "" : "  (" + comment + ")"));
    rows.append(row);
    return w;
}
//------------------------------------------------------------------------------
// on/off enum rendered as a checkbox (posopt1-6 etc.)
QCheckBox *OptDialog::checkFor(const char *name, const QString &label)
{
    opt_t *opt = find(name);
    auto *cb = new QCheckBox(label);
    if (!opt) { cb->setEnabled(false); return cb; }

    placed.insert(name);
    Row row;
    row.opt = opt;
    row.check = cb;
    QString comment = QString::fromLatin1(opt->comment ? opt->comment : "");
    cb->setToolTip(QString(name) + (comment.isEmpty() ? "" : "  (" + comment + ")"));
    rows.append(row);
    return cb;
}
//------------------------------------------------------------------------------
void OptDialog::addRow(QFormLayout *form, const QString &label, const char *name)
{
    form->addRow(label, editorFor(name));
}
//------------------------------------------------------------------------------
void OptDialog::addRow2(QFormLayout *form, const QString &label,
                        const char *a, const char *b)
{
    auto *wrap = new QWidget();
    auto *h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(editorFor(a), 1);
    h->addWidget(editorFor(b), 1);
    form->addRow(label, wrap);
}
//------------------------------------------------------------------------------
// bitmask option (navsys / arsys) as a row of checkboxes
QWidget *OptDialog::bitmaskRow(const char *name, const QStringList &labels,
                               const QList<int> &bits)
{
    opt_t *opt = find(name);
    auto *wrap = new QWidget();
    auto *h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    if (!opt) { h->addWidget(new QLabel(QString("(%1: n/a)").arg(name))); return wrap; }

    placed.insert(name);
    for (int i = 0; i < labels.size(); i++) {
        Row row;
        row.opt = opt;
        row.check = new QCheckBox(labels[i]);
        row.bit = bits[i];
        row.check->setToolTip(QString("%1 bit %2").arg(name).arg(bits[i]));
        h->addWidget(row.check);
        rows.append(row);
    }
    h->addStretch();
    return wrap;
}
//------------------------------------------------------------------------------
// Setting1: positioning mode, corrections, satellite systems (RTKNAVI layout)
QWidget *OptDialog::buildSetting1()
{
    auto *page = new QWidget();
    auto *form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);

    addRow (form, "Positioning Mode", "pos1-posmode");
    addRow2(form, "Frequencies / Filter Type", "pos1-frequency", "pos1-soltype");

    // elevation mask + SNR mask sub-dialog
    {
        auto *wrap = new QWidget();
        auto *h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(editorFor("pos1-elmask"), 1);
        auto *btn = new QPushButton("SNR Mask...");
        connect(btn, &QPushButton::clicked, this, &OptDialog::snrMaskDialog);
        h->addWidget(btn, 1);
        form->addRow(QString::fromUtf8("Elevation Mask (°) / SNR Mask"), wrap);
    }
    addRow2(form, "Rec Dynamics / Earth Tides Correction", "pos1-dynamics", "pos1-tidecorr");
    // 1.3.0: dead-reckoning sub option / interval (goes with Rec Dynamics)
    addRow2(form, "DR Sub Option / Interval (s)", "pos1-subopt", "pos1-drint");
    addRow (form, "Ionosphere Correction", "pos1-ionoopt");
    addRow (form, "Troposphere Correction", "pos1-tropopt");
    addRow (form, "Satellite Ephemeris/Clock", "pos1-sateph");

    // posopt1-6 as a checkbox row (posopt3 is a 3-state enum -> combo)
    {
        auto *wrap = new QWidget();
        auto *h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(checkFor("pos1-posopt1", "Sat PCV"));
        h->addWidget(checkFor("pos1-posopt2", "Rec PCV"));
        h->addWidget(new QLabel("PhWU:"));
        h->addWidget(editorFor("pos1-posopt3", 110));
        h->addWidget(checkFor("pos1-posopt4", "Rej Ecl"));
        h->addWidget(checkFor("pos1-posopt5", "RAIM FDE"));
        h->addWidget(checkFor("pos1-posopt6", "DBCorr"));
        h->addStretch();
        form->addRow("", wrap);
    }
    addRow(form, "Excluded Satellites (+PRN: Included)", "pos1-exclsats");
    form->addRow("Navigation Systems",
                 bitmaskRow("pos1-navsys", GNSS_LABELS, GNSS_BITS));

    // 1.3.0: per-constellation SNR weighting (edited in its own dialog, so the
    // 15 options are marked placed to keep them off the fallback tab)
    {
        auto *btn = new QPushButton("SNR Weighting...");
        btn->setToolTip("pos1-snrwthres/snrwmargin/snrweratio per constellation");
        connect(btn, &QPushButton::clicked, this, &OptDialog::snrWeightDialog);
        form->addRow("SNR Weighting (threshold table)", btn);
        for (const char *sys : {"gps", "glo", "gal", "qzs", "bds"}) {
            placed.insert(QString("pos1-snrwthres_%1").arg(sys));
            placed.insert(QString("pos1-snrwmargin_%1").arg(sys));
            placed.insert(QString("pos1-snrweratio_%1").arg(sys));
        }
    }
    auto *scroll = new QScrollArea();
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    return scroll;
}
//------------------------------------------------------------------------------
// group box helper for the Setting2/3 grids
QFormLayout *OptDialog::addGroup(QGridLayout *outer, const char *title,
                                 int r, int c)
{
    auto *g = new QGroupBox(title);
    auto *f = new QFormLayout(g);
    f->setLabelAlignment(Qt::AlignRight);
    outer->addWidget(g, r, c);
    return f;
}
//------------------------------------------------------------------------------
// Setting2: ambiguity resolution + solution validation (classic RTKLIB content)
QWidget *OptDialog::buildSetting2()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *ar = addGroup(outer, "Integer Ambiguity Resolution", 0, 0);
    addRow2(ar, "AR Mode (GPS / GLONASS)", "pos2-armode", "pos2-gloarmode");
    addRow (ar, "AR Mode (BDS)", "pos2-bdsarmode");
    ar->addRow("AR Systems", bitmaskRow("pos2-arsys", GNSS_LABELS, GNSS_BITS));
    addRow (ar, "Min Ratio to Fix Ambiguity", "pos2-arthres");
    addRow2(ar, "Min Confidence / Max FCB", "pos2-arthres1", "pos2-arthres2");
    addRow2(ar, "Max Pos Var / Slip Thres for AR", "pos2-arthres3", "pos2-arthres4");
    addRow2(ar, QString::fromUtf8("Min Lock / Elevation (°) to Fix"),
            "pos2-arlockcnt", "pos2-arelmask");
    addRow2(ar, QString::fromUtf8("Min Fix / Elevation (°) to Hold"),
            "pos2-arminfix", "pos2-elmaskhold");
    addRow2(ar, "Max Iteration / Outage to Reset", "pos2-armaxiter", "pos2-aroutcnt");

    QFormLayout *va = addGroup(outer, "Solution Validation", 0, 1);
    addRow2(va, "Max Age of Diff (s) / Slip Thres (m)", "pos2-maxage", "pos2-slipthres");
    addRow2(va, "Reject Innov (m) / GDOP", "pos2-rejionno", "pos2-rejgdop");
    addRow (va, "Number of Filter Iteration", "pos2-niter");
    addRow (va, "Ignore Chi-Squared Error", "pos2-ign_chierr");

    QFormLayout *bl = addGroup(outer, "Baseline Constraint", 1, 1);
    addRow2(bl, "Length / Sigma (m)", "pos2-baselen", "pos2-basesig");

    QFormLayout *ck = addGroup(outer, "Carrier Smoothing / Clock", 1, 0);
    addRow (ck, "Carrier Smoothing", "pos2-car");
    addRow2(ck, "Smoothing Thres 1 / 2", "pos2-carthres1", "pos2-carthres2");
    addRow2(ck, "Clock Option / Drift Option", "pos2-clkopt", "pos2-clkdopt");
    addRow (ck, "Time Interval (s)", "pos2-tint");

    outer->setRowStretch(2, 1);
    return page;
}
//------------------------------------------------------------------------------
// Setting3: MALIB 1.3.0 additions (MADOCA-PPP, signal selection, INS)
QWidget *OptDialog::buildSetting3()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *pp = addGroup(outer, "MADOCA-PPP", 0, 0);
    addRow (pp, "Ionosphere Correction (MADOCA)", "pos2-ionocorr");
    addRow2(pp, "Sat Code / Phase Bias", "pos2-pppsatcb", "pos2-pppsatpb");
    addRow2(pp, "Uncorrected Bias / Max dt (s)", "pos2-uncorrbias", "pos2-maxbiasdt");
    addRow (pp, "BDS-2 Bias Correction", "pos2-bds2bias");

    QFormLayout *sig = addGroup(outer, "Signal Selection", 0, 1);
    addRow(sig, "GPS Block IIR-M", "pos2-siggpsIIR-M");
    addRow(sig, "GPS Block IIF",   "pos2-siggpsIIF");
    addRow(sig, "GPS Block IIIA",  "pos2-siggpsIIIA");
    addRow(sig, "Galileo", "pos2-siggal");
    addRow(sig, "QZSS Block I/II", "pos2-sigqzs1_2");
    addRow(sig, "QZSS Block IIA",  "pos2-sigqzs2A");
    addRow(sig, "QZSS Block III",  "pos2-sigqzs3");
    addRow(sig, "BeiDou-2", "pos2-sigbds2");
    addRow(sig, "BeiDou-3", "pos2-sigbds3");

    QFormLayout *ins = addGroup(outer, "INS / Dead Reckoning", 1, 0);
    addRow2(ins, "INS Interval (s) / Reset", "pos2-insint", "pos2-insreset");
    addRow2(ins, "Mask Object / Max Distance (m)", "pos2-maskobj", "pos2-maxdist");

    outer->setRowStretch(2, 1);
    return page;
}
//------------------------------------------------------------------------------
// Output: solution format, time/coordinate representation, NMEA, status
QWidget *OptDialog::buildOutput()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *sf = addGroup(outer, "Solution Format", 0, 0);
    addRow (sf, "Solution Format", "out-solformat");
    addRow2(sf, "Output Header / Processing Options", "out-outhead", "out-outopt");
    addRow2(sf, "Output Velocity / Single Solution", "out-outvel", "out-outsingle");
    addRow (sf, "Static Mode Output", "out-solstatic");
    addRow (sf, "Max Solution Std to Output (m)", "out-maxsolstd");

    QFormLayout *tc = addGroup(outer, "Time / Coordinate", 0, 1);
    addRow2(tc, "Time System / Format", "out-timesys", "out-timeform");
    addRow (tc, "Time Decimals", "out-timendec");
    addRow (tc, "Latitude/Longitude Format", "out-degform");
    addRow (tc, "Field Separator", "out-fieldsep");
    addRow2(tc, "Height / Geoid Model", "out-height", "out-geoid");

    QFormLayout *nm = addGroup(outer, "NMEA Output Interval (s)", 1, 0);
    addRow2(nm, "RMC,GGA / GSA,GSV", "out-nmeaintv1", "out-nmeaintv2");

    QFormLayout *st = addGroup(outer, "Status Output", 1, 1);
    addRow(st, "Solution Status", "out-outstat");

    outer->setRowStretch(2, 1);
    return page;
}
//------------------------------------------------------------------------------
// Statistics: measurement errors, initial state std, process noise
QWidget *OptDialog::buildStatistics()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *er = addGroup(outer, "Measurement Error Ratio", 0, 0);
    addRow2(er, "Code/Carrier-Phase (L1 / L2)", "stats-eratio1", "stats-eratio2");
    addRow (er, "URA Ratio", "stats-uraratio");

    QFormLayout *es = addGroup(outer, "Measurement Error Std", 0, 1);
    addRow2(es, "Carrier-Phase (m) / Elevation (m)", "stats-errphase", "stats-errphaseel");
    addRow2(es, "Baseline (m/10km) / Doppler (Hz)", "stats-errphasebl", "stats-errdoppler");

    QFormLayout *si = addGroup(outer, "Initial State Std (GNSS)", 1, 0);
    addRow2(si, "Bias (m) / Iono (m)", "stats-stdbias", "stats-stdiono");
    addRow2(si, "Trop (m) / Position (m)", "stats-stdtrop", "stats-stdpos");
    addRow2(si, "Clock (m) / Clock Drift (m/s)", "stats-stdclko", "stats-stdclkd");

    QFormLayout *sn = addGroup(outer, "Initial State Std (INS)", 1, 1);
    addRow2(sn, "Velocity (m/s) / Attitude (deg)", "stats-stdvel", "stats-stdatt");
    addRow2(sn, "Accel Bias (ug) / Gyro Bias (deg/h)", "stats-stdaccb", "stats-stdgyrob");

    QFormLayout *pn = addGroup(outer, "Process Noise (GNSS)", 2, 0);
    addRow2(pn, "Bias (m) / Iono (m)", "stats-prnbias", "stats-prniono");
    addRow2(pn, "Trop (m) / Position (m)", "stats-prntrop", "stats-prnpos");
    addRow2(pn, "Accel H / V (m/s^2)", "stats-prnaccelh", "stats-prnaccelv");
    addRow2(pn, "IFB (m) / Clock Drift (m)", "stats-prnifb", "stats-prnclkd");

    QFormLayout *pi = addGroup(outer, "Process Noise (INS)", 2, 1);
    addRow2(pi, "Accel / Gyro Noise", "stats-prnaccn", "stats-prngyron");
    addRow2(pi, "Accel / Gyro Bias", "stats-prnaccb", "stats-prngyrob");

    QFormLayout *pc = addGroup(outer, "Process Noise: Receiver Clock (m)", 3, 0);
    addRow2(pc, "GPS / GLONASS", "stats-prnclk_gps", "stats-prnclk_glo");
    addRow2(pc, "Galileo / QZSS", "stats-prnclk_gal", "stats-prnclk_qzs");
    addRow2(pc, "BeiDou / BeiDou-2", "stats-prnclk_bds", "stats-prnclk_bds2");
    addRow (pc, "NavIC", "stats-prnclk_irn");

    QFormLayout *ck = addGroup(outer, "Receiver Clock", 3, 1);
    addRow2(ck, "Jump Threshold (s) / Stability (s/s)", "stats-clkjump", "stats-clkstab");

    outer->setRowStretch(4, 1);
    return page;
}
//------------------------------------------------------------------------------
// "Select from station position file..." button for ant1 / ant2
QWidget *OptDialog::refButton(const char *prefix)
{
    auto *btn = new QPushButton("Select from Station Position File...");
    QString pre(prefix);
    connect(btn, &QPushButton::clicked, this, [this, pre] {
        RefDialog dlg(this);
        opt_t *of = find("file-staposfile");
        if (of) dlg.StaPosFile = fromLib((char *)of->var);
        // rover position is the reference for the distance column
        opt_t *r1 = find("ant1-pos1"), *r2 = find("ant1-pos2"), *r3 = find("ant1-pos3");
        if (r1 && r2 && r3) {
            dlg.RovPos[0] = *(double *)r1->var;
            dlg.RovPos[1] = *(double *)r2->var;
            dlg.RovPos[2] = *(double *)r3->var;
        }
        if (dlg.exec() != QDialog::Accepted) return;
        for (int i = 0; i < 3; i++) {
            opt_t *o = find(qPrintable(QString("%1-pos%2").arg(pre).arg(i + 1)));
            if (o) *(double *)o->var = dlg.Pos[i];
        }
        loadValues();
    });
    return btn;
}
//------------------------------------------------------------------------------
// Positions: rover / base antenna position, offsets and IMU mounting
QWidget *OptDialog::buildPositions()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *rv = addGroup(outer, "Rover Antenna", 0, 0);
    addRow (rv, "Position Type", "ant1-postype");
    addRow2(rv, "Lat/X (deg|m) / Lon/Y (deg|m)", "ant1-pos1", "ant1-pos2");
    addRow (rv, "Height/Z (m)", "ant1-pos3");
    rv->addRow("", refButton("ant1"));
    addRow (rv, "Antenna Type", "ant1-anttype");
    addRow2(rv, "Delta E / N (m)", "ant1-antdele", "ant1-antdeln");
    addRow (rv, "Delta U (m)", "ant1-antdelu");

    QFormLayout *bs = addGroup(outer, "Base Station Antenna", 0, 1);
    addRow (bs, "Position Type", "ant2-postype");
    addRow2(bs, "Lat/X (deg|m) / Lon/Y (deg|m)", "ant2-pos1", "ant2-pos2");
    addRow (bs, "Height/Z (m)", "ant2-pos3");
    bs->addRow("", refButton("ant2"));
    addRow (bs, "Antenna Type", "ant2-anttype");
    addRow2(bs, "Delta E / N (m)", "ant2-antdele", "ant2-antdeln");
    addRow (bs, "Delta U (m)", "ant2-antdelu");
    addRow2(bs, "Max Averaging Epochs / Reset", "ant2-maxaveep", "ant2-initrst");

    // 1.3.0: IMU lever arm and mounting rotation (rover only)
    QFormLayout *imu = addGroup(outer, "IMU Mounting (rover)", 1, 0);
    addRow2(imu, "Lever Arm X / Y (m)", "ant1-antimux", "ant1-antimuy");
    addRow (imu, "Lever Arm Z (m)", "ant1-antimuz");
    addRow2(imu, "Rotation X / Y (deg)", "ant1-rotimux", "ant1-rotimuy");
    addRow (imu, "Rotation Z (deg)", "ant1-rotimuz");

    outer->setRowStretch(2, 1);
    return page;
}
//------------------------------------------------------------------------------
// Files: input products, output logs, working directory
QWidget *OptDialog::buildFiles()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *pr = addGroup(outer, "Products / Corrections", 0, 0);
    addRow(pr, "Satellite Antenna PCV (ANTEX)", "file-satantfile");
    addRow(pr, "Receiver Antenna PCV", "file-rcvantfile");
    addRow(pr, "Station Position File", "file-staposfile");
    addRow(pr, "Geoid Data File", "file-geoidfile");
    addRow(pr, "Ionosphere File (IONEX)", "file-ionofile");
    addRow(pr, "DCB Data File", "file-dcbfile");
    addRow(pr, "EOP Data File", "file-eopfile");
    addRow(pr, "Ocean Tide Loading (BLQ)", "file-blqfile");
    addRow(pr, "FCB Data File", "file-fcbfile");
    addRow(pr, "Bias SINEX File", "file-biafile");
    addRow(pr, "PPP Kalman Filter File", "file-pppkf");

    QFormLayout *ms = addGroup(outer, "Masks / Exclusions", 1, 0);
    addRow(ms, "Elevation Mask File", "file-elmaskfile");
    addRow(ms, "Mask File", "file-maskfile");
    addRow(ms, "Excluded Satellites File", "file-exclsatsfile");

    QFormLayout *ou = addGroup(outer, "Output / Working", 2, 0);
    addRow(ou, "Solution Status File", "file-solstatfile");
    addRow(ou, "Trace File", "file-tracefile");
    addRow(ou, "FTP/HTTP Temp Directory", "file-tempdir");

    outer->setRowStretch(3, 1);
    return page;
}
//------------------------------------------------------------------------------
// Misc: interpolation, per-format extra options
QWidget *OptDialog::buildMisc()
{
    auto *page = new QWidget();
    auto *outer = new QGridLayout(page);

    QFormLayout *g = addGroup(outer, "Processing", 0, 0);
    addRow(g, "Time Interpolation of Base Obs", "misc-timeinterp");
    addRow(g, "SBAS Satellite Selection (0:all)", "misc-sbasatsel");

    QFormLayout *e = addGroup(outer, "Extra Options", 1, 0);
    addRow(e, "RINEX Option (Rover)", "misc-rnxopt1");
    addRow(e, "RINEX Option (Base)", "misc-rnxopt2");
    addRow(e, "PPP Option", "misc-pppopt");
    addRow(e, "RTCM Option", "misc-rtcmopt");

    outer->setRowStretch(2, 1);
    return page;
}
//------------------------------------------------------------------------------
// anything not placed on a hand-built tab (keeps vendor additions visible)
void OptDialog::buildGeneratedTabs()
{
    // count what is left over first; skip the tab entirely when nothing is
    int left = 0;
    for (opt_t *o = sysopts; *o->name; o++) if (!placed.contains(o->name)) left++;
    if (!left) return;

    QMap<QString, QFormLayout *> forms;
    for (auto &t : GEN_TABS) {
        if (forms.contains(t.tab)) continue;
        auto *page = new QWidget();
        auto *form = new QFormLayout(page);
        form->setLabelAlignment(Qt::AlignRight);
        auto *scroll = new QScrollArea();
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        tabs->addTab(scroll, t.tab);
        forms[t.tab] = form;
    }
    auto *misc = forms["Othe&r"];

    for (opt_t *opt = sysopts; *opt->name; opt++) {
        QString name(opt->name);
        if (placed.contains(name)) continue;

        QString prefix = name.section('-', 0, 0);
        QFormLayout *form = misc;
        for (auto &t : GEN_TABS) {
            if (prefix == t.prefix) { form = forms[t.tab]; break; }
        }
        if (name == "ant1-postype") form->addRow(new QLabel("<b>Rover</b>"));
        if (name == "ant2-postype") form->addRow(new QLabel("<b>Base Station</b>"));

        QWidget *editor = editorFor(opt->name);
        if (name.startsWith("file-")) {
            auto *edit = rows.last().edit;
            if (edit) {
                auto *browse = new QPushButton("...");
                browse->setFixedWidth(28);
                auto *wrap = new QWidget();
                auto *h = new QHBoxLayout(wrap);
                h->setContentsMargins(0, 0, 0, 0);
                h->addWidget(edit);
                h->addWidget(browse);
                editor = wrap;
                connect(browse, &QPushButton::clicked, this, [this, edit] {
                    QString f = QFileDialog::getOpenFileName(this, "File", edit->text());
                    if (!f.isEmpty()) edit->setText(f);
                });
            }
        }
        form->addRow(name.section('-', 1) + ":", editor);
    }
}
//------------------------------------------------------------------------------
OptDialog::OptDialog(opt_t *extraOpts_, prcopt_t *prcopt_, solopt_t *solopt_,
                     filopt_t *filopt_, QWidget *parent)
    : QDialog(parent), extraOpts(extraOpts_), prcopt(prcopt_),
      solopt(solopt_), filopt(filopt_)
{
    setWindowTitle("Options");

    tabs = new QTabWidget();
    tabs->addTab(buildSetting1(), "Setting&1");
    tabs->addTab(buildSetting2(), "Setting&2");
    tabs->addTab(buildSetting3(), "Setting&3");
    tabs->addTab(buildOutput(), "&Output");
    tabs->addTab(buildStatistics(), "S&tatistics");
    tabs->addTab(buildPositions(), "&Positions");
    tabs->addTab(buildFiles(), "&Files");
    tabs->addTab(buildMisc(), "&Misc");
    buildGeneratedTabs();

    auto *btnLoad   = new QPushButton("&Load...");
    auto *btnSave   = new QPushButton("&Save...");
    auto *btnOk     = new QPushButton("&OK");
    auto *btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);
    auto *btns = new QHBoxLayout();
    btns->addWidget(btnLoad);
    btns->addWidget(btnSave);
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addWidget(tabs, 1);
    vbox->addLayout(btns);

    connect(btnLoad,   &QPushButton::clicked, this, &OptDialog::btnLoadClicked);
    connect(btnSave,   &QPushButton::clicked, this, &OptDialog::btnSaveClicked);
    connect(btnOk,     &QPushButton::clicked, this, &OptDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    // conditional graying follows the positioning mode
    for (Row &row : rows) {
        if (row.combo && row.opt && !strcmp(row.opt->name, "pos1-posmode")) {
            connect(row.combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &OptDialog::updateEnable);
        }
    }
    resize(900, 660);
}
//------------------------------------------------------------------------------
// SNR mask sub-dialog: on/off per rover/base + threshold grid (L1/L2/L5 x 9)
void OptDialog::snrMaskDialog()
{
    static const char *bands[] = {"L1", "L2", "L5"};
    static const char *keys[]  = {"pos1-snrmask_L1", "pos1-snrmask_L2", "pos1-snrmask_L5"};
    static const char *elev[]  = {"<5","15","25","35","45","55","65","75",">85"};

    QDialog dlg(this);
    dlg.setWindowTitle("SNR Mask");
    auto *v = new QVBoxLayout(&dlg);

    auto *top = new QHBoxLayout();
    QCheckBox *rover = new QCheckBox("Rover"), *base = new QCheckBox("Base Station");
    opt_t *optR = find("pos1-snrmask_r"), *optB = find("pos1-snrmask_b");
    if (optR) rover->setChecked(*(int *)optR->var != 0);
    if (optB) base->setChecked(*(int *)optB->var != 0);
    top->addWidget(rover);
    top->addWidget(base);
    top->addStretch();
    top->addWidget(new QLabel(QString::fromUtf8("Elevation (deg) / (dBHz)")));
    v->addLayout(top);

    auto *grid = new QGridLayout();
    for (int c = 0; c < 9; c++) grid->addWidget(new QLabel(elev[c]), 0, c + 1);
    QLineEdit *cell[3][9];
    for (int r = 0; r < 3; r++) {
        grid->addWidget(new QLabel(bands[r]), r + 1, 0);
        opt_t *opt = find(keys[r]);
        QStringList vals = opt ? fromLib((char *)opt->var).split(',') : QStringList();
        for (int c = 0; c < 9; c++) {
            cell[r][c] = new QLineEdit(c < vals.size() ? vals[c].trimmed() : "0");
            cell[r][c]->setMaximumWidth(46);
            grid->addWidget(cell[r][c], r + 1, c + 1);
        }
    }
    v->addLayout(grid);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return;

    if (optR) *(int *)optR->var = rover->isChecked() ? 1 : 0;
    if (optB) *(int *)optB->var = base->isChecked() ? 1 : 0;
    for (int r = 0; r < 3; r++) {
        opt_t *opt = find(keys[r]);
        if (!opt) continue;
        QStringList vals;
        for (int c = 0; c < 9; c++) {
            vals << (cell[r][c]->text().isEmpty() ? "0" : cell[r][c]->text());
        }
        strcpy((char *)opt->var, toLib(vals.join(",")).constData());
    }
}
//------------------------------------------------------------------------------
// SNR weighting: per-signal threshold table (see snrwdlg.cpp)
void OptDialog::snrWeightDialog()
{
    SnrWeightDialog dlg(rawKept, this);
    if (dlg.exec() != QDialog::Accepted) return;
    rawKept = dlg.rawLines();       // carried into the next save
}
//------------------------------------------------------------------------------
void OptDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;
    if (!confFile.isEmpty()) rawKept = keepRawOpts(confFile);
    loadValues();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
// widget values <- current sysopts variables
void OptDialog::loadValues()
{
    char buf[1024];

    for (Row &row : rows) {
        if (row.bit) {                          // bitmask member
            row.check->setChecked(*(int *)row.opt->var & row.bit);
            continue;
        }
        *buf = '\0';
        opt2str(row.opt, buf);
        if (row.check) {                        // on/off enum
            row.check->setChecked(*(int *)row.opt->var != 0);
        }
        else if (row.combo) {
            int idx = row.combo->findData(*(int *)row.opt->var);
            if (idx < 0) idx = row.combo->findText(fromLib(buf).trimmed());
            row.combo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        else {
            row.edit->setText(fromLib(buf));
        }
    }
    updateEnable();
}
//------------------------------------------------------------------------------
// widget values -> sysopts variables
void OptDialog::applyValues()
{
    QSet<opt_t *> bitDone;

    for (Row &row : rows) {
        if (row.bit) {
            if (!bitDone.contains(row.opt)) {   // clear once, then OR the bits
                *(int *)row.opt->var = 0;
                bitDone.insert(row.opt);
            }
            if (row.check->isChecked()) *(int *)row.opt->var |= row.bit;
            continue;
        }
        if (row.check) {
            *(int *)row.opt->var = row.check->isChecked() ? 1 : 0;
            continue;
        }
        if (row.combo) {                        // write the value directly
            *(int *)row.opt->var = row.combo->currentData().toInt();
            continue;
        }
        str2opt(row.opt, toLib(row.edit->text()).constData());
    }
}
//------------------------------------------------------------------------------
// gray out options that do not apply to the selected positioning mode
void OptDialog::updateEnable()
{
    int mode = -1;
    for (Row &row : rows) {
        if (row.combo && row.opt && !strcmp(row.opt->name, "pos1-posmode")) {
            mode = row.combo->currentIndex();
            break;
        }
    }
    if (mode < 0) return;

    bool isPPP  = mode >= PMODE_PPP_KINEMA;
    bool isRel  = mode >= PMODE_DGPS && mode <= PMODE_FIXED;   // needs a base
    bool canAR  = mode == PMODE_KINEMA || mode == PMODE_STATIC ||
                  mode == PMODE_MOVEB  || mode == PMODE_FIXED || isPPP;

    static const QStringList relOnly = {
        "pos2-maxage", "pos2-baselen", "pos2-basesig", "pos1-snrmask_b"
    };
    static const QStringList pppOnly = {
        "pos2-ionocorr", "pos2-pppsatcb", "pos2-pppsatpb", "pos2-uncorrbias",
        "pos2-maxbiasdt", "pos2-bds2bias"
    };
    static const QStringList arOnly = {
        "pos2-armode", "pos2-gloarmode", "pos2-bdsarmode", "pos2-arsys",
        "pos2-arthres", "pos2-arthres1", "pos2-arthres2", "pos2-arthres3",
        "pos2-arthres4", "pos2-arlockcnt", "pos2-arelmask", "pos2-arminfix",
        "pos2-elmaskhold", "pos2-armaxiter", "pos2-aroutcnt"
    };
    for (Row &row : rows) {
        if (!row.opt) continue;
        QString n(row.opt->name);
        QWidget *w = row.check ? (QWidget *)row.check
                   : row.combo ? (QWidget *)row.combo : (QWidget *)row.edit;
        if (!w) continue;
        if      (relOnly.contains(n)) w->setEnabled(isRel);
        else if (pppOnly.contains(n)) w->setEnabled(isPPP);
        else if (arOnly.contains(n))  w->setEnabled(canAR);
    }
}
//------------------------------------------------------------------------------
void OptDialog::btnLoadClicked()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Load Options", confFile, "Config (*.conf);;All (*)");
    if (file.isEmpty()) return;
    resetsysopts();
    QByteArray fileB = toLib(file);
    if (!loadopts(fileB.constData(), sysopts) ||
        (extraOpts && !loadopts(fileB.constData(), extraOpts))) {
        QMessageBox::warning(this, "Options", "conf read error: " + file);
        return;
    }
    confFile = file;
    if (ssrVar) fixSsrFromConf(file, ssrVar);   // base-lib str2enum workaround
    rawKept = keepRawOpts(file);                // base-lib opt2str workaround
    loadValues();
}
//------------------------------------------------------------------------------
void OptDialog::btnSaveClicked()
{
    QString file = QFileDialog::getSaveFileName(
        this, "Save Options", confFile, "Config (*.conf);;All (*)");
    if (file.isEmpty()) return;
    if (!file.contains('.')) file += ".conf";

    applyValues();
    QString comment = QString("rtknavi_qt options (MALIB ver.%1 %2)")
                          .arg(VER_MALIB, PATCH_LEVEL_MALIB);
    bool ok;
    if (extraOpts) {
        ok = saveopts(toLib(file).constData(), "w", toLib(comment).constData(), extraOpts) &&
             saveopts(toLib(file).constData(), "a", NULL, sysopts);
    }
    else {
        ok = saveopts(toLib(file).constData(), "w", toLib(comment).constData(), sysopts);
    }
    if (!ok) {
        QMessageBox::warning(this, "Options", "conf write error: " + file);
        return;
    }
    restoreRawOpts(file, rawKept);              // base-lib opt2str workaround
    confFile = file;
}
//------------------------------------------------------------------------------
void OptDialog::btnOkClicked()
{
    applyValues();
    getsysopts(prcopt, solopt, filopt);
    accept();
}
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// exhaustive widget-to-option mapping check (--selftest)
//
// For every registered editor: drive it to each possible value, apply, and
// verify that (a) the intended option took exactly that value and (b) none
// of the other options moved (catches wrong wiring / searchopt aliasing).
//------------------------------------------------------------------------------
static QStringList optSnapshot()
{
    QStringList snap;
    char buf[1024];
    for (opt_t *opt = sysopts; *opt->name; opt++) {
        *buf = '\0';
        opt2str(opt, buf);
        snap << QString("%1=%2").arg(opt->name, buf);
    }
    return snap;
}
static QStringList snapDiff(const QStringList &a, const QStringList &b)
{
    QStringList diff;
    for (int i = 0; i < a.size() && i < b.size(); i++) {
        if (a[i] != b[i]) diff << a[i].section('=', 0, 0);
    }
    return diff;
}
int OptDialog::selfTest(QStringList &ng)
{
    int nfail = 0, ncase = 0;
    loadValues();                       // widgets must mirror the options first
    applyValues();
    QStringList before = optSnapshot();

    auto verify = [&](const QString &name, const QString &expect) {
        ncase++;
        QStringList after = optSnapshot();
        QStringList changed = snapDiff(before, after);
        char buf[1024] = "";
        opt_t *opt = nullptr;
        for (opt_t *o = sysopts; *o->name; o++) {
            if (name == o->name) { opt = o; break; }
        }
        if (opt) opt2str(opt, buf);
        QString got = QString(buf).trimmed();

        if (changed.size() > 1 || (changed.size() == 1 && changed[0] != name)) {
            ng << QString("%1: 他のオプションが動いた -> %2")
                      .arg(name, changed.join(","));
            nfail++;
        }
        else if (!expect.isEmpty() && got != expect) {
            ng << QString("%1: 期待 \"%2\" 実際 \"%3\"").arg(name, expect, got);
            nfail++;
        }
        before = after;
    };

    for (Row &row : rows) {
        QString name = row.opt->name;

        if (row.bit) {                          // bitmask member
            bool orig = row.check->isChecked();
            row.check->setChecked(!orig);
            applyValues();
            verify(name, "");                   // isolation only (int value)
            row.check->setChecked(orig);
            applyValues();
            before = optSnapshot();
            continue;
        }
        if (row.check) {                        // on/off checkbox
            bool orig = row.check->isChecked();
            row.check->setChecked(!orig);
            applyValues();
            verify(name, !orig ? "on" : "off");
            row.check->setChecked(orig);
            applyValues();
            before = optSnapshot();
            continue;
        }
        if (row.combo) {                        // enum combo: every entry
            int orig = row.combo->currentIndex();
            for (int j = 0; j < row.combo->count(); j++) {
                row.combo->setCurrentIndex(j);
                applyValues();
                int want = row.combo->itemData(j).toInt();
                int got = *(int *)row.opt->var;
                ncase++;
                QStringList after = optSnapshot();
                QStringList changed = snapDiff(before, after);
                if (changed.size() > 1 || (changed.size() == 1 && changed[0] != name)) {
                    ng << QString("%1: 他のオプションが動いた -> %2")
                              .arg(name, changed.join(","));
                    nfail++;
                }
                else if (got != want) {
                    ng << QString("%1: 期待 %2(%3) 実際 %4")
                              .arg(name).arg(want).arg(row.combo->itemText(j)).arg(got);
                    nfail++;
                }
                before = after;
            }
            row.combo->setCurrentIndex(orig);
            applyValues();
            before = optSnapshot();
            continue;
        }
        if (row.edit) {                         // number / string edit
            if (QString(name).startsWith("pos1-snrwthres")) continue; // format 4
            QString orig = row.edit->text();
            QString sentinel = row.opt->format == 0 ? "7" :
                               row.opt->format == 1 ? "7.25" : "TESTVAL";
            row.edit->setText(sentinel);
            applyValues();
            verify(name, sentinel == "7.25" ? "7.25" : sentinel);
            row.edit->setText(orig);
            applyValues();
            before = optSnapshot();
        }
    }
    ng << QString("--- %1 ケース実行 / %2 失敗").arg(ncase).arg(nfail);
    return nfail;
}

//------------------------------------------------------------------------------
// coverage report: for every sysopts entry, whether a widget on this dialog
// edits it (= covered by --selftest) or it is handled elsewhere.
//------------------------------------------------------------------------------
int OptDialog::coverage(QStringList &out)
{
    QSet<QString> widget;
    for (const Row &row : rows) if (row.opt) widget.insert(row.opt->name);

    int total = 0, covered = 0;
    for (opt_t *opt = sysopts; *opt->name; opt++) {
        total++;
        bool w = widget.contains(opt->name);
        if (w) covered++;
        out << QString("%1\t%2\t%3\t%4")
                   .arg(opt->name)
                   .arg(opt->format)
                   .arg(w ? "widget" : (placed.contains(opt->name) ? "subdialog" : "UNPLACED"))
                   .arg(w ? "selftest" : "-");
    }
    out << QString("#TOTAL\t%1\t%2").arg(total).arg(covered);
    return covered;
}
