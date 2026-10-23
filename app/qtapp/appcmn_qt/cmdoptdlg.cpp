//------------------------------------------------------------------------------
// cmdoptdlg.cpp : receiver command dialog
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
#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTextStream>
#include <QVBoxLayout>

#include "appstyle.h"
#include "cmdoptdlg.h"

static const char *CMD_LABEL[3] = {
    "Commands at Startup", "Commands at Shutdown", "Periodic Commands"
};

//------------------------------------------------------------------------------
CmdOptDialog::CmdOptDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Receiver Commands");

    auto *grid = new QGridLayout();
    for (int i = 0; i < 3; i++) {
        chk[i] = new QCheckBox(CMD_LABEL[i]);
        edit[i] = new QPlainTextEdit();
        edit[i]->setFont(QFont("monospace"));
        edit[i]->setMinimumHeight(90);
        grid->addWidget(chk[i], i * 2, 0);
        grid->addWidget(edit[i], i * 2 + 1, 0);
        connect(chk[i], &QCheckBox::toggled, this, &CmdOptDialog::updateEnable);
    }

    auto *btnLoad = new QPushButton("&Load...");
    auto *btnSave = new QPushButton("&Save...");
    auto *btnOk = new QPushButton("&OK");
    auto *btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);

    auto *btns = new QHBoxLayout();
    btns->addWidget(btnLoad);
    btns->addWidget(btnSave);
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addLayout(btns);

    connect(btnLoad,   &QPushButton::clicked, this, &CmdOptDialog::btnLoadClicked);
    connect(btnSave,   &QPushButton::clicked, this, &CmdOptDialog::btnSaveClicked);
    connect(btnOk,     &QPushButton::clicked, this, &CmdOptDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    resize(520, 460);
}
//------------------------------------------------------------------------------
void CmdOptDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;
    for (int i = 0; i < 3; i++) {
        edit[i]->setPlainText(Cmds[i]);
        chk[i]->setChecked(CmdEna[i]);
    }
    updateEnable();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
void CmdOptDialog::btnOkClicked()
{
    for (int i = 0; i < 3; i++) {
        Cmds[i] = edit[i]->toPlainText();
        CmdEna[i] = chk[i]->isChecked() ? 1 : 0;
    }
    accept();
}
//------------------------------------------------------------------------------
// load a command file: blocks separated by a line starting with '@'
void CmdOptDialog::btnLoadClicked()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Load Commands", CmdFile, "Command (*.cmd);;All (*)");
    if (file.isEmpty()) return;

    QFile fp(file);
    if (!fp.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QStringList block[3];
    int n = 0;
    QTextStream in(&fp);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith('@')) { if (++n >= 3) break; continue; }
        block[n] << line;
    }
    for (int i = 0; i < 3; i++) edit[i]->setPlainText(block[i].join("\n"));
    CmdFile = file;
}
//------------------------------------------------------------------------------
void CmdOptDialog::btnSaveClicked()
{
    QString file = QFileDialog::getSaveFileName(
        this, "Save Commands", CmdFile, "Command (*.cmd);;All (*)");
    if (file.isEmpty()) return;
    if (!file.contains('.')) file += ".cmd";

    QFile fp(file);
    if (!fp.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream os(&fp);
    os << edit[0]->toPlainText() << "\n@\n"
       << edit[1]->toPlainText() << "\n@\n"
       << edit[2]->toPlainText() << "\n";
    CmdFile = file;
}
//------------------------------------------------------------------------------
void CmdOptDialog::updateEnable()
{
    for (int i = 0; i < 3; i++) edit[i]->setEnabled(chk[i]->isChecked());
}
//------------------------------------------------------------------------------
