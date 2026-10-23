//------------------------------------------------------------------------------
// serioptdlg.cpp : serial options dialog (Qt port of winapp/appcmn/serioptdlg)
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
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>

#include "appstyle.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "serioptdlg.h"

#define MAXCOMNO    299                 // maximum com number

//------------------------------------------------------------------------------
SerialOptDialog::SerialOptDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Serial Options");

    port = new QComboBox();
    port->setEditable(true);

    bitRate = new QComboBox();
    bitRate->setEditable(true);
    for (const char *b : {"300","600","1200","2400","4800","9600","19200",
                          "38400","57600","115200","230400","460800","921600"}) {
        bitRate->addItem(b);
    }
    bitRate->setCurrentText("115200");

    byteSize = new QComboBox();
    byteSize->addItems({"7 bits", "8 bits"});
    byteSize->setCurrentIndex(1);

    parity = new QComboBox();
    parity->addItems({"None", "Even", "Odd"});

    stopBits = new QComboBox();
    stopBits->addItems({"1 bit", "2 bits"});

    flowCtr = new QComboBox();
    flowCtr->addItems({"None", "X on/off", "RTS/CTS"});

    outTcpPort = new QCheckBox("Output Received Stream to TCP Port");
    tcpPort = new QLineEdit();
    tcpPort->setMaximumWidth(80);

    auto *btnOk = new QPushButton("&OK");
    auto *btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);

    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel("Port"),          0, 0);
    grid->addWidget(new QLabel("Bitrate (bps)"), 0, 1);
    grid->addWidget(new QLabel("Byte Size"),     0, 2);
    grid->addWidget(port,     1, 0);
    grid->addWidget(bitRate,  1, 1);
    grid->addWidget(byteSize, 1, 2);
    grid->addWidget(new QLabel("Parity"),    2, 0);
    grid->addWidget(new QLabel("Stop Bits"), 2, 1);
    grid->addWidget(new QLabel("Flow Control"), 2, 2);
    grid->addWidget(parity,   3, 0);
    grid->addWidget(stopBits, 3, 1);
    grid->addWidget(flowCtr,  3, 2);

    auto *tcp = new QHBoxLayout();
    tcp->addWidget(outTcpPort);
    tcp->addWidget(tcpPort);
    tcp->addStretch();

    auto *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addLayout(tcp);
    vbox->addLayout(btns);

    connect(btnOk,      &QPushButton::clicked, this, &SerialOptDialog::btnOkClicked);
    connect(btnCancel,  &QPushButton::clicked, this, &QDialog::reject);
    connect(outTcpPort, &QCheckBox::toggled,   this, &SerialOptDialog::updateEnable);
}
//------------------------------------------------------------------------------
void SerialOptDialog::updatePortList()
{
    port->clear();
#ifdef WIN32
    char path[64];
    for (int i = 1; i <= MAXCOMNO; i++) {
        sprintf(path, "\\\\.\\COM%d", i);
        HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, 0,
                               OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        port->addItem(path + 4);
        CloseHandle(h);
    }
#else
    QDir dev("/dev");
    for (const QString &pat : {"ttyUSB*", "ttyACM*", "ttyAMA*"}) {
        for (const QString &d : dev.entryList({pat}, QDir::System | QDir::AllEntries)) {
            port->addItem(d);
        }
    }
#endif
}
//------------------------------------------------------------------------------
void SerialOptDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;

    updatePortList();

    // decode port:bitrate:bytesize:parity:stopbits:fctr[#tcpport]
    QStringList v = Path.split(':');
    if (v.size() >= 1) port->setCurrentText(v[0]);
    if (v.size() >= 2) bitRate->setCurrentText(v[1]);
    if (v.size() >= 3) byteSize->setCurrentIndex(v[2] == "7" ? 0 : 1);
    if (v.size() >= 4) parity->setCurrentIndex(v[3] == "n" ? 0 : (v[3] == "e" ? 1 : 2));
    if (v.size() >= 5) stopBits->setCurrentIndex(v[4] == "1" ? 0 : 1);
    if (v.size() >= 6) {
        QString f = v[5];
        int hash = f.indexOf('#');
        QString fc = hash >= 0 ? f.left(hash) : f;
        flowCtr->setCurrentIndex(fc == "off" ? 0 : (fc == "xon" ? 1 : 2));
        if (hash >= 0) {
            outTcpPort->setChecked(true);
            tcpPort->setText(f.mid(hash + 1));
        }
        else {
            outTcpPort->setChecked(false);
            tcpPort->clear();
        }
    }
    outTcpPort->setEnabled(Opt);
    tcpPort->setEnabled(Opt);
    updateEnable();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
void SerialOptDialog::btnOkClicked()
{
    static const char *par[] = {"n", "e", "o"}, *fctr[] = {"off", "xon", "rts"};
    Path = QString("%1:%2:%3:%4:%5:%6")
               .arg(port->currentText(), bitRate->currentText())
               .arg(byteSize->currentIndex() ? 8 : 7)
               .arg(par[parity->currentIndex()])
               .arg(stopBits->currentIndex() ? 2 : 1)
               .arg(fctr[flowCtr->currentIndex()]);
    if (outTcpPort->isChecked() && !tcpPort->text().isEmpty()) {
        Path += "#" + tcpPort->text();
    }
    accept();
}
//------------------------------------------------------------------------------
void SerialOptDialog::updateEnable()
{
    tcpPort->setEnabled(outTcpPort->isChecked() && Opt);
}
//------------------------------------------------------------------------------
