//------------------------------------------------------------------------------
// conndlg.h : real-time connection settings for RTKPLOT-Qt
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtkplot/conndlg (RTKLIB 2.4.3 b35).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef CONNDLG_H
#define CONNDLG_H

#include <QDialog>

#include "rtklib.h"

class QComboBox;
class QLineEdit;
class QPushButton;

class ConnectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectDialog(QWidget *parent = nullptr);

    // in/out: caller sets before exec(), reads back when Accepted
    int     stream[2]  = {STR_NONE, STR_NONE};   // STR_*
    int     format[2]  = {SOLF_LLH, SOLF_LLH};
    QString paths[2][3];                          // serial / tcp / file
    int     timeForm   = 0;                       // 0:ww ssss 1:gpst 2:utc 3:jst
    int     degForm    = 0;                       // 0:ddd.ddd 1:ddd mm ss
    QString fieldSep;
    int     timeOutTime = 0;
    int     reConnTime  = 10000;
    QString tcpHistory[10];

    bool connectNow = false;      // OK pressed via [Connect] rather than [Set]

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOptClicked(int i);
    void accepted(bool doConnect);
    void updateEnable();

private:
    QComboBox *selStream[2], *solFormat[2], *timeFormS, *degFormS;
    QPushButton *btnOpt[2];
    QLineEdit *fieldSepE, *timeOutE, *reConnE;
};

#endif // CONNDLG_H
