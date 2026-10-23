//------------------------------------------------------------------------------
// serioptdlg.h : serial options dialog (Qt port of winapp/appcmn/serioptdlg)
//
// Path format: port:bitrate:bytesize:parity:stopbits:fctr[#tcpport]
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/appcmn/serioptdlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef SERIOPTDLG_H
#define SERIOPTDLG_H

#include <QDialog>

class QComboBox;
class QLineEdit;
class QCheckBox;

class SerialOptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SerialOptDialog(QWidget *parent = nullptr);

    int Opt = 0;                        // 1: enable output tcp port
    QString Path;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOkClicked();
    void updateEnable();

private:
    void updatePortList();

    QComboBox *port, *byteSize, *parity, *stopBits, *flowCtr, *bitRate;
    QCheckBox *outTcpPort;
    QLineEdit *tcpPort;
};

#endif // SERIOPTDLG_H
