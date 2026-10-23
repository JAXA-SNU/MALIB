//------------------------------------------------------------------------------
// refdlg.h : reference position dialog (Qt port of winapp/appcmn/refdlg)
//
// Picks a position from a station position file (RTKLIB pos format:
// lat lon height id name antenna ...) with distance from the rover shown.
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
#ifndef REFDLG_H
#define REFDLG_H

#include <QDialog>

class QLineEdit;
class QTableWidget;

class RefDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RefDialog(QWidget *parent = nullptr);

    double  Pos[3] = {0, 0, 0};         // selected position (deg,deg,m)
    double  RovPos[3] = {0, 0, 0};      // rover position for the distance column
    QString StaPosFile;
    QString StaId, StaName;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnLoadClicked();
    void btnOkClicked();
    void filterChanged();

private:
    void loadFile(const QString &file);

    struct Sta { double pos[3]; QString id, name; };
    QList<Sta>    stas;
    QTableWidget *list;
    QLineEdit    *filter;
};

#endif // REFDLG_H
