#ifndef PENELOPE_TABLESTYLEPROPERTIESEDITOR_H
#define PENELOPE_TABLESTYLEPROPERTIESEDITOR_H

#include "tablestyle.h"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

class TableStylePropertiesEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TableStylePropertiesEditor(QWidget *parent = nullptr);

    void loadTableStyle(const TableStyle &style,
                        const QStringList &paraStyleNames);
    void applyToTableStyle(TableStyle &style) const;
    void clear();

Q_SIGNALS:
    void propertyChanged();

private:
    void buildUI();
    void blockAllSignals(bool block);

    struct BorderRow {
        QDoubleSpinBox *widthSpin = nullptr;
        QComboBox *styleCombo = nullptr;
    };
    BorderRow createBorderRow(const QString &label, QWidget *parent);

    // Borders
    BorderRow m_outerBorder;
    BorderRow m_innerBorder;
    BorderRow m_headerBottomBorder;

    // Cell padding
    QDoubleSpinBox *m_padTopSpin = nullptr;
    QDoubleSpinBox *m_padBottomSpin = nullptr;
    QDoubleSpinBox *m_padLeftSpin = nullptr;
    QDoubleSpinBox *m_padRightSpin = nullptr;

    // Paragraph styles
    QComboBox *m_headerParaCombo = nullptr;
    QComboBox *m_bodyParaCombo = nullptr;
};

#endif // PENELOPE_TABLESTYLEPROPERTIESEDITOR_H
