#include "pagelayoutwidget.h"

#include "headerfooterdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMarginsF>
#include <QPageSize>
#include <QSignalBlocker>
#include <QVBoxLayout>

PageLayoutWidget::PageLayoutWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    // Page type selector (master pages)
    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(new QLabel(tr("Page type:")));
    m_pageTypeCombo = new QComboBox;
    m_pageTypeCombo->addItem(tr("All Pages"));
    m_pageTypeCombo->addItem(tr("First Page"));
    m_pageTypeCombo->addItem(tr("Left Pages"));
    m_pageTypeCombo->addItem(tr("Right Pages"));
    typeRow->addWidget(m_pageTypeCombo);
    layout->addLayout(typeRow);

    // Page size (wrapped in a widget for show/hide)
    m_pageSizeRow = new QWidget;
    auto *sizeRowLayout = new QHBoxLayout(m_pageSizeRow);
    sizeRowLayout->setContentsMargins(0, 0, 0, 0);
    sizeRowLayout->addWidget(new QLabel(tr("Size:")));
    m_pageSizeCombo = new QComboBox;
    for (QPageSize::PageSizeId id : PageSizes::offered())
        m_pageSizeCombo->addItem(PageSizes::label(id), static_cast<int>(id));
    sizeRowLayout->addWidget(m_pageSizeCombo, 1);
    sizeRowLayout->addWidget(new QLabel(tr("Units:")));
    m_unitsCombo = new QComboBox;
    m_unitsCombo->addItem(tr("Millimetres"));
    m_unitsCombo->addItem(tr("Inches"));
    sizeRowLayout->addWidget(m_unitsCombo);
    layout->addWidget(m_pageSizeRow);

    // Custom page dimensions, shown only while "Custom" is chosen.
    m_customSizeRow = new QWidget;
    auto *customRow = new QHBoxLayout(m_customSizeRow);
    customRow->setContentsMargins(0, 0, 0, 0);
    customRow->addWidget(new QLabel(tr("Page:")));
    m_customWidthSpin = new QDoubleSpinBox;
    m_customWidthSpin->setRange(40.0, 2000.0);
    m_customWidthSpin->setDecimals(1);
    m_customWidthSpin->setValue(210.0);
    m_customWidthSpin->setSuffix(QStringLiteral(" mm"));
    customRow->addWidget(m_customWidthSpin);
    customRow->addWidget(new QLabel(tr("\u00d7")));
    m_customHeightSpin = new QDoubleSpinBox;
    m_customHeightSpin->setRange(40.0, 2000.0);
    m_customHeightSpin->setDecimals(1);
    m_customHeightSpin->setValue(297.0);
    m_customHeightSpin->setSuffix(QStringLiteral(" mm"));
    customRow->addWidget(m_customHeightSpin);
    customRow->addStretch();
    layout->addWidget(m_customSizeRow);
    m_customSizeRow->setVisible(false);

    // Orientation (wrapped in a widget for show/hide)
    m_orientationRow = new QWidget;
    auto *orientRowLayout = new QHBoxLayout(m_orientationRow);
    orientRowLayout->setContentsMargins(0, 0, 0, 0);
    orientRowLayout->addWidget(new QLabel(tr("Orientation:")));
    m_orientationCombo = new QComboBox;
    m_orientationCombo->addItem(tr("Portrait"), static_cast<int>(QPageLayout::Portrait));
    m_orientationCombo->addItem(tr("Landscape"), static_cast<int>(QPageLayout::Landscape));
    orientRowLayout->addWidget(m_orientationCombo);
    layout->addWidget(m_orientationRow);

    // Margins
    layout->addWidget(new QLabel(tr("Margins (mm):")));

    auto makeMarginSpin = []() {
        auto *spin = new QDoubleSpinBox;
        spin->setRange(0.0, 200.0);
        spin->setSuffix(QStringLiteral(" mm"));
        spin->setDecimals(1);
        spin->setValue(25.0);
        spin->setSpecialValueText(tr("(inherit)"));
        return spin;
    };

    auto *topBottomRow = new QHBoxLayout;
    topBottomRow->addWidget(new QLabel(tr("Top:")));
    m_marginTopSpin = makeMarginSpin();
    topBottomRow->addWidget(m_marginTopSpin);
    topBottomRow->addWidget(new QLabel(tr("Bottom:")));
    m_marginBottomSpin = makeMarginSpin();
    topBottomRow->addWidget(m_marginBottomSpin);
    layout->addLayout(topBottomRow);

    auto *leftRightRow = new QHBoxLayout;
    leftRightRow->addWidget(new QLabel(tr("Left:")));
    m_marginLeftSpin = makeMarginSpin();
    leftRightRow->addWidget(m_marginLeftSpin);
    leftRightRow->addWidget(new QLabel(tr("Right:")));
    m_marginRightSpin = makeMarginSpin();
    leftRightRow->addWidget(m_marginRightSpin);
    layout->addLayout(leftRightRow);

    // --- Header/footer section ---
    m_headerCheck = new QCheckBox(tr("Header"));
    layout->addWidget(m_headerCheck);

    m_footerCheck = new QCheckBox(tr("Footer"));
    m_footerCheck->setChecked(true);
    layout->addWidget(m_footerCheck);

    m_editHfButton = new QPushButton(tr("Edit Headers && Footers..."));
    m_editHfButton->setEnabled(true);
    layout->addWidget(m_editHfButton);

    auto updateEditButton = [this]() {
        m_editHfButton->setEnabled(m_headerCheck->isChecked() || m_footerCheck->isChecked());
    };
    connect(m_headerCheck, &QCheckBox::toggled, this, updateEditButton);
    connect(m_footerCheck, &QCheckBox::toggled, this, updateEditButton);
    connect(m_editHfButton, &QPushButton::clicked, this, &PageLayoutWidget::onEditHeadersFooters);

    layout->addStretch();

    // Connect signals
    connect(m_pageSizeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        updateCustomSizeVisibility();
        Q_EMIT pageLayoutChanged();
    });
    connect(m_unitsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool toInches = (index == 1);
        if (toInches == m_inches)
            return;
        const qreal factor = toInches ? 1.0 / 25.4 : 25.4;
        const QList<QDoubleSpinBox *> spins{m_marginTopSpin, m_marginBottomSpin,
                                            m_marginLeftSpin, m_marginRightSpin,
                                            m_customWidthSpin, m_customHeightSpin};
        // Widen the range first: converting 25 mm to inches inside a range that
        // still stops at 8 in would silently clamp the value.
        for (auto *spin : spins)
            spin->setRange(spin->minimum() < 0 ? -1.0 : 0.0, 100000.0);
        for (auto *spin : spins) {
            const bool inherited = spin->minimum() < 0 && spin->value() < 0;
            if (!inherited)
                spin->setValue(spin->value() * factor);
        }
        m_inches = toInches;
        applyUnits(); // tightens the ranges for the new unit
        Q_EMIT pageLayoutChanged();
    });
    connect(m_customWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() { m_marginDebounce->start(); });
    connect(m_customHeightSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() { m_marginDebounce->start(); });
    connect(m_orientationCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PageLayoutWidget::pageLayoutChanged);
    m_marginDebounce = new QTimer(this);
    m_marginDebounce->setSingleShot(true);
    m_marginDebounce->setInterval(150);
    connect(m_marginDebounce, &QTimer::timeout, this, &PageLayoutWidget::pageLayoutChanged);
    auto debounceMargin = [this]() { m_marginDebounce->start(); };
    connect(m_marginTopSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, debounceMargin);
    connect(m_marginBottomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, debounceMargin);
    connect(m_marginLeftSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, debounceMargin);
    connect(m_marginRightSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, debounceMargin);

    // Header/footer signal connections
    connect(m_headerCheck, &QCheckBox::toggled,
            this, &PageLayoutWidget::pageLayoutChanged);
    connect(m_footerCheck, &QCheckBox::toggled,
            this, &PageLayoutWidget::pageLayoutChanged);

    // Page type combo
    connect(m_pageTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PageLayoutWidget::onPageTypeChanged);
}

void PageLayoutWidget::blockAllSignals(bool block)
{
    m_pageSizeCombo->blockSignals(block);
    m_orientationCombo->blockSignals(block);
    m_customWidthSpin->blockSignals(block);
    m_customHeightSpin->blockSignals(block);
    m_unitsCombo->blockSignals(block);
    m_marginTopSpin->blockSignals(block);
    m_marginBottomSpin->blockSignals(block);
    m_marginLeftSpin->blockSignals(block);
    m_marginRightSpin->blockSignals(block);
    m_headerCheck->blockSignals(block);
    m_footerCheck->blockSignals(block);
    m_pageTypeCombo->blockSignals(block);
}

void PageLayoutWidget::onPageTypeChanged(int index)
{
    // Save current state before switching
    saveCurrentPageTypeState();

    // Determine new page type
    static const QString types[] = {QString(), QStringLiteral("first"),
                                     QStringLiteral("left"), QStringLiteral("right")};
    QString newType = (index >= 0 && index < 4) ? types[index] : QString();

    m_currentPageType = newType;
    loadPageTypeState(newType);

    Q_EMIT pageLayoutChanged();
}

void PageLayoutWidget::saveCurrentPageTypeState()
{
    if (m_currentPageType.isEmpty()) {
        // Saving base layout
        m_baseLayout.pageSizeId = static_cast<QPageSize::PageSizeId>(
            m_pageSizeCombo->currentData().toInt());
        if (isCustomSizeSelected()) {
            m_baseLayout.customSizeMm = QSizeF(toMillimetres(m_customWidthSpin->value()),
                                               toMillimetres(m_customHeightSpin->value()));
        }
        m_baseLayout.orientation = static_cast<QPageLayout::Orientation>(
            m_orientationCombo->currentData().toInt());
        m_baseLayout.margins = QMarginsF(m_marginLeftSpin->value(),
                                          m_marginTopSpin->value(),
                                          m_marginRightSpin->value(),
                                          m_marginBottomSpin->value());
        m_baseLayout.headerEnabled = m_headerCheck->isChecked();
        m_baseLayout.footerEnabled = m_footerCheck->isChecked();
    } else {
        MasterPage mp;
        mp.name = m_currentPageType;

        Qt::CheckState hState = m_headerCheck->checkState();
        mp.headerEnabled = (hState == Qt::PartiallyChecked) ? -1 : (hState == Qt::Checked ? 1 : 0);
        Qt::CheckState fState = m_footerCheck->checkState();
        mp.footerEnabled = (fState == Qt::PartiallyChecked) ? -1 : (fState == Qt::Checked ? 1 : 0);

        auto storeMargin = [this](qreal displayed) {
            return displayed < 0 ? -1.0 : toMillimetres(displayed);
        };
        mp.marginTop = storeMargin(m_marginTopSpin->value());
        mp.marginBottom = storeMargin(m_marginBottomSpin->value());
        mp.marginLeft = storeMargin(m_marginLeftSpin->value());
        mp.marginRight = storeMargin(m_marginRightSpin->value());

        // Preserve text overrides managed by the dialog
        auto existingIt = m_masterPages.find(m_currentPageType);
        if (existingIt != m_masterPages.end()) {
            const MasterPage &existing = existingIt.value();
            mp.headerLeft = existing.headerLeft;
            mp.hasHeaderLeft = existing.hasHeaderLeft;
            mp.headerCenter = existing.headerCenter;
            mp.hasHeaderCenter = existing.hasHeaderCenter;
            mp.headerRight = existing.headerRight;
            mp.hasHeaderRight = existing.hasHeaderRight;
            mp.footerLeft = existing.footerLeft;
            mp.hasFooterLeft = existing.hasFooterLeft;
            mp.footerCenter = existing.footerCenter;
            mp.hasFooterCenter = existing.hasFooterCenter;
            mp.footerRight = existing.footerRight;
            mp.hasFooterRight = existing.hasFooterRight;
        }

        if (mp.isDefault())
            m_masterPages.remove(m_currentPageType);
        else
            m_masterPages.insert(m_currentPageType, mp);
    }
}

void PageLayoutWidget::loadPageTypeState(const QString &type)
{
    blockAllSignals(true);

    bool isBase = type.isEmpty();

    // Show/hide page size and orientation rows (only for base layout)
    m_pageSizeRow->setVisible(isBase);
    m_orientationRow->setVisible(isBase);

    if (isBase) {
        // Revert to two-state checkboxes
        m_headerCheck->setTristate(false);
        m_footerCheck->setTristate(false);

        // Load base layout values
        for (int i = 0; i < m_pageSizeCombo->count(); ++i) {
            if (m_pageSizeCombo->itemData(i).toInt() == static_cast<int>(m_baseLayout.pageSizeId)) {
                m_pageSizeCombo->setCurrentIndex(i);
                break;
            }
        }
        m_orientationCombo->setCurrentIndex(
            m_baseLayout.orientation == QPageLayout::Landscape ? 1 : 0);

        // A page may have no margin at all; the master page types keep the
        // (inherit) sentinel instead.
        m_marginTopSpin->setMinimum(fromMillimetres(0.0));
        m_marginBottomSpin->setMinimum(fromMillimetres(0.0));
        m_marginLeftSpin->setMinimum(fromMillimetres(0.0));
        m_marginRightSpin->setMinimum(fromMillimetres(0.0));

        m_marginTopSpin->setValue(fromMillimetres(m_baseLayout.margins.top()));
        m_marginBottomSpin->setValue(fromMillimetres(m_baseLayout.margins.bottom()));
        m_marginLeftSpin->setValue(fromMillimetres(m_baseLayout.margins.left()));
        m_marginRightSpin->setValue(fromMillimetres(m_baseLayout.margins.right()));

        if (m_baseLayout.hasCustomSize()) {
            m_customWidthSpin->setValue(fromMillimetres(m_baseLayout.customSizeMm.width()));
            m_customHeightSpin->setValue(fromMillimetres(m_baseLayout.customSizeMm.height()));
        }
        updateCustomSizeVisibility();

        m_headerCheck->setChecked(m_baseLayout.headerEnabled);
        m_footerCheck->setChecked(m_baseLayout.footerEnabled);
    } else {
        // Enable tri-state for master page checkboxes
        m_headerCheck->setTristate(true);
        m_footerCheck->setTristate(true);

        // Allow -1 for inherit
        m_marginTopSpin->setMinimum(-1.0);
        m_marginBottomSpin->setMinimum(-1.0);
        m_marginLeftSpin->setMinimum(-1.0);
        m_marginRightSpin->setMinimum(-1.0);

        MasterPage mp;
        auto it = m_masterPages.find(type);
        if (it != m_masterPages.end())
            mp = it.value();

        // Header/footer checkboxes: -1=partial(inherit), 0=unchecked, 1=checked
        if (mp.headerEnabled < 0)
            m_headerCheck->setCheckState(Qt::PartiallyChecked);
        else
            m_headerCheck->setCheckState(mp.headerEnabled ? Qt::Checked : Qt::Unchecked);

        if (mp.footerEnabled < 0)
            m_footerCheck->setCheckState(Qt::PartiallyChecked);
        else
            m_footerCheck->setCheckState(mp.footerEnabled ? Qt::Checked : Qt::Unchecked);

        // Margins: -1 = inherit
        auto loadMargin = [this](qreal millimetres) {
            return millimetres < 0 ? -1.0 : fromMillimetres(millimetres);
        };
        m_marginTopSpin->setValue(loadMargin(mp.marginTop));
        m_marginBottomSpin->setValue(loadMargin(mp.marginBottom));
        m_marginLeftSpin->setValue(loadMargin(mp.marginLeft));
        m_marginRightSpin->setValue(loadMargin(mp.marginRight));
        m_customSizeRow->setVisible(false);
    }

    blockAllSignals(false);
}

PageLayout PageLayoutWidget::currentPageLayout() const
{
    // Save current state first (const_cast needed since this is const)
    const_cast<PageLayoutWidget *>(this)->saveCurrentPageTypeState();

    PageLayout pl = m_baseLayout;
    pl.masterPages = m_masterPages;

    // If we're on the base page, read directly from widgets
    if (m_currentPageType.isEmpty()) {
        pl.pageSizeId = static_cast<QPageSize::PageSizeId>(m_pageSizeCombo->currentData().toInt());
        pl.orientation = static_cast<QPageLayout::Orientation>(m_orientationCombo->currentData().toInt());
        pl.margins = QMarginsF(toMillimetres(m_marginLeftSpin->value()),
                               toMillimetres(m_marginTopSpin->value()),
                               toMillimetres(m_marginRightSpin->value()),
                               toMillimetres(m_marginBottomSpin->value()));
        if (isCustomSizeSelected()) {
            pl.customSizeMm = QSizeF(toMillimetres(m_customWidthSpin->value()),
                                     toMillimetres(m_customHeightSpin->value()));
        }
        pl.headerEnabled = m_headerCheck->isChecked();
        pl.footerEnabled = m_footerCheck->isChecked();
    }

    return pl;
}

void PageLayoutWidget::setPageLayout(const PageLayout &layout)
{
    blockAllSignals(true);

    m_baseLayout = layout;
    m_masterPages = layout.masterPages;
    m_currentPageType.clear();

    // Reset combo to "All Pages"
    m_pageTypeCombo->setCurrentIndex(0);

    blockAllSignals(false);

    // Load base layout into controls
    loadPageTypeState(QString());
}

void PageLayoutWidget::onEditHeadersFooters()
{
    saveCurrentPageTypeState();

    PageLayout current = m_baseLayout;
    current.masterPages = m_masterPages;
    current.headerEnabled = m_headerCheck->isChecked();
    current.footerEnabled = m_footerCheck->isChecked();

    HeaderFooterDialog dlg(current, this);
    if (dlg.exec() == QDialog::Accepted) {
        PageLayout result = dlg.result();

        m_baseLayout.headerLeft = result.headerLeft;
        m_baseLayout.headerCenter = result.headerCenter;
        m_baseLayout.headerRight = result.headerRight;
        m_baseLayout.footerLeft = result.footerLeft;
        m_baseLayout.footerCenter = result.footerCenter;
        m_baseLayout.footerRight = result.footerRight;

        m_masterPages = result.masterPages;

        Q_EMIT pageLayoutChanged();
    }
}

bool PageLayoutWidget::isCustomSizeSelected() const
{
    return static_cast<QPageSize::PageSizeId>(m_pageSizeCombo->currentData().toInt())
           == QPageSize::Custom;
}

void PageLayoutWidget::updateCustomSizeVisibility()
{
    m_customSizeRow->setVisible(m_currentPageType.isEmpty() && isCustomSizeSelected());
}

qreal PageLayoutWidget::toMillimetres(qreal displayed) const
{
    return m_inches ? displayed * 25.4 : displayed;
}

qreal PageLayoutWidget::fromMillimetres(qreal millimetres) const
{
    return m_inches ? millimetres / 25.4 : millimetres;
}

// The spin boxes show millimetres or inches; the model is always millimetres.
void PageLayoutWidget::applyUnits()
{
    const QString suffix = m_inches ? QStringLiteral(" in") : QStringLiteral(" mm");
    const int decimals = m_inches ? 2 : 1;
    const qreal marginMax = m_inches ? 200.0 / 25.4 : 200.0;
    const qreal pageMin = m_inches ? 40.0 / 25.4 : 40.0;
    const qreal pageMax = m_inches ? 2000.0 / 25.4 : 2000.0;

    for (auto *spin : {m_marginTopSpin, m_marginBottomSpin, m_marginLeftSpin, m_marginRightSpin}) {
        spin->setSuffix(suffix);
        spin->setDecimals(decimals);
        // A master page type may still inherit (-1); the base layout may not.
        spin->setRange(spin->minimum() < 0 ? -1.0 : 0.0, marginMax);
    }
    for (auto *spin : {m_customWidthSpin, m_customHeightSpin}) {
        spin->setSuffix(suffix);
        spin->setDecimals(decimals);
        spin->setRange(pageMin, pageMax);
    }
}
