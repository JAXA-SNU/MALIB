//------------------------------------------------------------------------------
// instrdlg.h : input streams dialog (redesigned for MALIB 1.3.0)
//
// 4 input streams + SSR type (main/sub/off) per stream. Edits the rtkrcv
// conf model directly: strtype (STR_*), strpath, strfmt (STRFMT_*), strssr,
// command file per stream, NMEA request.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/instrdlg (redesigned for 1.3.0).
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef INSTRDLG_H
#define INSTRDLG_H

#include <QDialog>

#include "rtklib.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;

class InputStrDialog : public QDialog
{
    Q_OBJECT
public:
    explicit InputStrDialog(QWidget *parent = nullptr);

    // in/out: caller sets before exec(), reads back after Accepted
    int     strType[NSTRIN] = {STR_NONE, STR_NONE, STR_NONE, STR_NONE};
    QString strPath[NSTRIN];
    int     strFmt[NSTRIN]  = {STRFMT_UBX, STRFMT_RTCM3, STRFMT_RTCM3, STRFMT_L6};
    int     strSsr[NSTRIN]  = {STRSSR_OFF, STRSSR_OFF, STRSSR_MAIN, STRSSR_OFF};
    QString cmdFile[NSTRIN];
    QString cmds[NSTRIN][3];        // open/close/periodic (in/out)
    int     cmdEna[NSTRIN][3] = {};
    int     nmeaReq = 0;
    double  nmeaPos[3] = {0, 0, 0};
    QString history[10];                // NTRIP address history

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnOkClicked();
    void updateEnable();

private:
    void btnStrClicked(int i);
    void btnCmdClicked(int i);
    void btnRcvOptClicked(int i);
    void btnFileClicked(int i);
    int  kindOf(int type) const;        // 0:serial 1:tcp-like 2:file 3:ftp/http
    QString filePathWithTags(const QString &base) const;

    QCheckBox   *streamC[NSTRIN];
    QComboBox   *stream[NSTRIN], *format[NSTRIN], *ssr[NSTRIN];
    QPushButton *btnStr[NSTRIN], *btnCmd[NSTRIN], *btnRcvOpt[NSTRIN];
    QLineEdit   *filePath[NSTRIN];
    QPushButton *btnFile[NSTRIN];
    QComboBox   *nmeaReqL;
    QLineEdit   *nmeaPosE[3];
    QCheckBox   *timeTagC, *chk64Bit;
    QComboBox   *timeSpeedL;
    QLineEdit   *timeStartE;
    QLabel      *labelNmea, *labelFile;

    QString paths[NSTRIN][4];           // per-kind path memory
    QString rcvOpt[NSTRIN];
};

#endif // INSTRDLG_H
