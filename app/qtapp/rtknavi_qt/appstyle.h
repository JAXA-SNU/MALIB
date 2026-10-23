//------------------------------------------------------------------------------
// appstyle.h : common look-and-feel for the MALIB Qt apps
//
// The original RTKNAVI (VCL) is a compact, tightly-spaced dialog-style app.
// Qt's native Windows 11 style (rounded corners, thick padding, large
// controls) looks nothing like it and differs from Linux, so the apps pin
// the Fusion style and a compact metric set. Result: identical appearance on
// both platforms, close to the original.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef APPSTYLE_H
#define APPSTYLE_H

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

// compact spacing used by every layout (original uses ~4-6 px gaps)
#define UI_MARGIN   4
#define UI_SPACING  3

inline void applyAppStyle(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QFont f = app.font();
#ifdef WIN32
    f.setFamily("Segoe UI");
#endif
    f.setPointSizeF(8.5);               // original RTKNAVI is ~8pt Tahoma
    app.setFont(f);

    // tighten the controls the way the VCL forms are laid out
    app.setStyleSheet(
        "QGroupBox{margin-top:8px;padding-top:6px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:6px;color:#404040;}"
        "QComboBox,QLineEdit{min-height:16px;padding:0px 3px;}"
        "QPushButton{min-height:18px;padding:1px 8px;}"
        "QTabWidget::pane{border:1px solid #a0a0a0;}"
        "QTabBar::tab{padding:3px 10px;}"
        "QLabel{padding:0px;}"
    );
}

#endif // APPSTYLE_H
