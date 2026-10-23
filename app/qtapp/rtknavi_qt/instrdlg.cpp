//------------------------------------------------------------------------------
// instrdlg.cpp : input streams dialog (redesigned for MALIB 1.3.0)
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
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "appstyle.h"

#include "instrdlg.h"
#include "cmdoptdlg.h"
#include "ftpoptdlg.h"
#include "serioptdlg.h"
#include "tcpoptdlg.h"

// stream types selectable for input (value = STR_*; same numbers as ISTOPT)
static const struct { const char *label; int type; } STR_TYPES[] = {
    {"Serial",      STR_SERIAL},
    {"TCP Client",  STR_TCPCLI},
    {"TCP Server",  STR_TCPSVR},
    {"NTRIP Client",STR_NTRIPCLI},
    {"UDP Server",  STR_UDPSVR},
    {"UDP Client",  STR_UDPCLI},
    {"File",        STR_FILE},
    {"FTP",         STR_FTP},
    {"HTTP",        STR_HTTP},
};
// stream formats selectable for input (value = STRFMT_*)
static const int STR_FMTS[] = {
    STRFMT_RTCM2,STRFMT_RTCM3,STRFMT_OEM4,STRFMT_OEM3,STRFMT_UBX,STRFMT_SS2,
    STRFMT_CRES,STRFMT_STQ,STRFMT_JAVAD,STRFMT_NVS,STRFMT_BINEX,STRFMT_RT17,
    STRFMT_SEPT,STRFMT_SP3,STRFMT_L6,STRFMT_STAT
};

//------------------------------------------------------------------------------
InputStrDialog::InputStrDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Input Streams");

    static const char *rowLabel[NSTRIN] = {
        "(1) Rover", "(2) Correction 1", "(3) Correction 2", "(4) Correction 3"
    };
    static const char *rowTip[NSTRIN] = {
        "GNSS receiver data",
        "correction data (e.g. RTK/DGNSS/local augmentation stat)",
        "correction data (e.g. main L6E)",
        "correction data (e.g. sub L6E)"
    };

    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel("Input Stream"), 0, 0);
    grid->addWidget(new QLabel("Type"),   0, 1);
    grid->addWidget(new QLabel("Opt"),    0, 2);
    grid->addWidget(new QLabel("Cmd"),    0, 3);
    grid->addWidget(new QLabel("Format"), 0, 4);
    grid->addWidget(new QLabel("SSR"),    0, 5);
    grid->addWidget(new QLabel("Opt"),    0, 6);

    for (int i = 0; i < NSTRIN; i++) {
        streamC[i] = new QCheckBox(rowLabel[i]);
        streamC[i]->setToolTip(rowTip[i]);
        stream[i] = new QComboBox();
        for (auto &t : STR_TYPES) stream[i]->addItem(t.label, t.type);
        stream[i]->setMinimumWidth(110);
        btnStr[i] = new QPushButton("...");
        btnStr[i]->setFixedWidth(28);
        btnCmd[i] = new QPushButton("...");
        btnCmd[i]->setFixedWidth(28);
        btnCmd[i]->setToolTip("receiver command file");
        format[i] = new QComboBox();
        format[i]->setMinimumWidth(130);
        for (int f : STR_FMTS) {
            format[i]->addItem(f == STRFMT_SP3  ? "SP3" :
                               f == STRFMT_L6   ? "L6"  :
                               f == STRFMT_STAT ? "STAT" : formatstrs[f], f);
        }
        ssr[i] = new QComboBox();
        ssr[i]->setMinimumWidth(72);
        ssr[i]->addItem("OFF",  STRSSR_OFF);
        ssr[i]->addItem("MAIN", STRSSR_MAIN);
        ssr[i]->addItem("SUB",  STRSSR_SUB);
        btnRcvOpt[i] = new QPushButton("...");
        btnRcvOpt[i]->setFixedWidth(28);
        btnRcvOpt[i]->setToolTip("receiver option");

        grid->addWidget(streamC[i],   1 + i, 0);
        grid->addWidget(stream[i],    1 + i, 1);
        grid->addWidget(btnStr[i],    1 + i, 2);
        grid->addWidget(btnCmd[i],    1 + i, 3);
        grid->addWidget(format[i],    1 + i, 4);
        grid->addWidget(ssr[i],       1 + i, 5);
        grid->addWidget(btnRcvOpt[i], 1 + i, 6);

        connect(streamC[i], &QCheckBox::toggled, this, &InputStrDialog::updateEnable);
        connect(stream[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &InputStrDialog::updateEnable);
        connect(btnStr[i],    &QPushButton::clicked, this, [this, i] { btnStrClicked(i); });
        connect(btnCmd[i],    &QPushButton::clicked, this, [this, i] { btnCmdClicked(i); });
        connect(btnRcvOpt[i], &QPushButton::clicked, this, [this, i] { btnRcvOptClicked(i); });
    }

    // NMEA request (inpstr2) ---------------------------------------------------
    labelNmea = new QLabel("Transmit NMEA GGA to Base Station");
    labelNmea->setStyleSheet("color:gray;");
    nmeaReqL = new QComboBox();
    nmeaReqL->addItems({"OFF", "Latitude/Longitude", "Single Solution"});
    for (int i = 0; i < 3; i++) nmeaPosE[i] = new QLineEdit("0");
    auto *nmea = new QHBoxLayout();
    nmea->addWidget(nmeaReqL);
    for (int i = 0; i < 3; i++) nmea->addWidget(nmeaPosE[i]);
    connect(nmeaReqL, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InputStrDialog::updateEnable);

    // file paths ------------------------------------------------------------------
    labelFile = new QLabel("Input File Paths");
    labelFile->setStyleSheet("color:gray;");
    auto *files = new QGridLayout();
    for (int i = 0; i < NSTRIN; i++) {
        filePath[i] = new QLineEdit();
        btnFile[i] = new QPushButton("...");
        btnFile[i]->setFixedWidth(28);
        files->addWidget(filePath[i], i, 0);
        files->addWidget(btnFile[i],  i, 1);
        connect(btnFile[i], &QPushButton::clicked, this, [this, i] { btnFileClicked(i); });
    }
    timeTagC = new QCheckBox("Time");
    timeSpeedL = new QComboBox();
    timeSpeedL->addItems({"x1", "x2", "x5", "x10", "x20", "x50", "x100"});
    timeStartE = new QLineEdit("0");
    timeStartE->setMaximumWidth(60);
    chk64Bit = new QCheckBox("64bit");
    auto *timeRow = new QHBoxLayout();
    timeRow->addWidget(timeTagC);
    timeRow->addWidget(timeSpeedL);
    timeRow->addWidget(new QLabel("+"));
    timeRow->addWidget(timeStartE);
    timeRow->addWidget(new QLabel("s"));
    timeRow->addWidget(chk64Bit);
    timeRow->addStretch();
    connect(timeTagC, &QCheckBox::toggled, this, &InputStrDialog::updateEnable);

    auto *btnOk = new QPushButton("&OK");
    auto *btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);
    auto *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);
    connect(btnOk,     &QPushButton::clicked, this, &InputStrDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addWidget(labelNmea);
    vbox->addLayout(nmea);
    vbox->addWidget(labelFile);
    vbox->addLayout(files);
    vbox->addLayout(timeRow);
    vbox->addLayout(btns);
}
//------------------------------------------------------------------------------
int InputStrDialog::kindOf(int type) const
{
    switch (type) {
        case STR_SERIAL:   return 0;
        case STR_FILE:     return 2;
        case STR_FTP:
        case STR_HTTP:     return 3;
        default:           return 1;    // tcp/ntrip/udp
    }
}
//------------------------------------------------------------------------------
void InputStrDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;

    bool tag = false, bit64 = false;
    QString speed = "x1", start = "0";

    for (int i = 0; i < NSTRIN; i++) {
        int type = strType[i];
        streamC[i]->setChecked(type != STR_NONE);
        if (type != STR_NONE) {
            int idx = stream[i]->findData(type);
            if (idx >= 0) stream[i]->setCurrentIndex(idx);
        }
        int fdx = format[i]->findData(strFmt[i]);
        if (fdx >= 0) format[i]->setCurrentIndex(fdx);
        int sdx = ssr[i]->findData(strSsr[i]);
        if (sdx >= 0) ssr[i]->setCurrentIndex(sdx);

        // decode path into per-kind slot; file tags into shared controls
        for (int k = 0; k < 4; k++) paths[i][k] = "";
        QString p = strPath[i];
        int kind = kindOf(type == STR_NONE ? stream[i]->currentData().toInt() : type);
        if (kind == 2) {
            QString base = p.section("::", 0, 0);
            paths[i][2] = base;
            filePath[i]->setText(base);
            for (const QString &t : p.mid(base.length()).split("::", Qt::SkipEmptyParts)) {
                if      (t == "T")            tag = true;
                else if (t == "P=8")          bit64 = true;
                else if (t.startsWith('x'))   speed = t;
                else if (t.startsWith('+'))   start = t.mid(1);
            }
        }
        else {
            paths[i][kind] = p;
            filePath[i]->setText("");
        }
    }
    timeTagC->setChecked(tag);
    timeSpeedL->setCurrentText(speed);
    timeStartE->setText(start);
    chk64Bit->setChecked(bit64);

    nmeaReqL->setCurrentIndex(nmeaReq);
    nmeaPosE[0]->setText(QString::asprintf("%.9f", nmeaPos[0]));
    nmeaPosE[1]->setText(QString::asprintf("%.9f", nmeaPos[1]));
    nmeaPosE[2]->setText(QString::asprintf("%.3f", nmeaPos[2]));

    updateEnable();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
QString InputStrDialog::filePathWithTags(const QString &base) const
{
    QString path = base;
    if (timeTagC->isChecked())      path += "::T";
    if (timeStartE->text() != "0" && !timeStartE->text().isEmpty())
                                    path += "::+" + timeStartE->text();
    path += "::" + timeSpeedL->currentText();
    if (chk64Bit->isChecked())      path += "::P=8";
    return path;
}
//------------------------------------------------------------------------------
void InputStrDialog::btnOkClicked()
{
    for (int i = 0; i < NSTRIN; i++) {
        int type = stream[i]->currentData().toInt();
        strType[i] = streamC[i]->isChecked() ? type : STR_NONE;
        int kind = kindOf(type);
        if (kind == 2) {
            paths[i][2] = filePath[i]->text();
            strPath[i] = filePath[i]->text().isEmpty()
                             ? QString() : filePathWithTags(filePath[i]->text());
        }
        else {
            strPath[i] = paths[i][kind];
        }
        strFmt[i] = format[i]->currentData().toInt();
        strSsr[i] = ssr[i]->currentData().toInt();
    }
    nmeaReq = nmeaReqL->currentIndex();
    for (int i = 0; i < 3; i++) nmeaPos[i] = nmeaPosE[i]->text().toDouble();
    accept();
}
//------------------------------------------------------------------------------
void InputStrDialog::btnStrClicked(int i)
{
    int type = stream[i]->currentData().toInt();
    switch (type) {
    case STR_SERIAL: {
        SerialOptDialog dlg(this);
        dlg.Path = paths[i][0];
        dlg.Opt = 0;
        if (dlg.exec() != QDialog::Accepted) return;
        paths[i][0] = dlg.Path;
        break;
    }
    case STR_TCPSVR:
    case STR_TCPCLI:
    case STR_NTRIPCLI:
    case STR_UDPSVR:
    case STR_UDPCLI: {
        TcpOptDialog dlg(this);
        dlg.Path = paths[i][1];
        dlg.Opt = type == STR_TCPSVR || type == STR_UDPSVR ? 0 :
                  type == STR_NTRIPCLI ? 3 : 1;
        for (int j = 0; j < 10; j++) dlg.History[j] = history[j];
        if (dlg.exec() != QDialog::Accepted) return;
        paths[i][1] = dlg.Path;
        for (int j = 0; j < 10; j++) history[j] = dlg.History[j];
        break;
    }
    case STR_FTP:
    case STR_HTTP: {
        FtpOptDialog dlg(this);
        dlg.Path = paths[i][3];
        dlg.Opt = (type == STR_HTTP) ? 1 : 0;
        for (int j = 0; j < 10; j++) dlg.History[j] = history[j];
        if (dlg.exec() != QDialog::Accepted) return;
        paths[i][3] = dlg.Path;
        for (int j = 0; j < 10; j++) history[j] = dlg.History[j];
        break;
    }
    default: break;
    }
}
//------------------------------------------------------------------------------
// receiver commands: edited in place, optionally saved to the command file
// that rtkrcv/rtknavi reads at startup (file-cmdfileN)
void InputStrDialog::btnCmdClicked(int i)
{
    CmdOptDialog dlg(this);
    dlg.CmdFile = cmdFile[i];
    for (int j = 0; j < 3; j++) {
        dlg.Cmds[j]   = cmds[i][j];
        dlg.CmdEna[j] = cmdEna[i][j];
    }
    if (dlg.exec() != QDialog::Accepted) return;
    for (int j = 0; j < 3; j++) {
        cmds[i][j]   = dlg.Cmds[j];
        cmdEna[i][j] = dlg.CmdEna[j];
    }
    cmdFile[i] = dlg.CmdFile;
}
//------------------------------------------------------------------------------
void InputStrDialog::btnRcvOptClicked(int i)
{
    bool ok;
    QString opt = QInputDialog::getText(this, "Receiver Option",
        "receiver dependent options", QLineEdit::Normal, rcvOpt[i], &ok);
    if (!ok) return;
    rcvOpt[i] = opt;
}
//------------------------------------------------------------------------------
void InputStrDialog::updateEnable()
{
    bool enaFile = false;
    for (int i = 0; i < NSTRIN; i++) {
        bool on = streamC[i]->isChecked();
        int type = stream[i]->currentData().toInt();
        stream[i]->setEnabled(on);
        btnStr[i]->setEnabled(on && type != STR_FILE);
        btnCmd[i]->setEnabled(on && (type == STR_SERIAL || type == STR_TCPCLI ||
                                     type == STR_TCPSVR || type == STR_NTRIPCLI));
        format[i]->setEnabled(on);
        ssr[i]->setEnabled(on);
        btnRcvOpt[i]->setEnabled(on);
        bool isFile = on && type == STR_FILE;
        filePath[i]->setEnabled(isFile);
        btnFile[i]->setEnabled(isFile);
        if (isFile) enaFile = true;
    }
    bool ena2 = streamC[1]->isChecked() &&
                stream[1]->currentData().toInt() != STR_FILE;
    labelNmea->setEnabled(ena2);
    nmeaReqL->setEnabled(ena2);
    for (int i = 0; i < 3; i++) {
        nmeaPosE[i]->setEnabled(ena2 && nmeaReqL->currentIndex() == 1);
    }
    labelFile->setEnabled(enaFile);
    timeTagC->setEnabled(enaFile);
    timeSpeedL->setEnabled(enaFile && timeTagC->isChecked());
    timeStartE->setEnabled(enaFile && timeTagC->isChecked());
    chk64Bit->setEnabled(enaFile && timeTagC->isChecked());
}
//------------------------------------------------------------------------------
void InputStrDialog::btnFileClicked(int i)
{
    QString file = QFileDialog::getOpenFileName(
        this, "Input File Path", filePath[i]->text());
    if (file.isEmpty()) return;
    filePath[i]->setText(file);
}
//------------------------------------------------------------------------------
