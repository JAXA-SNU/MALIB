//------------------------------------------------------------------------------
// optdlg.h : options dialog
//
// Setting1/Setting2 follow the original RTKNAVI layout (related options paired
// on one line, checkbox rows, sub-dialogs for the SNR mask / SNR weighting).
// The remaining tabs are generated from the sysopts table, so options the
// vendor adds later still show up without touching the GUI.
//
// Copyright (C) 2026 Japan Aerospace Exploration Agency. All Rights Reserved.
//
// Distributed under the BSD 2-clause license (see LICENSE.txt).
//------------------------------------------------------------------------------
#ifndef OPTDLG_H
#define OPTDLG_H

#include <QDialog>
#include <QList>
#include <QSet>
#include <QStringList>

#include "rtklib.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGridLayout;
class QLineEdit;
class QTabWidget;
class QWidget;

class OptDialog : public QDialog
{
    Q_OBJECT
public:
    // extraOpts: additional option table (stream settings etc.) included in
    // conf Load/Save so a full rtkrcv .conf round-trips (may be NULL)
    explicit OptDialog(opt_t *extraOpts, prcopt_t *prcopt, solopt_t *solopt,
                       filopt_t *filopt, QWidget *parent = nullptr);

    QString confFile;                   // last loaded/saved conf path
    int    *ssrVar = nullptr;           // inpstrN-ssr array (str2enum workaround)

    // exhaustive widget-to-option mapping check (--selftest): drives every
    // editor and verifies that exactly the intended option changes.
    // returns the number of failures; details appended to ng.
    int selfTest(QStringList &ng);

    // report which sysopts entries a widget on this dialog can edit
    // (used to document the machine-checked scope). returns covered count.
    int coverage(QStringList &out);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void btnLoadClicked();
    void btnSaveClicked();
    void btnOkClicked();
    void updateEnable();

private:
    struct Row {
        opt_t     *opt = nullptr;
        QComboBox *combo = nullptr;     // enum
        QLineEdit *edit = nullptr;      // number / string
        QCheckBox *check = nullptr;     // on-off enum shown as a checkbox
        int        bit = 0;             // bitmask member (navsys/arsys)
    };

    // widget factory / layout helpers
    opt_t    *find(const char *name);
    QWidget  *editorFor(const char *name, int width = 0);
    QCheckBox *checkFor(const char *name, const QString &label);
    void      addRow(QFormLayout *form, const QString &label, const char *name);
    void      addRow2(QFormLayout *form, const QString &label,
                      const char *a, const char *b);
    QWidget  *bitmaskRow(const char *name, const QStringList &labels,
                         const QList<int> &bits);
    QFormLayout *addGroup(QGridLayout *outer, const char *title, int r, int c);
    QWidget  *buildSetting1();
    QWidget  *buildSetting2();
    QWidget  *buildSetting3();
    QWidget  *buildOutput();
    QWidget  *buildStatistics();
    QWidget  *refButton(const char *prefix);
    QWidget  *buildPositions();
    QWidget  *buildFiles();
    QWidget  *buildMisc();
    void      buildGeneratedTabs();
    void      snrMaskDialog();
    void      snrWeightDialog();

    void loadValues();
    void applyValues();

    QList<Row>  rows;
    QSet<QString> placed;               // options already on a hand-built tab
    QTabWidget *tabs;
    opt_t      *extraOpts;
    prcopt_t   *prcopt;
    solopt_t   *solopt;
    filopt_t   *filopt;
    QStringList rawKept;                // opt2str-unserializable lines
};

#endif // OPTDLG_H
