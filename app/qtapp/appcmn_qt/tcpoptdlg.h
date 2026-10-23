//------------------------------------------------------------------------------
// tcpoptdlg.h : TCP/NTRIP options dialog (Qt port of winapp/appcmn/tcpoptdlg)
//
// Opt: 0:TCP Server, 1:TCP Client, 2:NTRIP Server, 3:NTRIP Client,
//      4:NTRIP Caster, 5:NTRIP Caster Server, 6:UDP Server, 7:UDP Client
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/appcmn/tcpoptdlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef TCPOPTDLG_H
#define TCPOPTDLG_H

#include <QDialog>

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;

#define MAXHIST     10

class TcpOptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TcpOptDialog(QWidget *parent = nullptr);

    // public state mirrors the VCL class to keep caller-side porting mechanical
    int Opt = 0;
    QString Path;
    QString MntpStr;
    QString History[MAXHIST];

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOkClicked();
    void btnNtripClicked();
    void btnBrowsClicked();
    void btnMountpClicked();

private:
    void addHist(QComboBox *list, QString *hist);
    int  execCmd(const QString &cmd);

    QLabel      *labelAddr, *labelPort, *labelUser, *labelPasswd, *labelMntPnt;
    QComboBox   *addr, *mntPnt;
    QLineEdit   *port, *user, *passwd;
    QPushButton *btnOk, *btnCancel, *btnNtrip, *btnBrows, *btnMountp;
};

#endif // TCPOPTDLG_H
