//------------------------------------------------------------------------------
// ftpoptdlg.cpp : FTP/HTTP download options
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
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "appstyle.h"
#include "ftpoptdlg.h"

//------------------------------------------------------------------------------
FtpOptDialog::FtpOptDialog(QWidget *parent) : QDialog(parent)
{
    addr = new QComboBox();
    addr->setEditable(true);
    addr->setInsertPolicy(QComboBox::NoInsert);
    addr->setMinimumWidth(300);
    addr->setToolTip("address/path  (keywords: %Y %m %d %h %M %n %W %D)");

    user   = new QLineEdit();
    passwd = new QLineEdit();
    passwd->setEchoMode(QLineEdit::Password);

    pathOffset    = new QLineEdit("0");
    interval      = new QLineEdit("1");
    offset        = new QLineEdit("0");
    retryInterval = new QLineEdit("0");
    for (QLineEdit *e : {pathOffset, interval, offset, retryInterval}) {
        e->setMaximumWidth(80);
    }

    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel("Address / Path"), 0, 0, 1, 3);
    grid->addWidget(addr, 1, 0, 1, 3);
    grid->addWidget(new QLabel("User ID"),  2, 0);
    grid->addWidget(new QLabel("Password"), 2, 1);
    grid->addWidget(user,   3, 0);
    grid->addWidget(passwd, 3, 1);

    grid->addWidget(new QLabel("Path Offset (h)"),    4, 0);
    grid->addWidget(new QLabel("Interval (h)"),       4, 1);
    grid->addWidget(new QLabel("Offset (h)"),         4, 2);
    grid->addWidget(pathOffset, 5, 0);
    grid->addWidget(interval,   5, 1);
    grid->addWidget(offset,     5, 2);
    grid->addWidget(new QLabel("Retry Interval (s, 0: no retry)"), 6, 0, 1, 2);
    grid->addWidget(retryInterval, 7, 0);

    auto *btnOk = new QPushButton("&OK");
    auto *btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);
    auto *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addLayout(btns);

    connect(btnOk,     &QPushButton::clicked, this, &FtpOptDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}
//------------------------------------------------------------------------------
void FtpOptDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;
    setWindowTitle(Opt ? "HTTP Option" : "FTP Option");

    // decode [user[:passwd]@]addr/path[::T=poff,tint,toff,tret]
    QString p = Path, tpart;
    int t = p.indexOf("::T=");
    if (t >= 0) { tpart = p.mid(t + 4); p = p.left(t); }

    QString cred, host = p;
    int at = p.lastIndexOf('@');
    if (at >= 0) { cred = p.left(at); host = p.mid(at + 1); }
    addr->setCurrentText(host);
    user->setText(cred.section(':', 0, 0));
    passwd->setText(cred.section(':', 1));

    int topts[4] = {0, 3600, 0, 0};
    QStringList tv = tpart.split(',');
    for (int i = 0; i < tv.size() && i < 4; i++) topts[i] = tv[i].toInt();
    pathOffset   ->setText(QString::number(topts[0] / 3600.0, 'g', 3));
    interval     ->setText(QString::number(topts[1] / 3600.0, 'g', 3));
    offset       ->setText(QString::number(topts[2] / 3600.0, 'g', 3));
    retryInterval->setText(QString::number(topts[3]));

    QString cur = addr->currentText();
    addr->clear();
    for (int i = 0; i < MAXHIST; i++) {
        if (!History[i].isEmpty()) addr->addItem(History[i]);
    }
    addr->setCurrentText(cur);
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
void FtpOptDialog::btnOkClicked()
{
    int topts[4];
    topts[0] = (int)(pathOffset->text().toDouble() * 3600.0);
    topts[1] = (int)(interval->text().toDouble() * 3600.0);
    topts[2] = (int)(offset->text().toDouble() * 3600.0);
    topts[3] = retryInterval->text().toInt();

    Path = QString("%1:%2@%3::T=%4,%5,%6,%7")
               .arg(user->text(), passwd->text(), addr->currentText())
               .arg(topts[0]).arg(topts[1]).arg(topts[2]).arg(topts[3]);
    addHist();
    accept();
}
//------------------------------------------------------------------------------
void FtpOptDialog::addHist()
{
    QString cur = addr->currentText();
    for (int i = 0; i < MAXHIST; i++) {
        if (cur != History[i]) continue;
        for (int j = i + 1; j < MAXHIST; j++) History[j - 1] = History[j];
        History[MAXHIST - 1] = "";
    }
    for (int i = MAXHIST - 1; i > 0; i--) History[i] = History[i - 1];
    History[0] = cur;
}
//------------------------------------------------------------------------------
