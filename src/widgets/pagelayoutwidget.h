#ifndef PENELOPE_PAGELAYOUTWIDGET_H
#define PENELOPE_PAGELAYOUTWIDGET_H

#include <QHash>
#include <QWidget>
#include <QTimer>

#include "pagelayout.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;

class PageLayoutWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PageLayoutWidget(QWidget *parent = nullptr);

    PageLayout currentPageLayout() const;
    void setPageLayout(const PageLayout &layout);

Q_SIGNALS:
    void pageLayoutChanged();

private Q_SLOTS:
    void onPageTypeChanged(int index);
    void onEditHeadersFooters();

private:
    void saveCurrentPageTypeState();
    void applyUnits();
    qreal toMillimetres(qreal displayed) const;
    qreal fromMillimetres(qreal millimetres) const;
    bool isCustomSizeSelected() const;
    void updateCustomSizeVisibility();
    void loadPageTypeState(const QString &type);
    void blockAllSignals(bool block);

    QComboBox *m_pageTypeCombo = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QComboBox *m_orientationCombo = nullptr;
    QWidget *m_pageSizeRow = nullptr;
    QWidget *m_orientationRow = nullptr;
    QWidget *m_customSizeRow = nullptr;
    QDoubleSpinBox *m_customWidthSpin = nullptr;
    QDoubleSpinBox *m_customHeightSpin = nullptr;
    QComboBox *m_unitsCombo = nullptr;
    QDoubleSpinBox *m_marginTopSpin = nullptr;
    QDoubleSpinBox *m_marginBottomSpin = nullptr;
    QDoubleSpinBox *m_marginLeftSpin = nullptr;
    QDoubleSpinBox *m_marginRightSpin = nullptr;
    QTimer *m_marginDebounce = nullptr;

    // Header/footer controls
    QCheckBox *m_headerCheck = nullptr;
    QCheckBox *m_footerCheck = nullptr;
    QPushButton *m_editHfButton = nullptr;

    // Master page state
    QHash<QString, MasterPage> m_masterPages;
    QString m_currentPageType;  // empty = "All Pages"
    PageLayout m_baseLayout;    // base layout for inherit reference
    bool m_inches = false;      // display unit for the margin and size spin boxes
};

#endif // PENELOPE_PAGELAYOUTWIDGET_H
