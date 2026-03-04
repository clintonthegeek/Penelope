#include "tablestylepropertieseditor.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QToolBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

TableStylePropertiesEditor::TableStylePropertiesEditor(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
}

TableStylePropertiesEditor::BorderRow
TableStylePropertiesEditor::createBorderRow(const QString &label,
                                             QWidget *parent)
{
    Q_UNUSED(parent);
    BorderRow row;

    row.widthSpin = new QDoubleSpinBox;
    row.widthSpin->setRange(0.0, 10.0);
    row.widthSpin->setSuffix(tr(" pt"));
    row.widthSpin->setDecimals(1);
    row.widthSpin->setSingleStep(0.5);

    row.styleCombo = new QComboBox;
    row.styleCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row.styleCombo->addItem(tr("Solid"), static_cast<int>(Qt::SolidLine));
    row.styleCombo->addItem(tr("Dashed"), static_cast<int>(Qt::DashLine));
    row.styleCombo->addItem(tr("Dotted"), static_cast<int>(Qt::DotLine));

    connect(row.widthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TableStylePropertiesEditor::propertyChanged);
    connect(row.styleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TableStylePropertiesEditor::propertyChanged);

    return row;
}

void TableStylePropertiesEditor::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolBox = new QToolBox;
    layout->addWidget(toolBox, 1);

    // --- Borders page ---
    auto *borderPage = new QWidget;
    auto *borderLayout = new QVBoxLayout(borderPage);
    borderLayout->setContentsMargins(4, 4, 4, 4);
    borderLayout->setSpacing(4);

    auto addBorderRow = [&](const QString &label, BorderRow &row) {
        row = createBorderRow(label, borderPage);
        auto *hbox = new QHBoxLayout;
        hbox->addWidget(new QLabel(label));
        hbox->addWidget(row.widthSpin);
        hbox->addWidget(row.styleCombo);
        borderLayout->addLayout(hbox);
    };

    addBorderRow(tr("Outer:"), m_outerBorder);
    addBorderRow(tr("Inner:"), m_innerBorder);
    addBorderRow(tr("Header bottom:"), m_headerBottomBorder);

    borderLayout->addStretch();
    toolBox->addItem(borderPage, tr("Borders"));

    // --- Cell Padding page ---
    auto *padPage = new QWidget;
    auto *padLayout = new QVBoxLayout(padPage);
    padLayout->setContentsMargins(4, 4, 4, 4);
    padLayout->setSpacing(4);

    auto makePadSpin = [this]() {
        auto *spin = new QDoubleSpinBox;
        spin->setRange(0.0, 20.0);
        spin->setSuffix(tr(" pt"));
        spin->setDecimals(1);
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &TableStylePropertiesEditor::propertyChanged);
        return spin;
    };

    auto *padRow1 = new QHBoxLayout;
    padRow1->addWidget(new QLabel(tr("Top:")));
    m_padTopSpin = makePadSpin();
    padRow1->addWidget(m_padTopSpin);
    padRow1->addWidget(new QLabel(tr("Bottom:")));
    m_padBottomSpin = makePadSpin();
    padRow1->addWidget(m_padBottomSpin);
    padLayout->addLayout(padRow1);

    auto *padRow2 = new QHBoxLayout;
    padRow2->addWidget(new QLabel(tr("Left:")));
    m_padLeftSpin = makePadSpin();
    padRow2->addWidget(m_padLeftSpin);
    padRow2->addWidget(new QLabel(tr("Right:")));
    m_padRightSpin = makePadSpin();
    padRow2->addWidget(m_padRightSpin);
    padLayout->addLayout(padRow2);

    padLayout->addStretch();
    toolBox->addItem(padPage, tr("Cell Padding"));

    // --- Paragraph Styles page ---
    auto *paraPage = new QWidget;
    auto *paraLayout = new QVBoxLayout(paraPage);
    paraLayout->setContentsMargins(4, 4, 4, 4);
    paraLayout->setSpacing(4);

    auto *headerParaRow = new QHBoxLayout;
    headerParaRow->addWidget(new QLabel(tr("Header cells:")));
    m_headerParaCombo = new QComboBox;
    m_headerParaCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    headerParaRow->addWidget(m_headerParaCombo, 1);
    paraLayout->addLayout(headerParaRow);

    auto *bodyParaRow = new QHBoxLayout;
    bodyParaRow->addWidget(new QLabel(tr("Body cells:")));
    m_bodyParaCombo = new QComboBox;
    m_bodyParaCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    bodyParaRow->addWidget(m_bodyParaCombo, 1);
    paraLayout->addLayout(bodyParaRow);

    connect(m_headerParaCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TableStylePropertiesEditor::propertyChanged);
    connect(m_bodyParaCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TableStylePropertiesEditor::propertyChanged);

    paraLayout->addStretch();
    toolBox->addItem(paraPage, tr("Paragraph Styles"));
}

void TableStylePropertiesEditor::blockAllSignals(bool block)
{
    m_outerBorder.widthSpin->blockSignals(block);
    m_outerBorder.styleCombo->blockSignals(block);
    m_innerBorder.widthSpin->blockSignals(block);
    m_innerBorder.styleCombo->blockSignals(block);
    m_headerBottomBorder.widthSpin->blockSignals(block);
    m_headerBottomBorder.styleCombo->blockSignals(block);
    m_padTopSpin->blockSignals(block);
    m_padBottomSpin->blockSignals(block);
    m_padLeftSpin->blockSignals(block);
    m_padRightSpin->blockSignals(block);
    m_headerParaCombo->blockSignals(block);
    m_bodyParaCombo->blockSignals(block);
}

void TableStylePropertiesEditor::loadTableStyle(const TableStyle &style,
                                                  const QStringList &paraStyleNames)
{
    blockAllSignals(true);

    auto loadBorder = [](BorderRow &row, const TableStyle::Border &b) {
        row.widthSpin->setValue(b.width);
        int idx = row.styleCombo->findData(static_cast<int>(b.style));
        row.styleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    };

    loadBorder(m_outerBorder, style.outerBorder());
    loadBorder(m_innerBorder, style.innerBorder());
    loadBorder(m_headerBottomBorder, style.headerBottomBorder());

    QMarginsF pad = style.cellPadding();
    m_padTopSpin->setValue(pad.top());
    m_padBottomSpin->setValue(pad.bottom());
    m_padLeftSpin->setValue(pad.left());
    m_padRightSpin->setValue(pad.right());

    // Paragraph style combos
    m_headerParaCombo->clear();
    m_bodyParaCombo->clear();
    for (const QString &name : paraStyleNames) {
        m_headerParaCombo->addItem(name);
        m_bodyParaCombo->addItem(name);
    }
    int hIdx = m_headerParaCombo->findText(style.headerParagraphStyle());
    m_headerParaCombo->setCurrentIndex(hIdx >= 0 ? hIdx : 0);
    int bIdx = m_bodyParaCombo->findText(style.bodyParagraphStyle());
    m_bodyParaCombo->setCurrentIndex(bIdx >= 0 ? bIdx : 0);

    blockAllSignals(false);
}

void TableStylePropertiesEditor::applyToTableStyle(TableStyle &style) const
{
    auto readBorder = [](const BorderRow &row) {
        TableStyle::Border b;
        b.width = row.widthSpin->value();
        b.style = static_cast<Qt::PenStyle>(
            row.styleCombo->currentData().toInt());
        return b;
    };

    style.setOuterBorder(readBorder(m_outerBorder));
    style.setInnerBorder(readBorder(m_innerBorder));
    style.setHeaderBottomBorder(readBorder(m_headerBottomBorder));

    style.setCellPadding(QMarginsF(m_padLeftSpin->value(),
                                    m_padTopSpin->value(),
                                    m_padRightSpin->value(),
                                    m_padBottomSpin->value()));

    if (m_headerParaCombo->currentIndex() >= 0)
        style.setHeaderParagraphStyle(m_headerParaCombo->currentText());
    if (m_bodyParaCombo->currentIndex() >= 0)
        style.setBodyParagraphStyle(m_bodyParaCombo->currentText());
}

void TableStylePropertiesEditor::clear()
{
    blockAllSignals(true);

    auto clearBorder = [](BorderRow &row) {
        row.widthSpin->setValue(0.5);
        row.styleCombo->setCurrentIndex(0);
    };

    clearBorder(m_outerBorder);
    clearBorder(m_innerBorder);
    clearBorder(m_headerBottomBorder);

    m_padTopSpin->setValue(3.0);
    m_padBottomSpin->setValue(3.0);
    m_padLeftSpin->setValue(4.0);
    m_padRightSpin->setValue(4.0);

    m_headerParaCombo->clear();
    m_bodyParaCombo->clear();

    blockAllSignals(false);
}
