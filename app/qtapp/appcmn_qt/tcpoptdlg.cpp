//------------------------------------------------------------------------------
// tcpoptdlg.cpp : TCP/NTRIP options dialog (Qt port of winapp/appcmn/tcpoptdlg)
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
#include <QApplication>
#include <QComboBox>
#include <QSizePolicy>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QShowEvent>

#include "appstyle.h"

#include "rtklib.h"
#include "tcpoptdlg.h"

#define NTRIP_TIMEOUT   5000                // response timeout (ms)
#define NTRIP_CYCLE     50                  // processing cycle (ms)
#define MAXSRCTBL       512000              // max source table size (bytes)
#define ENDSRCTBL       "ENDSOURCETABLE"    // end marker of table
#define MAXLINE         1024                // max line size (byte)

static char srctbl[MAXSRCTBL];

//------------------------------------------------------------------------------
TcpOptDialog::TcpOptDialog(QWidget *parent) : QDialog(parent)
{
    labelAddr   = new QLabel("TCP Server Address");
    labelPort   = new QLabel("Port");
    labelMntPnt = new QLabel("Mountpoint");
    labelUser   = new QLabel("User ID");
    labelPasswd = new QLabel("Password");

    addr = new QComboBox();
    addr->setEditable(true);
    addr->setInsertPolicy(QComboBox::NoInsert);
    addr->setMaxVisibleItems(16);
    addr->setMinimumWidth(240);

    port = new QLineEdit();
    port->setMinimumWidth(90);

    mntPnt = new QComboBox();
    mntPnt->setEditable(true);
    mntPnt->setInsertPolicy(QComboBox::NoInsert);
    mntPnt->setMaxVisibleItems(16);
    // mountpoint names can be long (e.g. APRSAF_rov) - give it the widest cell
    mntPnt->setMinimumWidth(170);
    mntPnt->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    user   = new QLineEdit();
    passwd = new QLineEdit();
    passwd->setEchoMode(QLineEdit::Password);
    user->setMaximumWidth(110);
    passwd->setMaximumWidth(110);

    btnBrows  = new QPushButton("&Browse...");
    btnNtrip  = new QPushButton("&Get Mountp");
    btnMountp = new QPushButton("Mountp Options...");
    btnOk     = new QPushButton("&OK");
    btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);

    auto *grid = new QGridLayout();
    grid->addWidget(labelAddr,   0, 0, 1, 2);
    grid->addWidget(labelPort,   0, 2);
    grid->addWidget(addr,        1, 0, 1, 2);
    grid->addWidget(port,        1, 2);
    grid->addWidget(labelMntPnt, 2, 0);
    grid->addWidget(labelUser,   2, 1);
    grid->addWidget(labelPasswd, 2, 2);
    grid->addWidget(mntPnt,      3, 0);
    grid->addWidget(user,        3, 1);
    grid->addWidget(passwd,      3, 2);
    grid->setColumnStretch(0, 3);       // mountpoint / address
    grid->setColumnStretch(1, 1);       // user id
    grid->setColumnStretch(2, 1);       // password / port

    auto *btns = new QHBoxLayout();
    btns->addWidget(btnBrows);
    btns->addWidget(btnNtrip);
    btns->addWidget(btnMountp);
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addLayout(btns);

    connect(btnOk,     &QPushButton::clicked, this, &TcpOptDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnNtrip,  &QPushButton::clicked, this, &TcpOptDialog::btnNtripClicked);
    connect(btnBrows,  &QPushButton::clicked, this, &TcpOptDialog::btnBrowsClicked);
    connect(btnMountp, &QPushButton::clicked, this, &TcpOptDialog::btnMountpClicked);
}
//------------------------------------------------------------------------------
void TcpOptDialog::showEvent(QShowEvent *event)
{
    char buff[2048], *p, *q;
    char *addr_s = (char *)"", *port_s = (char *)"", *mntpnt = (char *)"";
    char *user_s = (char *)"", *passwd_s = (char *)"", *str = (char *)"";
    const char *ti[] = {"TCP Server Options", "TCP Client Options",
                        "NTRIP Server Options", "NTRIP Client Options",
                        "NTRIP Caster Options",
                        "NTRIP Caster Server Options", "UDP Server Options",
                        "UDP Client Options"};

    if (event->spontaneous()) return;

    // decode path: [user[:passwd]]@addr[:port][/mntpnt[:str]]  (same as VCL)
    strncpy(buff, qPrintable(Path), sizeof(buff) - 1);
    buff[sizeof(buff) - 1] = '\0';

    if (!(p = strchr(buff, '@'))) p = buff;

    if ((p = strchr(p, '/'))) {
        if ((q = strchr(p + 1, ':'))) {
            *q = '\0'; str = q + 1;
        }
        *p = '\0'; mntpnt = p + 1;
    }
    if ((p = strrchr(buff, '@'))) {
        *p++ = '\0';
        if ((q = strchr(buff, ':'))) {
            *q = '\0'; passwd_s = q + 1;
        }
        user_s = buff;
    }
    else p = buff;

    if ((q = strchr(p, ':'))) {
        *q = '\0'; port_s = q + 1;
    }
    addr_s = p;

    setWindowTitle(ti[Opt]);
    labelAddr  ->setText((Opt >= 2 && Opt <= 5) ? "NTRIP Caster Address" : "Server Address");
    labelAddr  ->setEnabled((Opt >= 1 && Opt <= 3) || Opt == 7);
    addr       ->setEnabled((Opt >= 1 && Opt <= 3) || Opt == 7);
    labelMntPnt->setEnabled(Opt >= 2 && Opt <= 4);
    mntPnt     ->setEnabled(Opt >= 2 && Opt <= 4);
    labelUser  ->setEnabled(Opt >= 3 && Opt <= 4);
    user       ->setEnabled(Opt >= 3 && Opt <= 4);
    labelPasswd->setEnabled(Opt >= 2 && Opt <= 5);
    passwd     ->setEnabled(Opt >= 2 && Opt <= 5);
    btnNtrip   ->setVisible(Opt == 3);
    btnBrows   ->setVisible(Opt == 3);
    btnMountp  ->setVisible(Opt == 2 || Opt == 4);

    addr  ->setCurrentText(addr_s);
    port  ->setText(port_s);
    mntPnt->setCurrentText(mntpnt);
    user  ->setText(user_s);
    passwd->setText(passwd_s);
    if (Opt == 2 || Opt == 4) {
        MntpStr = str;
    }
    addr->clear();
    for (int i = 0; i < MAXHIST; i++) {
        if (History[i] != "") addr->addItem(History[i]);
    }
    addr->setCurrentText(addr_s);

    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
void TcpOptDialog::btnOkClicked()
{
    Path = QString("%1:%2@%3:%4/%5:%6")
               .arg(user->text(), passwd->text(), addr->currentText(),
                    port->text(), mntPnt->currentText(), MntpStr);
    addHist(addr, History);
    accept();
}
//------------------------------------------------------------------------------
void TcpOptDialog::addHist(QComboBox *list, QString *hist)
{
    for (int i = 0; i < MAXHIST; i++) {
        if (list->currentText() != hist[i]) continue;
        for (int j = i + 1; j < MAXHIST; j++) hist[j - 1] = hist[j];
        hist[MAXHIST - 1] = "";
    }
    for (int i = MAXHIST - 1; i > 0; i--) hist[i] = hist[i - 1];
    hist[0] = list->currentText();

    list->clear();
    for (int i = 0; i < MAXHIST; i++) {
        if (hist[i] != "") list->addItem(hist[i]);
    }
}
//------------------------------------------------------------------------------
void TcpOptDialog::btnNtripClicked()
{
    QString path = addr->currentText() + ":" + port->text();
    stream_t str;
    uint32_t tick = tickget();
    char *p = srctbl, msg[MAXSTRMSG] = "", mntpnt[256];

    if (addr->currentText().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), "Enter the caster address first.");
        return;
    }
    strinit(&str);
    if (!stropen(&str, STR_NTRIPCLI, STR_MODE_R, qPrintable(path))) {
        QMessageBox::warning(this, windowTitle(),
                             QString("Cannot open the stream to %1.").arg(path));
        return;
    }
    btnNtrip->setEnabled(false);
    setCursor(Qt::WaitCursor);

    bool complete = false;
    *p = '\0';
    while (p < srctbl + MAXSRCTBL - 1) {
        p += strread(&str, (uint8_t *)p, (int)(srctbl + MAXSRCTBL - p - 1));
        *p = '\0';
        sleepms(NTRIP_CYCLE);
        QApplication::processEvents();  // keep UI alive (VCL version blocked)
        if (strstr(srctbl, ENDSRCTBL)) { complete = true; break; }
        if ((int)(tickget() - tick) > NTRIP_TIMEOUT) break;
    }
    int nrecv = (int)(p - srctbl);
    strstat(&str, msg);
    strclose(&str);
    setCursor(Qt::ArrowCursor);
    btnNtrip->setEnabled(true);

    QString current = mntPnt->currentText();
    mntPnt->clear();
    int n = 0;
    for (p = srctbl; (p = strstr(p, "STR;")); p += 4) {
        if (sscanf(p, "STR;%255[^;]", mntpnt) == 1) { mntPnt->addItem(mntpnt); n++; }
    }
    mntPnt->setCurrentText(current);

    // the VCL original fails silently here; say what happened instead
    if (n == 0) {
        QMessageBox::warning(this, windowTitle(),
            QString("No mountpoint was received from %1.\n\n"
                    "received: %2 bytes%3\nstream status: %4")
                .arg(path).arg(nrecv)
                .arg(complete ? "" : "  (timed out)")
                .arg(*msg ? msg : "(no message)"));
    }
    else if (!complete) {
        QMessageBox::information(this, windowTitle(),
            QString("%1 mountpoints received, but the source table was "
                    "truncated by the %2 s timeout.")
                .arg(n).arg(NTRIP_TIMEOUT / 1000));
    }
}
//------------------------------------------------------------------------------
void TcpOptDialog::btnBrowsClicked()
{
    QString addr_text = addr->currentText();
    if (port->text() != "") addr_text += ":" + port->text();
    execCmd("srctblbrows_qt " + addr_text);
}
//------------------------------------------------------------------------------
void TcpOptDialog::btnMountpClicked()
{
    // TODO: port mntpoptdlg (Phase 3)
    QMessageBox::information(this, windowTitle(),
                             "Mountpoint options dialog is not ported yet.");
}
//------------------------------------------------------------------------------
int TcpOptDialog::execCmd(const QString &cmd)
{
    QStringList args = QProcess::splitCommand(cmd);
    if (args.isEmpty()) return 0;
    QString prog = args.takeFirst();
    return QProcess::startDetached(prog, args) ? 1 : 0;
}
//------------------------------------------------------------------------------
