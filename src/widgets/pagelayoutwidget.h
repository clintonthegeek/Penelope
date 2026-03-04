#ifndef PENELOPE_PAGELAYOUTWIDGET_H
#define PENELOPE_PAGELAYOUTWIDGET_H

#include <QHash>
#include <QWidget>

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
    void loadPageTypeState(const QString &type);
    void blockAllSignals(bool block);

    QComboBox *m_pageTypeCombo = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QComboBox *m_orientationCombo = nullptr;
    QWidget *m_pageSizeRow = nullptr;
    QWidget *m_orientationRow = nullptr;
    QDoubleSpinBox *m_marginTopSpin = nullptr;
    QDoubleSpinBox *m_marginBottomSpin = nullptr;
    QDoubleSpinBox *m_marginLeftSpin = nullptr;
    QDoubleSpinBox *m_marginRightSpin = nullptr;

    // Header/footer controls
    QCheckBox *m_headerCheck = nullptr;
    QCheckBox *m_footerCheck = nullptr;
    QPushButton *m_editHfButton = nullptr;

    // Master page state
    QHash<QString, MasterPage> m_masterPages;
    QString m_currentPageType;  // empty = "All Pages"
    PageLayout m_baseLayout;    // base layout for inherit reference
};

#endif // PENELOPE_PAGELAYOUTWIDGET_H
