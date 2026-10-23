//------------------------------------------------------------------------------
// outstrdlg.h : output/log streams dialog (port of winapp/rtknavi outstrdlg
//               + logstrdlg, unified; adapted to 1.3.0 NSTROUT=2 / NSTRLOG=4)
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/outstrdlg + logstrdlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef OUTSTRDLG_H
#define OUTSTRDLG_H

#include <QDialog>

#include "rtklib.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;

#define MAXOUTSTR 4                     // max rows (log uses 4)

class OutStrDialog : public QDialog
{
    Q_OBJECT
public:
    // mode 0: output streams (NSTROUT rows, with solution format column)
    // mode 1: log streams (NSTRLOG rows, no format column)
    explicit OutStrDialog(int mode, QWidget *parent = nullptr);

    int     strType[MAXOUTSTR] = {STR_NONE, STR_NONE, STR_NONE, STR_NONE};
    QString strPath[MAXOUTSTR];
    int     strFmt[MAXOUTSTR]  = {SOLF_LLH, SOLF_NMEA, 0, 0};
    QString history[10];

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOkClicked();
    void updateEnable();

private:
    void btnStrClicked(int i);
    void btnFileClicked(int i);
    int  kindOf(int type) const;
    QString filePathWithTags(const QString &base) const;

    int nstr;                           // number of rows
    int mode;

    QCheckBox   *streamC[MAXOUTSTR];
    QComboBox   *stream[MAXOUTSTR], *format[MAXOUTSTR];
    QPushButton *btnStr[MAXOUTSTR];
    QLineEdit   *filePath[MAXOUTSTR];
    QPushButton *btnFile[MAXOUTSTR];
    QCheckBox   *timeTagC;
    QLineEdit   *swapIntv;
    QLabel      *labelFile;

    QString paths[MAXOUTSTR][4];
};

#endif // OUTSTRDLG_H
