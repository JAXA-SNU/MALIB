//------------------------------------------------------------------------------
// ftpoptdlg.h : FTP/HTTP download options (Qt port of winapp/appcmn/ftpoptdlg)
//
// Path format: [user[:passwd]@]addr/path[::T=poff,tint,toff,tret]
//   poff/tint/toff are seconds in the conf but shown in hours
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/appcmn/ftpoptdlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef FTPOPTDLG_H
#define FTPOPTDLG_H

#include <QDialog>

#define MAXHIST 10

class QComboBox;
class QLineEdit;

class FtpOptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FtpOptDialog(QWidget *parent = nullptr);

    int     Opt = 0;                    // 0: FTP, 1: HTTP
    QString Path;
    QString History[MAXHIST];

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOkClicked();

private:
    void addHist();

    QComboBox *addr;
    QLineEdit *user, *passwd;
    QLineEdit *pathOffset, *interval, *offset, *retryInterval;
};

#endif // FTPOPTDLG_H
