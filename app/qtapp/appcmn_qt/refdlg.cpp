//------------------------------------------------------------------------------
// refdlg.cpp : reference position dialog
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Qt port of app/winapp/appcmn/refdlg.
// Based on MALIB / RTKLIB:
//   Copyright (C) 2007-2023 by T.TAKASU, All rights reserved.
//   Copyright (C) 2023-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//   Copyright (C) 2023-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "rtklib.h"

#include "appstyle.h"
#include "refdlg.h"

static const QString CHARDEG = QString::fromUtf8("°");

//------------------------------------------------------------------------------
RefDialog::RefDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Reference Position");

    filter = new QLineEdit();
    filter->setPlaceholderText("filter by id / name");
    auto *btnLoad = new QPushButton("&Load...");

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel("Filter"));
    top->addWidget(filter, 1);
    top->addWidget(btnLoad);

    list = new QTableWidget();
    list->setColumnCount(6);
    list->setHorizontalHeaderLabels(
        {"Latitude (" + CHARDEG + ")", "Longitude (" + CHARDEG + ")",
         "Height (m)", "Id", "Name", "Distance (km)"});
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->verticalHeader()->setVisible(false);
    list->verticalHeader()->setDefaultSectionSize(18);
    for (int i = 0; i < 6; i++) list->setColumnWidth(i, i < 3 ? 120 : 100);

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
    vbox->addLayout(top);
    vbox->addWidget(list, 1);
    vbox->addLayout(btns);

    connect(btnLoad,   &QPushButton::clicked, this, &RefDialog::btnLoadClicked);
    connect(btnOk,     &QPushButton::clicked, this, &RefDialog::btnOkClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(filter,    &QLineEdit::textChanged, this, &RefDialog::filterChanged);
    connect(list,      &QTableWidget::doubleClicked, this, &RefDialog::btnOkClicked);

    resize(700, 420);
}
//------------------------------------------------------------------------------
void RefDialog::showEvent(QShowEvent *event)
{
    if (event->spontaneous()) return;
    if (!StaPosFile.isEmpty()) loadFile(StaPosFile);
    QDialog::showEvent(event);
}
//------------------------------------------------------------------------------
// station position file: "lat lon height id name ..." per line, # = comment
void RefDialog::loadFile(const QString &file)
{
    stas.clear();
    QFile fp(file);
    if (fp.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&fp);
        while (!in.atEnd()) {
            QString line = in.readLine().section('#', 0, 0).trimmed();
            if (line.isEmpty()) continue;
            QStringList v = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (v.size() < 4) continue;
            bool o1, o2, o3;
            Sta s;
            s.pos[0] = v[0].toDouble(&o1);
            s.pos[1] = v[1].toDouble(&o2);
            s.pos[2] = v[2].toDouble(&o3);
            if (!o1 || !o2 || !o3) continue;
            s.id = v[3];
            s.name = v.size() > 4 ? v[4] : QString();
            stas.append(s);
        }
    }
    StaPosFile = file;
    filterChanged();
}
//------------------------------------------------------------------------------
void RefDialog::filterChanged()
{
    QString f = filter->text().trimmed();
    double rpos[3], rr[3];
    bool haveRov = norm(RovPos, 3) > 0.0;
    if (haveRov) {
        rpos[0] = RovPos[0] * D2R; rpos[1] = RovPos[1] * D2R; rpos[2] = RovPos[2];
        pos2ecef(rpos, rr);
    }

    QList<const Sta *> shown;
    for (const Sta &s : stas) {
        if (!f.isEmpty() && !s.id.contains(f, Qt::CaseInsensitive) &&
            !s.name.contains(f, Qt::CaseInsensitive)) continue;
        shown.append(&s);
    }
    list->setRowCount(shown.size());
    for (int i = 0; i < shown.size(); i++) {
        const Sta *s = shown[i];
        auto set = [&](int c, const QString &t) {
            auto *it = new QTableWidgetItem(t);
            it->setData(Qt::UserRole, i);
            list->setItem(i, c, it);
        };
        set(0, QString::number(s->pos[0], 'f', 9));
        set(1, QString::number(s->pos[1], 'f', 9));
        set(2, QString::number(s->pos[2], 'f', 4));
        set(3, s->id);
        set(4, s->name);
        QString dist = "-";
        if (haveRov) {
            double p[3] = {s->pos[0] * D2R, s->pos[1] * D2R, s->pos[2]}, r[3], d[3];
            pos2ecef(p, r);
            for (int k = 0; k < 3; k++) d[k] = r[k] - rr[k];
            dist = QString::number(norm(d, 3) / 1000.0, 'f', 3);
        }
        set(5, dist);
    }
}
//------------------------------------------------------------------------------
void RefDialog::btnLoadClicked()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Station Position File", StaPosFile, "Position (*.pos);;All (*)");
    if (file.isEmpty()) return;
    loadFile(file);
}
//------------------------------------------------------------------------------
void RefDialog::btnOkClicked()
{
    int r = list->currentRow();
    if (r < 0) { reject(); return; }
    Pos[0] = list->item(r, 0)->text().toDouble();
    Pos[1] = list->item(r, 1)->text().toDouble();
    Pos[2] = list->item(r, 2)->text().toDouble();
    StaId   = list->item(r, 3)->text();
    StaName = list->item(r, 4)->text();
    accept();
}
//------------------------------------------------------------------------------
