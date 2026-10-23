//------------------------------------------------------------------------------
// snrwdlg.h : SNR weighting dialog (MALIB 1.3.0)
//
// Edits pos1-snrwthres_<sys> (per signal, 17 thresholds at 5..85 deg) plus
// pos1-snrwmargin_<sys> / pos1-snrweratio_<sys>.
//
// The threshold table cannot be serialized by opt2str() (format 4 has no
// case there), so it is carried as raw conf lines - see basefix.h. This
// dialog parses those lines, edits them, writes them back, and also pushes
// the values into prcopt via str2opt() so a running server sees them.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef SNRWDLG_H
#define SNRWDLG_H

#include <QDialog>
#include <QStringList>

#include "rtklib.h"

class QLineEdit;
class QTableWidget;
class QTabWidget;

#define SNRW_NSYS 5                     // gps/glo/gal/qzs/bds

class SnrWeightDialog : public QDialog
{
    Q_OBJECT
public:
    // rawLines: the pos1-snrwthres_* conf lines (in/out)
    explicit SnrWeightDialog(const QStringList &rawLines, QWidget *parent = nullptr);

    QStringList rawLines() const { return lines; }

private slots:
    void btnAddSignalClicked();
    void btnRemoveSignalClicked();
    void btnOkClicked();

private:
    void parseLines(const QStringList &src);
    void addSignalRow(int sys, const QString &sig, const QStringList &values);

    QStringList   lines;
    QTabWidget   *tabs;
    QTableWidget *table[SNRW_NSYS];
    QLineEdit    *margin[SNRW_NSYS];
    QLineEdit    *eratio[SNRW_NSYS];
};

#endif // SNRWDLG_H
