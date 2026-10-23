//------------------------------------------------------------------------------
// snrwdlg.cpp : SNR weighting dialog (MALIB 1.3.0)
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "appstyle.h"

#include "snrwdlg.h"

static const char *SYS_KEY[SNRW_NSYS] = {"gps", "glo", "gal", "qzs", "bds"};
static const char *SYS_LAB[SNRW_NSYS] = {"GPS", "GLONASS", "Galileo", "QZSS", "BeiDou"};

// signal codes offered when adding a row (from the vendor sample conf)
static const char *SIG_CAND[SNRW_NSYS] = {
    "1C 1W 2P 2L 2W 5Q 5X",
    "1C 1P 2C 2P",
    "1C 1X 5Q 5X 7Q 7X 6C",
    "1C 1L 1E 2L 5Q 5P",
    "2I 2Q 6I 6Q 5P 5X 7I 7Q"
};

//------------------------------------------------------------------------------
SnrWeightDialog::SnrWeightDialog(const QStringList &rawLines, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("SNR Weighting");

    tabs = new QTabWidget();
    for (int s = 0; s < SNRW_NSYS; s++) {
        auto *page = new QWidget();
        auto *v = new QVBoxLayout(page);

        auto *top = new QFormLayout();
        margin[s] = new QLineEdit();
        eratio[s] = new QLineEdit();
        margin[s]->setMaximumWidth(90);
        eratio[s]->setMaximumWidth(90);
        margin[s]->setToolTip(QString("pos1-snrwmargin_%1").arg(SYS_KEY[s]));
        eratio[s]->setToolTip(QString("pos1-snrweratio_%1").arg(SYS_KEY[s]));
        auto *hw = new QWidget();
        auto *hh = new QHBoxLayout(hw);
        hh->setContentsMargins(0, 0, 0, 0);
        hh->addWidget(new QLabel("Margin (dBHz)"));
        hh->addWidget(margin[s]);
        hh->addSpacing(16);
        hh->addWidget(new QLabel("Error Ratio"));
        hh->addWidget(eratio[s]);
        hh->addStretch();
        top->addRow(hw);
        v->addLayout(top);

        v->addWidget(new QLabel(
            QString::fromUtf8("Threshold (dBHz) by elevation — pos1-snrwthres_%1")
                .arg(SYS_KEY[s])));

        table[s] = new QTableWidget(0, SNRWTHRESNUM + 1);
        QStringList head;
        head << "Signal";
        for (int i = 0; i < SNRWTHRESNUM; i++) head << QString::number(5 + i * 5);
        table[s]->setHorizontalHeaderLabels(head);
        table[s]->verticalHeader()->setVisible(false);
        table[s]->horizontalHeader()->setDefaultSectionSize(48);
        table[s]->setColumnWidth(0, 62);
        v->addWidget(table[s], 1);

        auto *btns = new QHBoxLayout();
        auto *add = new QPushButton("Add Signal...");
        auto *del = new QPushButton("Remove Signal");
        connect(add, &QPushButton::clicked, this, &SnrWeightDialog::btnAddSignalClicked);
        connect(del, &QPushButton::clicked, this, &SnrWeightDialog::btnRemoveSignalClicked);
        btns->addWidget(add);
        btns->addWidget(del);
        btns->addStretch();
        v->addLayout(btns);

        tabs->addTab(page, SYS_LAB[s]);
    }

    parseLines(rawLines);

    // margin / eratio come straight from the option variables
    for (int s = 0; s < SNRW_NSYS; s++) {
        opt_t *om = searchopt(qPrintable(QString("pos1-snrwmargin_%1").arg(SYS_KEY[s])), sysopts);
        opt_t *oe = searchopt(qPrintable(QString("pos1-snrweratio_%1").arg(SYS_KEY[s])), sysopts);
        if (om) margin[s]->setText(QString::number(*(double *)om->var));
        if (oe) eratio[s]->setText(QString::number(*(double *)oe->var));
    }

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &SnrWeightDialog::btnOkClicked);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    v->setSpacing(UI_SPACING);
    v->addWidget(tabs, 1);
    auto *note = new QLabel(
        QString::fromUtf8("しきい値は仰角 5°〜85° の 5° 刻み %1 点。"
                          "測位時はこの間を線形補間して使われる。").arg(SNRWTHRESNUM));
    note->setStyleSheet("color:gray;");
    note->setWordWrap(true);
    v->addWidget(note);
    v->addWidget(bb);

    resize(980, 460);
}
//------------------------------------------------------------------------------
void SnrWeightDialog::addSignalRow(int sys, const QString &sig,
                                   const QStringList &values)
{
    QTableWidget *t = table[sys];
    int r = t->rowCount();
    t->insertRow(r);
    t->setItem(r, 0, new QTableWidgetItem(sig));
    for (int i = 0; i < SNRWTHRESNUM; i++) {
        QString v = i < values.size() ? values[i].trimmed() : "0.0";
        t->setItem(r, i + 1, new QTableWidgetItem(v));
    }
}
//------------------------------------------------------------------------------
// parse "pos1-snrwthres_gps= 1C= 0.0, 0.0, ..." into the per-system tables
void SnrWeightDialog::parseLines(const QStringList &src)
{
    QRegularExpression re(
        "^\\s*pos1-snrwthres_([a-z]+)\\s*=\\s*([0-9A-Za-z]+)\\s*=\\s*(.*)$");

    for (const QString &line : src) {
        QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch()) continue;
        QString key = m.captured(1);
        int sys = -1;
        for (int s = 0; s < SNRW_NSYS; s++) if (key == SYS_KEY[s]) sys = s;
        if (sys < 0) continue;
        QString body = m.captured(3).section('#', 0, 0);   // strip trailing comment
        addSignalRow(sys, m.captured(2), body.split(','));
    }
}
//------------------------------------------------------------------------------
void SnrWeightDialog::btnAddSignalClicked()
{
    int s = tabs->currentIndex();
    if (s < 0 || s >= SNRW_NSYS) return;

    bool ok;
    QStringList cand = QString(SIG_CAND[s]).split(' ');
    QString sig = QInputDialog::getItem(this, "Add Signal",
        QString("signal code for %1:").arg(SYS_LAB[s]), cand, 0, true, &ok);
    if (!ok || sig.isEmpty()) return;
    addSignalRow(s, sig.trimmed(), QStringList());
}
//------------------------------------------------------------------------------
void SnrWeightDialog::btnRemoveSignalClicked()
{
    int s = tabs->currentIndex();
    if (s < 0 || s >= SNRW_NSYS) return;
    int r = table[s]->currentRow();
    if (r >= 0) table[s]->removeRow(r);
}
//------------------------------------------------------------------------------
void SnrWeightDialog::btnOkClicked()
{
    lines.clear();

    for (int s = 0; s < SNRW_NSYS; s++) {
        // margin / error ratio -> option variables
        opt_t *om = searchopt(qPrintable(QString("pos1-snrwmargin_%1").arg(SYS_KEY[s])), sysopts);
        opt_t *oe = searchopt(qPrintable(QString("pos1-snrweratio_%1").arg(SYS_KEY[s])), sysopts);
        if (om) *(double *)om->var = margin[s]->text().toDouble();
        if (oe) *(double *)oe->var = eratio[s]->text().toDouble();

        opt_t *ot = searchopt(qPrintable(QString("pos1-snrwthres_%1").arg(SYS_KEY[s])), sysopts);

        for (int r = 0; r < table[s]->rowCount(); r++) {
            QTableWidgetItem *si = table[s]->item(r, 0);
            if (!si || si->text().trimmed().isEmpty()) continue;
            QString sig = si->text().trimmed();

            QStringList vals;
            for (int i = 0; i < SNRWTHRESNUM; i++) {
                QTableWidgetItem *it = table[s]->item(r, i + 1);
                QString v = it ? it->text().trimmed() : QString();
                vals << (v.isEmpty() ? "0.0" : v);
            }
            QString body = sig + "= " + vals.join(", ");
            lines << QString("pos1-snrwthres_%1= %2").arg(SYS_KEY[s], body);

            // push into prcopt through the library's own parser
            if (ot) str2opt(ot, qPrintable(body));
        }
    }
    accept();
}
//------------------------------------------------------------------------------
