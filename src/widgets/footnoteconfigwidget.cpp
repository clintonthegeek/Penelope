#include "footnoteconfigwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QToolBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

FootnoteConfigWidget::FootnoteConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
}

void FootnoteConfigWidget::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolBox = new QToolBox;
    layout->addWidget(toolBox, 1);

    // --- Footnotes page ---
    auto *numPage = new QWidget;
    auto *numLayout = new QVBoxLayout(numPage);
    numLayout->setContentsMargins(4, 4, 4, 4);
    numLayout->setSpacing(4);

    auto *formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel(tr("Format:")));
    m_formatCombo = new QComboBox;
    m_formatCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_formatCombo->addItem(tr("Arabic (1, 2, 3)"),
                            static_cast<int>(FootnoteStyle::Arabic));
    m_formatCombo->addItem(tr("Roman lower (i, ii, iii)"),
                            static_cast<int>(FootnoteStyle::RomanLower));
    m_formatCombo->addItem(tr("Roman upper (I, II, III)"),
                            static_cast<int>(FootnoteStyle::RomanUpper));
    m_formatCombo->addItem(tr("Alpha lower (a, b, c)"),
                            static_cast<int>(FootnoteStyle::AlphaLower));
    m_formatCombo->addItem(tr("Alpha upper (A, B, C)"),
                            static_cast<int>(FootnoteStyle::AlphaUpper));
    m_formatCombo->addItem(tr("Symbols (*, \u2020, \u2021)"),
                            static_cast<int>(FootnoteStyle::Asterisk));
    formatRow->addWidget(m_formatCombo, 1);
    numLayout->addLayout(formatRow);

    auto *startRow = new QHBoxLayout;
    startRow->addWidget(new QLabel(tr("Start at:")));
    m_startSpin = new QSpinBox;
    m_startSpin->setRange(1, 999);
    startRow->addWidget(m_startSpin);

    startRow->addWidget(new QLabel(tr("Restart:")));
    m_restartCombo = new QComboBox;
    m_restartCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_restartCombo->addItem(tr("Per document"),
                             static_cast<int>(FootnoteStyle::PerDocument));
    m_restartCombo->addItem(tr("Per page"),
                             static_cast<int>(FootnoteStyle::PerPage));
    startRow->addWidget(m_restartCombo);
    startRow->addStretch();
    numLayout->addLayout(startRow);

    auto *fixRow = new QHBoxLayout;
    fixRow->addWidget(new QLabel(tr("Prefix:")));
    m_prefixEdit = new QLineEdit;
    m_prefixEdit->setMaximumWidth(60);
    fixRow->addWidget(m_prefixEdit);
    fixRow->addWidget(new QLabel(tr("Suffix:")));
    m_suffixEdit = new QLineEdit;
    m_suffixEdit->setMaximumWidth(60);
    fixRow->addWidget(m_suffixEdit);
    fixRow->addStretch();
    numLayout->addLayout(fixRow);

    numLayout->addStretch();
    toolBox->addItem(numPage, tr("Footnotes"));

    // --- Appearance page ---
    auto *appearPage = new QWidget;
    auto *appearLayout = new QVBoxLayout(appearPage);
    appearLayout->setContentsMargins(4, 4, 4, 4);
    appearLayout->setSpacing(4);

    m_superRefCheck = new QCheckBox(tr("Superscript references in text"));
    appearLayout->addWidget(m_superRefCheck);

    m_superNoteCheck = new QCheckBox(tr("Superscript numbers in notes"));
    appearLayout->addWidget(m_superNoteCheck);

    m_endnotesCheck = new QCheckBox(tr("Display as endnotes"));
    appearLayout->addWidget(m_endnotesCheck);

    appearLayout->addStretch();
    toolBox->addItem(appearPage, tr("Appearance"));

    // --- Separator page ---
    auto *sepPage = new QWidget;
    auto *sepLayout = new QVBoxLayout(sepPage);
    sepLayout->setContentsMargins(4, 4, 4, 4);
    sepLayout->setSpacing(4);

    m_separatorCheck = new QCheckBox(tr("Show separator line"));
    sepLayout->addWidget(m_separatorCheck);

    auto *sepRow = new QHBoxLayout;
    sepRow->addWidget(new QLabel(tr("Width:")));
    m_sepWidthSpin = new QDoubleSpinBox;
    m_sepWidthSpin->setRange(0.25, 3.0);
    m_sepWidthSpin->setSuffix(tr(" pt"));
    m_sepWidthSpin->setDecimals(2);
    m_sepWidthSpin->setSingleStep(0.25);
    sepRow->addWidget(m_sepWidthSpin);

    sepRow->addWidget(new QLabel(tr("Length:")));
    m_sepLengthSpin = new QDoubleSpinBox;
    m_sepLengthSpin->setRange(18.0, 288.0);
    m_sepLengthSpin->setSuffix(tr(" pt"));
    m_sepLengthSpin->setDecimals(0);
    m_sepLengthSpin->setSingleStep(18.0);
    sepRow->addWidget(m_sepLengthSpin);
    sepRow->addStretch();
    sepLayout->addLayout(sepRow);

    connect(m_separatorCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_sepWidthSpin->setEnabled(on);
        m_sepLengthSpin->setEnabled(on);
    });

    sepLayout->addStretch();
    toolBox->addItem(sepPage, tr("Separator"));

    // Connect all change signals
    connect(m_formatCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_startSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_restartCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_prefixEdit, &QLineEdit::textChanged,
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_suffixEdit, &QLineEdit::textChanged,
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_superRefCheck, &QCheckBox::toggled,
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_superNoteCheck, &QCheckBox::toggled,
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_endnotesCheck, &QCheckBox::toggled,
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_separatorCheck, &QCheckBox::toggled,
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_sepWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &FootnoteConfigWidget::footnoteStyleChanged);
    connect(m_sepLengthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &FootnoteConfigWidget::footnoteStyleChanged);
}

void FootnoteConfigWidget::blockAllSignals(bool block)
{
    m_formatCombo->blockSignals(block);
    m_startSpin->blockSignals(block);
    m_restartCombo->blockSignals(block);
    m_prefixEdit->blockSignals(block);
    m_suffixEdit->blockSignals(block);
    m_superRefCheck->blockSignals(block);
    m_superNoteCheck->blockSignals(block);
    m_endnotesCheck->blockSignals(block);
    m_separatorCheck->blockSignals(block);
    m_sepWidthSpin->blockSignals(block);
    m_sepLengthSpin->blockSignals(block);
}

void FootnoteConfigWidget::loadFootnoteStyle(const FootnoteStyle &style)
{
    blockAllSignals(true);

    int fmtIdx = m_formatCombo->findData(static_cast<int>(style.format));
    m_formatCombo->setCurrentIndex(fmtIdx >= 0 ? fmtIdx : 0);

    m_startSpin->setValue(style.startNumber);

    int restartIdx = m_restartCombo->findData(static_cast<int>(style.restart));
    m_restartCombo->setCurrentIndex(restartIdx >= 0 ? restartIdx : 0);

    m_prefixEdit->setText(style.prefix);
    m_suffixEdit->setText(style.suffix);

    m_superRefCheck->setChecked(style.superscriptRef);
    m_superNoteCheck->setChecked(style.superscriptNote);
    m_endnotesCheck->setChecked(style.asEndnotes);

    m_separatorCheck->setChecked(style.showSeparator);
    m_sepWidthSpin->setValue(style.separatorWidth);
    m_sepLengthSpin->setValue(style.separatorLength);
    m_sepWidthSpin->setEnabled(style.showSeparator);
    m_sepLengthSpin->setEnabled(style.showSeparator);

    blockAllSignals(false);
}

FootnoteStyle FootnoteConfigWidget::currentFootnoteStyle() const
{
    FootnoteStyle style;
    style.format = static_cast<FootnoteStyle::NumberFormat>(
        m_formatCombo->currentData().toInt());
    style.startNumber = m_startSpin->value();
    style.restart = static_cast<FootnoteStyle::RestartMode>(
        m_restartCombo->currentData().toInt());
    style.prefix = m_prefixEdit->text();
    style.suffix = m_suffixEdit->text();
    style.superscriptRef = m_superRefCheck->isChecked();
    style.superscriptNote = m_superNoteCheck->isChecked();
    style.asEndnotes = m_endnotesCheck->isChecked();
    style.showSeparator = m_separatorCheck->isChecked();
    style.separatorWidth = m_sepWidthSpin->value();
    style.separatorLength = m_sepLengthSpin->value();
    return style;
}
