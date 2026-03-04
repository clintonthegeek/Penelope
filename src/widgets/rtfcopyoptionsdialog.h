/*
 * rtfcopyoptionsdialog.h — Dialog for choosing RTF copy style options
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_RTFCOPYOPTIONSDIALOG_H
#define PENELOPE_RTFCOPYOPTIONSDIALOG_H

#include <QDialog>

#include "rtffilteroptions.h"

class QCheckBox;
class QComboBox;

class RtfCopyOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RtfCopyOptionsDialog(QWidget *parent = nullptr);

    RtfFilterOptions filterOptions() const;

private Q_SLOTS:
    void onPresetChanged(int index);
    void onCheckboxToggled();

private:
    void loadSettings();
    void saveSettings();
    void applyFilterToCheckboxes(const RtfFilterOptions &opts);
    RtfFilterOptions checkboxesToFilter() const;
    void blockCheckboxSignals(bool block);

    QComboBox *m_presetCombo = nullptr;

    // Character-level
    QCheckBox *m_fontsCb = nullptr;
    QCheckBox *m_emphasisCb = nullptr;
    QCheckBox *m_scriptsCb = nullptr;
    QCheckBox *m_textColorCb = nullptr;
    QCheckBox *m_highlightsCb = nullptr;
    QCheckBox *m_sourceFormattingCb = nullptr;

    // Paragraph-level
    QCheckBox *m_alignmentCb = nullptr;
    QCheckBox *m_spacingCb = nullptr;
    QCheckBox *m_marginsCb = nullptr;
};

#endif // PENELOPE_RTFCOPYOPTIONSDIALOG_H
