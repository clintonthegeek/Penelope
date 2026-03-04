#ifndef PENELOPE_FOOTNOTECONFIGWIDGET_H
#define PENELOPE_FOOTNOTECONFIGWIDGET_H

#include "footnotestyle.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class FootnoteConfigWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FootnoteConfigWidget(QWidget *parent = nullptr);

    void loadFootnoteStyle(const FootnoteStyle &style);
    FootnoteStyle currentFootnoteStyle() const;

Q_SIGNALS:
    void footnoteStyleChanged();

private:
    void buildUI();
    void blockAllSignals(bool block);

    // Numbering
    QComboBox *m_formatCombo = nullptr;
    QSpinBox *m_startSpin = nullptr;
    QComboBox *m_restartCombo = nullptr;
    QLineEdit *m_prefixEdit = nullptr;
    QLineEdit *m_suffixEdit = nullptr;

    // Appearance
    QCheckBox *m_superRefCheck = nullptr;
    QCheckBox *m_superNoteCheck = nullptr;
    QCheckBox *m_endnotesCheck = nullptr;

    // Separator
    QCheckBox *m_separatorCheck = nullptr;
    QDoubleSpinBox *m_sepWidthSpin = nullptr;
    QDoubleSpinBox *m_sepLengthSpin = nullptr;
};

#endif // PENELOPE_FOOTNOTECONFIGWIDGET_H
