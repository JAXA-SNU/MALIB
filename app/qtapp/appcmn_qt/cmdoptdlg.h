//------------------------------------------------------------------------------
// cmdoptdlg.h : receiver command dialog (Qt port of winapp/appcmn/cmdoptdlg)
//
// Edits the open / close / periodic command blocks sent to a receiver.
// The on-disk format separates the three blocks with a line starting with '@'
// (same as rtkrcv readcmd()).
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/appcmn/cmdoptdlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef CMDOPTDLG_H
#define CMDOPTDLG_H

#include <QDialog>

class QCheckBox;
class QPlainTextEdit;

class CmdOptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CmdOptDialog(QWidget *parent = nullptr);

    QString Cmds[3];                    // open / close / periodic
    int     CmdEna[3] = {1, 1, 0};
    QString CmdFile;                    // last loaded/saved command file

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOkClicked();
    void btnLoadClicked();
    void btnSaveClicked();
    void updateEnable();

private:
    QPlainTextEdit *edit[3];
    QCheckBox      *chk[3];
};

#endif // CMDOPTDLG_H
