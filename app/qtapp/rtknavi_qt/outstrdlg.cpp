//------------------------------------------------------------------------------
// outstrdlg.cpp : output/log streams dialog
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/rtknavi/outstrdlg + logstrdlg.
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
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "appstyle.h"

#include "outstrdlg.h"
#include "serioptdlg.h"
#include "tcpoptdlg.h"

// stream types selectable for output/log (value = STR_*)
static const struct { const char *label; int type; } OSTR_TYPES[] = {
    {"Serial",       STR_SERIAL},
    {"TCP Client",   STR_TCPCLI},
    {"TCP Server",   STR_TCPSVR},
    {"NTRIP Server", STR_NTRIPSVR},
    {"NTRIP Caster", STR_NTRIPCAS},
    {"UDP Client",   STR_UDPCLI},
    {"File",         STR_FILE},
};
static const char *SOL_FMTS[] = {"LLH", "XYZ", "ENU", "NMEA", "STAT"};

//------------------------------------------------------------------------------
OutStrDialog::OutStrDialog(int mode_, QWidget *parent)
    : QDialog(parent), mode(mode_)
{
    nstr = mode ? NSTRLOG : NSTROUT;
    setWindowTitle(mode ? "Log Streams" : "Output Streams");

    static const char *outLabel[] = {"(1) Solution 1", "(2) Solution 2"};
    static const char *logLabel[] = {
        "(1) Rover", "(2) Correction 1", "(3) Correction 2", "(4) Correction 3"
    };

    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel(mode ? "Log Stream" : "Output Stream"), 0, 0);
    grid->addWidget(new QLabel("Type"), 0, 1);
    grid->addWidget(new QLabel("Opt"),  0, 2);
    if (!mode) grid->addWidget(new QLabel("Format"), 0, 3);

    for (int i = 0; i < nstr; i++) {
        streamC[i] = new QCheckBox(mode ? logLabel[i] : outLabel[i]);
        stream[i] = new QComboBox();
        for (auto &t : OSTR_TYPES) stream[i]->addItem(t.label, t.type);
        stream[i]->setMinimumWidth(110);
        btnStr[i] = new QPushButton("...");
        btnStr[i]->setFixedWidth(28);
        format[i] = new QComboBox();
        format[i]->setMinimumWidth(80);
        for (int f = 0; f < 5; f++) format[i]->addItem(SOL_FMTS[f], f);
        format[i]->setVisible(!mode);

        grid->addWidget(streamC[i], 1 + i, 0);
        grid->addWidget(stream[i],  1 + i, 1);
        grid->addWidget(btnStr[i],  1 + i, 2);
        if (!mode) grid->addWidget(format[i], 1 + i, 3);

        connect(streamC[i], &QCheckBox::toggled, this, &OutStrDialog::updateEnable);
        connect(stream[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &OutStrDialog::updateEnable);
        connect(btnStr[i], &QPushButton::clicked, this, [this, i] { btnStrClicked(i); });
    }

    labelFile = new QLabel(mode ? "Log File Paths" : "Output File Paths");
    labelFile->setStyleSheet("color:gray;");
    auto *files = new QGridLayout();
    for (int i = 0; i < nstr; i++) {
        filePath[i] = new QLineEdit();
        filePath[i]->setToolTip(
            "keywords: %Y=year %m=month %d=day %h=hour %M=min %S=sec %r=station");
        btnFile[i] = new QPushButton("...");
        btnFile[i]->setFixedWidth(28);
        files->addWidget(filePath[i], i, 0);
        files->addWidget(btnFile[i],  i, 1);
        connect(btnFile[i], &QPushButton::clicked, this, [this, i] { btnFileClicked(i); });
    }
    timeTagC = new QCheckBox("Output Time-Tag");
    swapIntv = new QLineEdit();
    swapIntv->setMaximumWidth(60);
    auto *tagRow = new QHBoxLayout();
    tagRow->addWidget(timeTagC);
    tagRow->addStretch();
    tagRow->addWidget(new QLabel("Swap Interval"));
    tagRow->addWidget(swapIntv);
    tagRow->addWidget(new QLabel("h"));

    auto *btnOk = new QPushButton("&OK");
    auto *btnCancel = new QPushButton("&Cancel");
    btnOk->setDefault(true);
    auto *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);
    connect(btnOk,     &QPushButton::clicked, this, &OutStrDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(UI_MARGIN, UI_MARGIN, UI_MARGIN, UI_MARGIN);
    vbox->setSpacing(UI_SPACING);
    vbox->addLayout(grid);
    vbox->addWidget(labelFile);
    vbox->addLayout(files);
    vbox->addLayout(tagRow);
    vbox->addLayout(btns);
}
//------------------------------------------------------------------------------
int OutStrDialog::kindOf(int type) const
{
    switch (type) {
        case STR_SERIAL: return 0;
        case STR_FILE:   return 2;
        default:         return 1;      // tcp/ntrip/udp
    }
}
//------------------------------------------------------------------------------
void OutStrDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;

    bool tag = false;
    QString swap;

    for (int i = 0; i < nstr; i++) {
        int type = strType[i];
        streamC[i]->setChecked(type != STR_NONE);
        if (type != STR_NONE) {
            int idx = stream[i]->findData(type);
            if (idx >= 0) stream[i]->setCurrentIndex(idx);
        }
        if (!mode && strFmt[i] >= 0 && strFmt[i] < 5) {
            format[i]->setCurrentIndex(strFmt[i]);
        }
        for (int k = 0; k < 4; k++) paths[i][k] = "";
        QString p = strPath[i];
        int kind = kindOf(type == STR_NONE ? stream[i]->currentData().toInt() : type);
        if (kind == 2) {
            QString base = p.section("::", 0, 0);
            paths[i][2] = base;
            filePath[i]->setText(base);
            for (const QString &t : p.mid(base.length()).split("::", Qt::SkipEmptyParts)) {
                if      (t == "T")          tag = true;
                else if (t.startsWith("S=")) swap = t.mid(2);
            }
        }
        else {
            paths[i][kind] = p;
            filePath[i]->setText("");
        }
    }
    timeTagC->setChecked(tag);
    swapIntv->setText(swap);
    updateEnable();
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
QString OutStrDialog::filePathWithTags(const QString &base) const
{
    QString path = base;
    if (timeTagC->isChecked()) path += "::T";
    bool ok;
    swapIntv->text().toDouble(&ok);
    if (ok) path += "::S=" + swapIntv->text();
    return path;
}
//------------------------------------------------------------------------------
void OutStrDialog::btnOkClicked()
{
    for (int i = 0; i < nstr; i++) {
        int type = stream[i]->currentData().toInt();
        strType[i] = streamC[i]->isChecked() ? type : STR_NONE;
        int kind = kindOf(type);
        if (kind == 2) {
            strPath[i] = filePath[i]->text().isEmpty()
                             ? QString() : filePathWithTags(filePath[i]->text());
        }
        else {
            strPath[i] = paths[i][kind];
        }
        if (!mode) strFmt[i] = format[i]->currentIndex();
    }
    accept();
}
//------------------------------------------------------------------------------
void OutStrDialog::btnStrClicked(int i)
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
    case STR_NTRIPSVR:
    case STR_NTRIPCAS:
    case STR_UDPCLI: {
        TcpOptDialog dlg(this);
        dlg.Path = paths[i][1];
        dlg.Opt = type == STR_TCPSVR ? 0 : type == STR_TCPCLI  ? 1 :
                  type == STR_NTRIPSVR ? 2 : type == STR_NTRIPCAS ? 4 : 1;
        for (int j = 0; j < 10; j++) dlg.History[j] = history[j];
        if (dlg.exec() != QDialog::Accepted) return;
        paths[i][1] = dlg.Path;
        for (int j = 0; j < 10; j++) history[j] = dlg.History[j];
        break;
    }
    default: break;
    }
}
//------------------------------------------------------------------------------
void OutStrDialog::btnFileClicked(int i)
{
    QString file = QFileDialog::getSaveFileName(
        this, "File Path", filePath[i]->text(), QString(), nullptr,
        QFileDialog::DontConfirmOverwrite);
    if (file.isEmpty()) return;
    filePath[i]->setText(file);
}
//------------------------------------------------------------------------------
void OutStrDialog::updateEnable()
{
    bool enaFile = false;
    for (int i = 0; i < nstr; i++) {
        bool on = streamC[i]->isChecked();
        int type = stream[i]->currentData().toInt();
        stream[i]->setEnabled(on);
        btnStr[i]->setEnabled(on && type != STR_FILE);
        if (!mode) format[i]->setEnabled(on);
        bool isFile = on && type == STR_FILE;
        filePath[i]->setEnabled(isFile);
        btnFile[i]->setEnabled(isFile);
        if (isFile) enaFile = true;
    }
    labelFile->setEnabled(enaFile);
    timeTagC->setEnabled(enaFile);
    swapIntv->setEnabled(enaFile);
}
//------------------------------------------------------------------------------
