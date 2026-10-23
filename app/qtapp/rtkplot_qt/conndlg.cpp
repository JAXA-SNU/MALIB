//------------------------------------------------------------------------------
// conndlg.cpp : real-time connection settings for RTKPLOT-Qt
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
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "appstyle.h"
#include "conndlg.h"
#include "serioptdlg.h"
#include "tcpoptdlg.h"

// stream types offered (index -> STR_*), same order as the VCL original
static const int STR_TBL[] = {
    STR_NONE, STR_SERIAL, STR_TCPCLI, STR_TCPSVR, STR_NTRIPCLI, STR_FILE
};
static const char *STR_LABEL[] = {
    "", "Serial", "TCP Client", "TCP Server", "NTRIP Client", "File"
};
static const char *SOLF_LABEL[] = {
    "Lat/Lon/Height", "X/Y/Z-ECEF", "E/N/U-Baseline", "NMEA-0183"
};

//------------------------------------------------------------------------------
ConnectDialog::ConnectDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Connection Settings");

    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel("Stream Type"),    0, 1);
    grid->addWidget(new QLabel("Opt"),            0, 2);
    grid->addWidget(new QLabel("Solution Format"),0, 3);

    for (int i = 0; i < 2; i++) {
        selStream[i] = new QComboBox();
        for (auto &l : STR_LABEL) selStream[i]->addItem(l);
        selStream[i]->setMinimumWidth(130);

        btnOpt[i] = new QPushButton("...");
        btnOpt[i]->setFixedWidth(28);

        solFormat[i] = new QComboBox();
        for (auto &l : SOLF_LABEL) solFormat[i]->addItem(l);
        solFormat[i]->setMinimumWidth(140);

        grid->addWidget(new QLabel(QString("(%1)").arg(i+1)), 1+i, 0);
        grid->addWidget(selStream[i],  1+i, 1);
        grid->addWidget(btnOpt[i],     1+i, 2);
        grid->addWidget(solFormat[i],  1+i, 3);

        connect(selStream[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ConnectDialog::updateEnable);
        connect(btnOpt[i], &QPushButton::clicked, this, [this, i] { btnOptClicked(i); });
    }

    timeFormS = new QComboBox();
    timeFormS->addItems({"ww ssss.ss GPST", "hh:mm:ss GPST", "hh:mm:ss UTC",
                         "hh:mm:ss JST"});
    degFormS = new QComboBox();
    degFormS->addItems({"ddd.dddddddd", "ddd mm ss.sss"});
    fieldSepE = new QLineEdit();
    fieldSepE->setMaximumWidth(80);

    auto *fmt = new QGridLayout();
    fmt->addWidget(new QLabel("Time Format"),   0, 0);
    fmt->addWidget(new QLabel("Lat/Lon Format"),0, 1);
    fmt->addWidget(new QLabel("Field Sep"),     0, 2);
    fmt->addWidget(timeFormS, 1, 0);
    fmt->addWidget(degFormS,  1, 1);
    fmt->addWidget(fieldSepE, 1, 2);

    timeOutE = new QLineEdit();
    reConnE  = new QLineEdit();
    timeOutE->setMaximumWidth(90);
    reConnE->setMaximumWidth(90);
    auto *tm = new QHBoxLayout();
    tm->addWidget(new QLabel("Timeout / Re-connect Intvl (ms)"));
    tm->addWidget(timeOutE);
    tm->addWidget(reConnE);
    tm->addStretch();

    auto *btnSet     = new QPushButton("&Set");
    auto *btnConnect = new QPushButton("&Connect");
    auto *btnCancel  = new QPushButton("Cancel");
    btnConnect->setDefault(true);
    auto *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(btnSet);
    btns->addWidget(btnConnect);
    btns->addWidget(btnCancel);
    connect(btnSet,     &QPushButton::clicked, this, [this] { accepted(false); });
    connect(btnConnect, &QPushButton::clicked, this, [this] { accepted(true);  });
    connect(btnCancel,  &QPushButton::clicked, this, &QDialog::reject);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addLayout(fmt);
    vbox->addLayout(tm);
    vbox->addLayout(btns);
}
//------------------------------------------------------------------------------
void ConnectDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;

    for (int i = 0; i < 2; i++) {
        for (size_t k = 0; k < sizeof(STR_TBL)/sizeof(*STR_TBL); k++) {
            if (STR_TBL[k] == stream[i]) selStream[i]->setCurrentIndex((int)k);
        }
        if (format[i] >= 0 && format[i] < 4) solFormat[i]->setCurrentIndex(format[i]);
    }
    timeFormS->setCurrentIndex(timeForm);
    degFormS->setCurrentIndex(degForm);
    fieldSepE->setText(fieldSep);
    timeOutE->setText(QString::number(timeOutTime));
    reConnE->setText(QString::number(reConnTime));
    updateEnable();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
void ConnectDialog::accepted(bool doConnect)
{
    for (int i = 0; i < 2; i++) {
        stream[i] = STR_TBL[selStream[i]->currentIndex()];
        format[i] = solFormat[i]->currentIndex();
    }
    timeForm = timeFormS->currentIndex();
    degForm  = degFormS->currentIndex();
    fieldSep = fieldSepE->text();
    timeOutTime = timeOutE->text().toInt();
    reConnTime  = reConnE->text().toInt();
    connectNow = doConnect;
    accept();
}
//------------------------------------------------------------------------------
void ConnectDialog::btnOptClicked(int i)
{
    switch (selStream[i]->currentIndex()) {
    case 1: {                                   // serial
        SerialOptDialog dlg(this);
        dlg.Path = paths[i][0];
        if (dlg.exec() != QDialog::Accepted) return;
        paths[i][0] = dlg.Path;
        break;
    }
    case 2: case 3: case 4: {                   // tcp client / server / ntrip
        TcpOptDialog dlg(this);
        dlg.Path = paths[i][1];
        dlg.Opt = selStream[i]->currentIndex() == 2 ? 1 :
                  selStream[i]->currentIndex() == 3 ? 0 : 3;
        for (int j = 0; j < 10; j++) dlg.History[j] = tcpHistory[j];
        if (dlg.exec() != QDialog::Accepted) return;
        paths[i][1] = dlg.Path;
        for (int j = 0; j < 10; j++) tcpHistory[j] = dlg.History[j];
        break;
    }
    case 5: {                                   // file
        QString f = QFileDialog::getOpenFileName(this, "Solution File", paths[i][2]);
        if (f.isEmpty()) return;
        paths[i][2] = f;
        break;
    }
    default: break;
    }
}
//------------------------------------------------------------------------------
void ConnectDialog::updateEnable()
{
    for (int i = 0; i < 2; i++) {
        int k = selStream[i]->currentIndex();
        btnOpt[i]->setEnabled(k > 0);
        solFormat[i]->setEnabled(k > 0);
    }
}
//------------------------------------------------------------------------------
