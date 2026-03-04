#include "stylepropertieseditor.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QToolBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

// Find the best matching font style index in a combo populated with
// QFontDatabase::styles(). Returns 0 if nothing reasonable matches.
static int findBestStyleIndex(QComboBox *combo, const QString &family,
                              QFont::Weight weight, bool italic)
{
    // 1. Try the exact style string Qt generates
    QString target = QFontDatabase::styleString(
        QFont(family, -1, weight, italic));
    int idx = combo->findText(target);
    if (idx >= 0)
        return idx;

    // 2. Try common normal-weight names
    if (weight <= QFont::Normal && !italic) {
        for (const auto *name : {"Regular", "Normal", "Book", "Roman"}) {
            idx = combo->findText(QString::fromLatin1(name));
            if (idx >= 0)
                return idx;
        }
    }

    // 3. Try bold names
    if (weight >= QFont::Bold && !italic) {
        idx = combo->findText(QStringLiteral("Bold"));
        if (idx >= 0)
            return idx;
    }

    // 4. Try italic names
    if (italic && weight <= QFont::Normal) {
        idx = combo->findText(QStringLiteral("Italic"));
        if (idx >= 0)
            return idx;
    }

    // 5. Try bold italic
    if (weight >= QFont::Bold && italic) {
        for (const auto *name : {"Bold Italic", "BoldItalic"}) {
            idx = combo->findText(QString::fromLatin1(name));
            if (idx >= 0)
                return idx;
        }
    }

    return 0;
}

StylePropertiesEditor::StylePropertiesEditor(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
}

QToolButton *StylePropertiesEditor::createResetButton()
{
    auto *btn = new QToolButton;
    btn->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
    btn->setToolTip(tr("Reset to inherited value"));
    btn->setFixedSize(20, 20);
    btn->setAutoRaise(true);
    btn->setVisible(false);
    return btn;
}

void StylePropertiesEditor::repopulateFontStyleCombo(const QString &family)
{
    const QSignalBlocker blocker(m_fontStyleCombo);
    m_fontStyleCombo->clear();
    const QStringList styles = QFontDatabase::styles(family);
    for (const QString &s : styles)
        m_fontStyleCombo->addItem(s);
}

QString StylePropertiesEditor::currentFontFamily() const
{
    return m_fontCombo->currentFont().family();
}

void StylePropertiesEditor::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // --- Parent row (always visible) ---
    auto *parentRow = new QHBoxLayout;
    parentRow->addWidget(new QLabel(tr("Parent:")));
    m_parentCombo = new QComboBox;
    m_parentCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    parentRow->addWidget(m_parentCombo, 1);
    layout->addLayout(parentRow);

    connect(m_parentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { Q_EMIT propertyChanged(); });

    // --- Section toolbox ---
    m_sectionBox = new QToolBox;
    layout->addWidget(m_sectionBox, 1);

    // --- Character page ---
    auto *charPage = new QWidget;
    auto *charLayout = new QVBoxLayout(charPage);
    charLayout->setContentsMargins(4, 4, 4, 4);
    charLayout->setSpacing(4);

    // Font family row
    auto *fontRow = new QHBoxLayout;
    m_fontInd.label = new QLabel(tr("Font:"));
    m_fontInd.resetBtn = createResetButton();
    m_fontCombo = new QFontComboBox;
    m_fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontInd.control = m_fontCombo;
    fontRow->addWidget(m_fontInd.label);
    fontRow->addWidget(m_fontCombo, 1);
    fontRow->addWidget(m_fontInd.resetBtn);
    charLayout->addLayout(fontRow);

    // Font style variant row
    auto *fontStyleRow = new QHBoxLayout;
    m_fontStyleInd.label = new QLabel(tr("Style:"));
    m_fontStyleInd.resetBtn = createResetButton();
    m_fontStyleCombo = new QComboBox;
    m_fontStyleCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontStyleInd.control = m_fontStyleCombo;
    fontStyleRow->addWidget(m_fontStyleInd.label);
    fontStyleRow->addWidget(m_fontStyleCombo, 1);
    fontStyleRow->addWidget(m_fontStyleInd.resetBtn);
    charLayout->addLayout(fontStyleRow);

    // Repopulate style combo when font family changes
    connect(m_fontCombo, &QFontComboBox::currentFontChanged,
            this, [this](const QFont &font) {
        repopulateFontStyleCombo(font.family());
    });

    // Size row with disabled U/S buttons
    auto *sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(4);
    m_sizeInd.label = new QLabel(tr("Size:"));
    m_sizeInd.resetBtn = createResetButton();
    m_sizeSpin = new QDoubleSpinBox;
    m_sizeInd.control = m_sizeSpin;
    m_sizeSpin->setRange(1.0, 200.0);
    m_sizeSpin->setSuffix(tr("pt"));
    m_sizeSpin->setDecimals(1);

    // TODO: implement underline and strikethrough functionality in the
    // document builder (QTextCharFormat supports them, but the markdown
    // rendering pipeline does not apply them yet)
    auto makeDisabledToggle = [](const QString &text, const QString &tooltip,
                                  bool underline, bool strike) {
        auto *btn = new QToolButton;
        btn->setText(text);
        btn->setCheckable(true);
        btn->setFixedSize(28, 28);
        btn->setEnabled(false);
        btn->setToolTip(tooltip);
        QFont f;
        if (underline) f.setUnderline(true);
        if (strike) f.setStrikeOut(true);
        btn->setFont(f);
        return btn;
    };

    m_underlineBtn = makeDisabledToggle(tr("U"), tr("Underline (not yet implemented)"), true, false);
    m_strikeBtn = makeDisabledToggle(tr("S"), tr("Strikethrough (not yet implemented)"), false, true);

    sizeRow->addWidget(m_sizeInd.label);
    sizeRow->addWidget(m_sizeSpin);
    sizeRow->addWidget(m_underlineBtn);
    sizeRow->addWidget(m_strikeBtn);
    sizeRow->addWidget(m_sizeInd.resetBtn);
    charLayout->addLayout(sizeRow);

    // Connect character change signals — mark property as explicit
    connect(m_fontCombo, &QFontComboBox::currentFontChanged,
            this, [this]() {
        m_explicit.fontFamily = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_fontStyleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() {
        m_explicit.fontWeight = true;
        m_explicit.fontItalic = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_sizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.fontSize = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    // Connect reset buttons for character properties
    connect(m_fontInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.fontFamily = false;
        blockAllSignals(true);
        const QString family = m_isParagraphMode
            ? m_resolvedPara.fontFamily() : m_resolvedChar.fontFamily();
        m_fontCombo->setCurrentFont(QFont(family));
        repopulateFontStyleCombo(family);
        if (!m_explicit.fontWeight && !m_explicit.fontItalic) {
            auto w = m_isParagraphMode ? m_resolvedPara.fontWeight() : m_resolvedChar.fontWeight();
            bool it = m_isParagraphMode ? m_resolvedPara.fontItalic() : m_resolvedChar.fontItalic();
            m_fontStyleCombo->setCurrentIndex(
                findBestStyleIndex(m_fontStyleCombo, family, w, it));
        }
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });

    connect(m_fontStyleInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.fontWeight = false;
        m_explicit.fontItalic = false;
        blockAllSignals(true);
        const QString family = currentFontFamily();
        auto w = m_isParagraphMode ? m_resolvedPara.fontWeight() : m_resolvedChar.fontWeight();
        bool it = m_isParagraphMode ? m_resolvedPara.fontItalic() : m_resolvedChar.fontItalic();
        m_fontStyleCombo->setCurrentIndex(
            findBestStyleIndex(m_fontStyleCombo, family, w, it));
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });

    connect(m_sizeInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.fontSize = false;
        blockAllSignals(true);
        m_sizeSpin->setValue(m_isParagraphMode
            ? m_resolvedPara.fontSize() : m_resolvedChar.fontSize());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });

    charLayout->addStretch();
    m_sectionBox->addItem(charPage, tr("Character"));

    // --- OpenType Features page ---
    auto *ffPage = new QWidget;
    auto *ffLayout = new QVBoxLayout(ffPage);
    ffLayout->setContentsMargins(4, 4, 4, 4);
    ffLayout->setSpacing(2);

    auto *ffRow1 = new QHBoxLayout;
    m_ligaturesCheck = new QCheckBox(tr("Ligatures"));
    m_ligaturesCheck->setToolTip(tr("Standard ligatures (fi, fl, ff)"));
    m_smallCapsCheck = new QCheckBox(tr("Small Caps"));
    m_smallCapsCheck->setToolTip(tr("Display lowercase as small capitals"));
    ffRow1->addWidget(m_ligaturesCheck);
    ffRow1->addWidget(m_smallCapsCheck);
    ffRow1->addStretch();
    ffLayout->addLayout(ffRow1);

    auto *ffRow2 = new QHBoxLayout;
    m_oldStyleNumsCheck = new QCheckBox(tr("Old-Style Nums"));
    m_oldStyleNumsCheck->setToolTip(tr("Old-style (text) numerals"));
    m_liningNumsCheck = new QCheckBox(tr("Lining Nums"));
    m_liningNumsCheck->setToolTip(tr("Lining (tabular) numerals"));
    ffRow2->addWidget(m_oldStyleNumsCheck);
    ffRow2->addWidget(m_liningNumsCheck);
    ffRow2->addStretch();
    ffLayout->addLayout(ffRow2);

    auto *ffRow3 = new QHBoxLayout;
    m_kerningCheck = new QCheckBox(tr("Kerning"));
    m_kerningCheck->setToolTip(tr("Pairwise kerning adjustments"));
    m_contextAltsCheck = new QCheckBox(tr("Contextual Alts"));
    m_contextAltsCheck->setToolTip(tr("Contextual alternates"));
    ffRow3->addWidget(m_kerningCheck);
    ffRow3->addWidget(m_contextAltsCheck);
    ffRow3->addStretch();
    ffLayout->addLayout(ffRow3);

    // Connect font feature change signals
    auto onFfChanged = [this]() {
        m_explicit.fontFeatures = true;
        Q_EMIT propertyChanged();
    };
    connect(m_ligaturesCheck, &QCheckBox::toggled, this, onFfChanged);
    connect(m_smallCapsCheck, &QCheckBox::toggled, this, onFfChanged);
    connect(m_oldStyleNumsCheck, &QCheckBox::toggled, this, onFfChanged);
    connect(m_liningNumsCheck, &QCheckBox::toggled, this, onFfChanged);
    connect(m_kerningCheck, &QCheckBox::toggled, this, onFfChanged);
    connect(m_contextAltsCheck, &QCheckBox::toggled, this, onFfChanged);

    ffLayout->addStretch();
    m_sectionBox->addItem(ffPage, tr("OpenType Features"));

    // --- Paragraph page ---
    auto *paraPage = new QWidget;
    auto *paraLayout = new QVBoxLayout(paraPage);
    paraLayout->setContentsMargins(4, 4, 4, 4);
    paraLayout->setSpacing(4);

    // Alignment row
    auto *alignRow = new QHBoxLayout;
    alignRow->setSpacing(2);
    m_alignInd.label = new QLabel(tr("Align:"));
    m_alignInd.resetBtn = createResetButton();

    m_alignLeftBtn = new QToolButton;
    m_alignLeftBtn->setIcon(QIcon::fromTheme(QStringLiteral("format-justify-left")));
    m_alignLeftBtn->setCheckable(true);
    m_alignLeftBtn->setFixedSize(28, 28);
    m_alignLeftBtn->setChecked(true);

    m_alignCenterBtn = new QToolButton;
    m_alignCenterBtn->setIcon(QIcon::fromTheme(QStringLiteral("format-justify-center")));
    m_alignCenterBtn->setCheckable(true);
    m_alignCenterBtn->setFixedSize(28, 28);

    m_alignRightBtn = new QToolButton;
    m_alignRightBtn->setIcon(QIcon::fromTheme(QStringLiteral("format-justify-right")));
    m_alignRightBtn->setCheckable(true);
    m_alignRightBtn->setFixedSize(28, 28);

    m_alignJustifyBtn = new QToolButton;
    m_alignJustifyBtn->setIcon(QIcon::fromTheme(QStringLiteral("format-justify-fill")));
    m_alignJustifyBtn->setCheckable(true);
    m_alignJustifyBtn->setFixedSize(28, 28);

    alignRow->addWidget(m_alignInd.label);
    alignRow->addWidget(m_alignLeftBtn);
    alignRow->addWidget(m_alignCenterBtn);
    alignRow->addWidget(m_alignRightBtn);
    alignRow->addWidget(m_alignJustifyBtn);
    alignRow->addWidget(m_alignInd.resetBtn);
    alignRow->addStretch();
    paraLayout->addLayout(alignRow);

    auto setAlignment = [this](QToolButton *active) {
        blockAllSignals(true);
        m_alignLeftBtn->setChecked(active == m_alignLeftBtn);
        m_alignCenterBtn->setChecked(active == m_alignCenterBtn);
        m_alignRightBtn->setChecked(active == m_alignRightBtn);
        m_alignJustifyBtn->setChecked(active == m_alignJustifyBtn);
        blockAllSignals(false);
        m_explicit.alignment = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    };

    connect(m_alignLeftBtn, &QToolButton::clicked,
            this, [this, setAlignment]() { setAlignment(m_alignLeftBtn); });
    connect(m_alignCenterBtn, &QToolButton::clicked,
            this, [this, setAlignment]() { setAlignment(m_alignCenterBtn); });
    connect(m_alignRightBtn, &QToolButton::clicked,
            this, [this, setAlignment]() { setAlignment(m_alignRightBtn); });
    connect(m_alignJustifyBtn, &QToolButton::clicked,
            this, [this, setAlignment]() { setAlignment(m_alignJustifyBtn); });

    connect(m_alignInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.alignment = false;
        blockAllSignals(true);
        Qt::Alignment align = m_resolvedPara.alignment();
        m_alignLeftBtn->setChecked(align == Qt::AlignLeft);
        m_alignCenterBtn->setChecked(align == Qt::AlignCenter || align == Qt::AlignHCenter);
        m_alignRightBtn->setChecked(align == Qt::AlignRight);
        m_alignJustifyBtn->setChecked(align == Qt::AlignJustify);
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });

    // Spacing row
    auto *spaceRow = new QHBoxLayout;
    m_spaceBeforeInd.label = new QLabel(tr("Before:"));
    m_spaceBeforeInd.resetBtn = createResetButton();
    m_spaceBeforeSpin = new QDoubleSpinBox;
    m_spaceBeforeInd.control = m_spaceBeforeSpin;
    m_spaceBeforeSpin->setRange(0.0, 100.0);
    m_spaceBeforeSpin->setSuffix(tr("pt"));
    m_spaceBeforeSpin->setDecimals(1);

    m_spaceAfterInd.label = new QLabel(tr("After:"));
    m_spaceAfterInd.resetBtn = createResetButton();
    m_spaceAfterSpin = new QDoubleSpinBox;
    m_spaceAfterInd.control = m_spaceAfterSpin;
    m_spaceAfterSpin->setRange(0.0, 100.0);
    m_spaceAfterSpin->setSuffix(tr("pt"));
    m_spaceAfterSpin->setDecimals(1);

    spaceRow->addWidget(m_spaceBeforeInd.label);
    spaceRow->addWidget(m_spaceBeforeSpin);
    spaceRow->addWidget(m_spaceBeforeInd.resetBtn);
    spaceRow->addWidget(m_spaceAfterInd.label);
    spaceRow->addWidget(m_spaceAfterSpin);
    spaceRow->addWidget(m_spaceAfterInd.resetBtn);
    paraLayout->addLayout(spaceRow);

    // Line height row
    auto *lhRow = new QHBoxLayout;
    m_lineHeightInd.label = new QLabel(tr("Line ht:"));
    m_lineHeightInd.resetBtn = createResetButton();
    m_lineHeightSpin = new QSpinBox;
    m_lineHeightInd.control = m_lineHeightSpin;
    m_lineHeightSpin->setRange(100, 300);
    m_lineHeightSpin->setSuffix(QStringLiteral("%"));
    lhRow->addWidget(m_lineHeightInd.label);
    lhRow->addWidget(m_lineHeightSpin);
    lhRow->addWidget(m_lineHeightInd.resetBtn);
    paraLayout->addLayout(lhRow);

    // Indent row
    auto *indentRow = new QHBoxLayout;
    m_firstIndentInd.label = new QLabel(tr("1st indent:"));
    m_firstIndentInd.resetBtn = createResetButton();
    m_firstIndentSpin = new QDoubleSpinBox;
    m_firstIndentInd.control = m_firstIndentSpin;
    m_firstIndentSpin->setRange(0.0, 72.0);
    m_firstIndentSpin->setSuffix(tr("pt"));
    m_firstIndentSpin->setDecimals(1);
    indentRow->addWidget(m_firstIndentInd.label);
    indentRow->addWidget(m_firstIndentSpin);
    indentRow->addWidget(m_firstIndentInd.resetBtn);
    paraLayout->addLayout(indentRow);

    // Margins row
    auto *marginRow = new QHBoxLayout;
    m_leftMarginInd.label = new QLabel(tr("L margin:"));
    m_leftMarginInd.resetBtn = createResetButton();
    m_leftMarginSpin = new QDoubleSpinBox;
    m_leftMarginInd.control = m_leftMarginSpin;
    m_leftMarginSpin->setRange(0.0, 100.0);
    m_leftMarginSpin->setSuffix(tr("pt"));
    m_leftMarginSpin->setDecimals(1);

    m_rightMarginInd.label = new QLabel(tr("R:"));
    m_rightMarginInd.resetBtn = createResetButton();
    m_rightMarginSpin = new QDoubleSpinBox;
    m_rightMarginInd.control = m_rightMarginSpin;
    m_rightMarginSpin->setRange(0.0, 100.0);
    m_rightMarginSpin->setSuffix(tr("pt"));
    m_rightMarginSpin->setDecimals(1);

    marginRow->addWidget(m_leftMarginInd.label);
    marginRow->addWidget(m_leftMarginSpin);
    marginRow->addWidget(m_leftMarginInd.resetBtn);
    marginRow->addWidget(m_rightMarginInd.label);
    marginRow->addWidget(m_rightMarginSpin);
    marginRow->addWidget(m_rightMarginInd.resetBtn);
    paraLayout->addLayout(marginRow);

    // Word/letter spacing row
    auto *spacingRow = new QHBoxLayout;
    m_wordSpacingInd.label = new QLabel(tr("Word sp:"));
    m_wordSpacingInd.resetBtn = createResetButton();
    m_wordSpacingSpin = new QDoubleSpinBox;
    m_wordSpacingInd.control = m_wordSpacingSpin;
    m_wordSpacingSpin->setRange(-5.0, 20.0);
    m_wordSpacingSpin->setSuffix(tr("pt"));
    m_wordSpacingSpin->setDecimals(1);

    m_letterSpacingInd.label = new QLabel(tr("Letter:"));
    m_letterSpacingInd.resetBtn = createResetButton();
    m_letterSpacingSpin = new QDoubleSpinBox;
    m_letterSpacingInd.control = m_letterSpacingSpin;
    m_letterSpacingSpin->setRange(-5.0, 20.0);
    m_letterSpacingSpin->setSuffix(tr("pt"));
    m_letterSpacingSpin->setDecimals(1);

    spacingRow->addWidget(m_wordSpacingInd.label);
    spacingRow->addWidget(m_wordSpacingSpin);
    spacingRow->addWidget(m_wordSpacingInd.resetBtn);
    spacingRow->addWidget(m_letterSpacingInd.label);
    spacingRow->addWidget(m_letterSpacingSpin);
    spacingRow->addWidget(m_letterSpacingInd.resetBtn);
    paraLayout->addLayout(spacingRow);

    // Connect paragraph change signals — mark property as explicit
    connect(m_spaceBeforeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.spaceBefore = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_spaceAfterSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.spaceAfter = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_lineHeightSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this]() {
        m_explicit.lineHeight = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_firstIndentSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.firstLineIndent = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_leftMarginSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.leftMargin = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_rightMarginSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.rightMargin = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_wordSpacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.wordSpacing = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_letterSpacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() {
        m_explicit.letterSpacing = true;
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });

    // Connect reset buttons for paragraph properties
    connect(m_spaceBeforeInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.spaceBefore = false;
        blockAllSignals(true);
        m_spaceBeforeSpin->setValue(m_resolvedPara.spaceBefore());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_spaceAfterInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.spaceAfter = false;
        blockAllSignals(true);
        m_spaceAfterSpin->setValue(m_resolvedPara.spaceAfter());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_lineHeightInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.lineHeight = false;
        blockAllSignals(true);
        m_lineHeightSpin->setValue(m_resolvedPara.lineHeightPercent());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_firstIndentInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.firstLineIndent = false;
        blockAllSignals(true);
        m_firstIndentSpin->setValue(m_resolvedPara.firstLineIndent());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_leftMarginInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.leftMargin = false;
        blockAllSignals(true);
        m_leftMarginSpin->setValue(m_resolvedPara.leftMargin());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_rightMarginInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.rightMargin = false;
        blockAllSignals(true);
        m_rightMarginSpin->setValue(m_resolvedPara.rightMargin());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_wordSpacingInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.wordSpacing = false;
        blockAllSignals(true);
        m_wordSpacingSpin->setValue(m_resolvedPara.wordSpacing());
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });
    connect(m_letterSpacingInd.resetBtn, &QToolButton::clicked, this, [this]() {
        m_explicit.letterSpacing = false;
        blockAllSignals(true);
        m_letterSpacingSpin->setValue(0);
        blockAllSignals(false);
        updatePropertyIndicators();
        Q_EMIT propertyChanged();
    });

    paraLayout->addStretch();
    m_paragraphPageIndex = m_sectionBox->addItem(paraPage, tr("Paragraph"));
}

void StylePropertiesEditor::blockAllSignals(bool block)
{
    m_parentCombo->blockSignals(block);
    m_fontCombo->blockSignals(block);
    m_fontStyleCombo->blockSignals(block);
    m_sizeSpin->blockSignals(block);
    m_underlineBtn->blockSignals(block);
    m_strikeBtn->blockSignals(block);
    m_alignLeftBtn->blockSignals(block);
    m_alignCenterBtn->blockSignals(block);
    m_alignRightBtn->blockSignals(block);
    m_alignJustifyBtn->blockSignals(block);
    m_spaceBeforeSpin->blockSignals(block);
    m_spaceAfterSpin->blockSignals(block);
    m_lineHeightSpin->blockSignals(block);
    m_firstIndentSpin->blockSignals(block);
    m_leftMarginSpin->blockSignals(block);
    m_rightMarginSpin->blockSignals(block);
    m_wordSpacingSpin->blockSignals(block);
    m_letterSpacingSpin->blockSignals(block);
    m_ligaturesCheck->blockSignals(block);
    m_smallCapsCheck->blockSignals(block);
    m_oldStyleNumsCheck->blockSignals(block);
    m_liningNumsCheck->blockSignals(block);
    m_kerningCheck->blockSignals(block);
    m_contextAltsCheck->blockSignals(block);
}

void StylePropertiesEditor::updatePropertyIndicators()
{
    auto setIndicator = [](PropIndicator &ind, bool isExplicit) {
        if (ind.control) {
            if (auto *combo = qobject_cast<QComboBox *>(ind.control)) {
                // Italicize only the current display, not the dropdown items
                QFont f = combo->font();
                f.setItalic(!isExplicit);
                combo->setFont(f);
                if (auto *view = combo->view()) {
                    QFont vf = view->font();
                    vf.setItalic(false);
                    view->setFont(vf);
                }
            } else {
                QFont f = ind.control->font();
                f.setItalic(!isExplicit);
                ind.control->setFont(f);
            }
        }
        if (ind.resetBtn)
            ind.resetBtn->setVisible(isExplicit);
    };

    setIndicator(m_fontInd, m_explicit.fontFamily);
    setIndicator(m_fontStyleInd, m_explicit.fontWeight || m_explicit.fontItalic);
    setIndicator(m_sizeInd, m_explicit.fontSize);
    if (m_isParagraphMode) {
        setIndicator(m_alignInd, m_explicit.alignment);
        setIndicator(m_spaceBeforeInd, m_explicit.spaceBefore);
        setIndicator(m_spaceAfterInd, m_explicit.spaceAfter);
        setIndicator(m_lineHeightInd, m_explicit.lineHeight);
        setIndicator(m_firstIndentInd, m_explicit.firstLineIndent);
        setIndicator(m_leftMarginInd, m_explicit.leftMargin);
        setIndicator(m_rightMarginInd, m_explicit.rightMargin);
        setIndicator(m_wordSpacingInd, m_explicit.wordSpacing);
        setIndicator(m_letterSpacingInd, m_explicit.letterSpacing);
    }
}

void StylePropertiesEditor::loadParagraphStyle(const ParagraphStyle &style,
                                                const ParagraphStyle &resolved,
                                                const QStringList &availableParents)
{
    blockAllSignals(true);
    m_isParagraphMode = true;
    m_sectionBox->setItemEnabled(m_paragraphPageIndex, true);

    // Store resolved style for reset functionality
    m_resolvedPara = resolved;

    // Store which properties are explicitly set on this (unresolved) style
    m_explicit = {};
    m_explicit.fontFamily = style.hasFontFamily();
    m_explicit.fontSize = style.hasFontSize();
    m_explicit.fontWeight = style.hasFontWeight();
    m_explicit.fontItalic = style.hasFontItalic();
    m_explicit.alignment = style.hasAlignment();
    m_explicit.spaceBefore = style.hasSpaceBefore();
    m_explicit.spaceAfter = style.hasSpaceAfter();
    m_explicit.lineHeight = style.hasLineHeight();
    m_explicit.firstLineIndent = style.hasFirstLineIndent();
    m_explicit.leftMargin = style.hasLeftMargin();
    m_explicit.rightMargin = style.hasRightMargin();
    m_explicit.wordSpacing = style.hasWordSpacing();
    m_explicit.fontFeatures = style.hasFontFeatures();

    // Parent combo
    m_parentCombo->clear();
    m_parentCombo->addItem(tr("(none)"), QString());
    for (const QString &p : availableParents)
        m_parentCombo->addItem(p, p);
    int parentIdx = m_parentCombo->findData(style.parentStyleName());
    m_parentCombo->setCurrentIndex(parentIdx >= 0 ? parentIdx : 0);

    // Show resolved values in all controls so the user sees effective values
    m_fontCombo->setCurrentFont(QFont(resolved.fontFamily()));
    // Populate style combo for this font family
    repopulateFontStyleCombo(resolved.fontFamily());
    // Select the best matching style
    m_fontStyleCombo->setCurrentIndex(
        findBestStyleIndex(m_fontStyleCombo, resolved.fontFamily(),
                           resolved.fontWeight(), resolved.fontItalic()));

    m_sizeSpin->setValue(resolved.fontSize());
    m_underlineBtn->setChecked(false);
    m_strikeBtn->setChecked(false);
    // Paragraph properties
    Qt::Alignment align = resolved.alignment();
    m_alignLeftBtn->setChecked(align == Qt::AlignLeft);
    m_alignCenterBtn->setChecked(align == Qt::AlignCenter || align == Qt::AlignHCenter);
    m_alignRightBtn->setChecked(align == Qt::AlignRight);
    m_alignJustifyBtn->setChecked(align == Qt::AlignJustify);

    m_spaceBeforeSpin->setValue(resolved.spaceBefore());
    m_spaceAfterSpin->setValue(resolved.spaceAfter());
    m_lineHeightSpin->setValue(resolved.lineHeightPercent());
    m_firstIndentSpin->setValue(resolved.firstLineIndent());
    m_leftMarginSpin->setValue(resolved.leftMargin());
    m_rightMarginSpin->setValue(resolved.rightMargin());
    m_wordSpacingSpin->setValue(resolved.wordSpacing());
    m_letterSpacingSpin->setValue(0);

    // Font features
    FontFeatures::Features ff = resolved.hasFontFeatures()
        ? resolved.fontFeatures() : FontFeatures::defaultFeatures();
    m_ligaturesCheck->setChecked(ff & FontFeatures::Ligatures);
    m_smallCapsCheck->setChecked(ff & FontFeatures::SmallCaps);
    m_oldStyleNumsCheck->setChecked(ff & FontFeatures::OldStyleNums);
    m_liningNumsCheck->setChecked(ff & FontFeatures::LiningNums);
    m_kerningCheck->setChecked(ff & FontFeatures::Kerning);
    m_contextAltsCheck->setChecked(ff & FontFeatures::ContextAlts);

    blockAllSignals(false);
    updatePropertyIndicators();
}

void StylePropertiesEditor::loadCharacterStyle(const CharacterStyle &style,
                                                const CharacterStyle &resolved,
                                                const QStringList &availableParents)
{
    blockAllSignals(true);
    m_isParagraphMode = false;
    m_sectionBox->setItemEnabled(m_paragraphPageIndex, false);

    // Store resolved style for reset functionality
    m_resolvedChar = resolved;

    // Store which properties are explicitly set
    m_explicit = {};
    m_explicit.fontFamily = style.hasFontFamily();
    m_explicit.fontSize = style.hasFontSize();
    m_explicit.fontWeight = style.hasFontWeight();
    m_explicit.fontItalic = style.hasFontItalic();
    m_explicit.fontUnderline = style.hasFontUnderline();
    m_explicit.fontStrikeOut = style.hasFontStrikeOut();
    m_explicit.letterSpacing = style.hasLetterSpacing();
    m_explicit.fontFeatures = style.hasFontFeatures();

    // Parent combo
    m_parentCombo->clear();
    m_parentCombo->addItem(tr("(none)"), QString());
    for (const QString &p : availableParents)
        m_parentCombo->addItem(p, p);
    int parentIdx = m_parentCombo->findData(style.parentStyleName());
    m_parentCombo->setCurrentIndex(parentIdx >= 0 ? parentIdx : 0);

    // Show resolved values
    m_fontCombo->setCurrentFont(QFont(resolved.fontFamily()));
    repopulateFontStyleCombo(resolved.fontFamily());
    m_fontStyleCombo->setCurrentIndex(
        findBestStyleIndex(m_fontStyleCombo, resolved.fontFamily(),
                           resolved.fontWeight(), resolved.fontItalic()));

    m_sizeSpin->setValue(resolved.fontSize());
    m_underlineBtn->setChecked(resolved.fontUnderline());
    m_strikeBtn->setChecked(resolved.fontStrikeOut());
    // Font features
    FontFeatures::Features ff = resolved.hasFontFeatures()
        ? resolved.fontFeatures() : FontFeatures::defaultFeatures();
    m_ligaturesCheck->setChecked(ff & FontFeatures::Ligatures);
    m_smallCapsCheck->setChecked(ff & FontFeatures::SmallCaps);
    m_oldStyleNumsCheck->setChecked(ff & FontFeatures::OldStyleNums);
    m_liningNumsCheck->setChecked(ff & FontFeatures::LiningNums);
    m_kerningCheck->setChecked(ff & FontFeatures::Kerning);
    m_contextAltsCheck->setChecked(ff & FontFeatures::ContextAlts);

    blockAllSignals(false);
    updatePropertyIndicators();
}

void StylePropertiesEditor::applyToParagraphStyle(ParagraphStyle &style) const
{
    // Parent always gets set
    style.setParentStyleName(m_parentCombo->currentData().toString());

    // Only set properties that are explicitly flagged
    if (m_explicit.fontFamily)
        style.setFontFamily(currentFontFamily());
    if (m_explicit.fontSize)
        style.setFontSize(m_sizeSpin->value());
    if (m_explicit.fontWeight) {
        QString family = m_fontCombo->currentFont().family();
        QString styleName = m_fontStyleCombo->currentText();
        QFont f = QFontDatabase::font(family, styleName, 12);
        style.setFontWeight(static_cast<QFont::Weight>(f.weight()));
    }
    if (m_explicit.fontItalic) {
        QString family = m_fontCombo->currentFont().family();
        QString styleName = m_fontStyleCombo->currentText();
        QFont f = QFontDatabase::font(family, styleName, 12);
        style.setFontItalic(f.italic());
    }
    // Paragraph properties
    if (m_explicit.alignment) {
        if (m_alignJustifyBtn->isChecked())
            style.setAlignment(Qt::AlignJustify);
        else if (m_alignCenterBtn->isChecked())
            style.setAlignment(Qt::AlignCenter);
        else if (m_alignRightBtn->isChecked())
            style.setAlignment(Qt::AlignRight);
        else
            style.setAlignment(Qt::AlignLeft);
    }

    if (m_explicit.spaceBefore)
        style.setSpaceBefore(m_spaceBeforeSpin->value());
    if (m_explicit.spaceAfter)
        style.setSpaceAfter(m_spaceAfterSpin->value());
    if (m_explicit.lineHeight)
        style.setLineHeightPercent(m_lineHeightSpin->value());
    if (m_explicit.firstLineIndent)
        style.setFirstLineIndent(m_firstIndentSpin->value());
    if (m_explicit.leftMargin)
        style.setLeftMargin(m_leftMarginSpin->value());
    if (m_explicit.rightMargin)
        style.setRightMargin(m_rightMarginSpin->value());
    if (m_explicit.wordSpacing)
        style.setWordSpacing(m_wordSpacingSpin->value());
    if (m_explicit.fontFeatures) {
        FontFeatures::Features ff;
        if (m_ligaturesCheck->isChecked())  ff |= FontFeatures::Ligatures;
        if (m_smallCapsCheck->isChecked())   ff |= FontFeatures::SmallCaps;
        if (m_oldStyleNumsCheck->isChecked())ff |= FontFeatures::OldStyleNums;
        if (m_liningNumsCheck->isChecked())  ff |= FontFeatures::LiningNums;
        if (m_kerningCheck->isChecked())     ff |= FontFeatures::Kerning;
        if (m_contextAltsCheck->isChecked()) ff |= FontFeatures::ContextAlts;
        style.setFontFeatures(ff);
    }
}

void StylePropertiesEditor::applyToCharacterStyle(CharacterStyle &style) const
{
    // Parent always gets set
    style.setParentStyleName(m_parentCombo->currentData().toString());

    // Only set properties that are explicitly flagged
    if (m_explicit.fontFamily)
        style.setFontFamily(currentFontFamily());
    if (m_explicit.fontSize)
        style.setFontSize(m_sizeSpin->value());
    if (m_explicit.fontWeight) {
        QString family = m_fontCombo->currentFont().family();
        QString styleName = m_fontStyleCombo->currentText();
        QFont f = QFontDatabase::font(family, styleName, 12);
        style.setFontWeight(static_cast<QFont::Weight>(f.weight()));
    }
    if (m_explicit.fontItalic) {
        QString family = m_fontCombo->currentFont().family();
        QString styleName = m_fontStyleCombo->currentText();
        QFont f = QFontDatabase::font(family, styleName, 12);
        style.setFontItalic(f.italic());
    }
    if (m_explicit.fontUnderline)
        style.setFontUnderline(m_underlineBtn->isChecked());
    if (m_explicit.fontStrikeOut)
        style.setFontStrikeOut(m_strikeBtn->isChecked());
    if (m_explicit.letterSpacing)
        style.setLetterSpacing(m_letterSpacingSpin->value());
    if (m_explicit.fontFeatures) {
        FontFeatures::Features ff;
        if (m_ligaturesCheck->isChecked())  ff |= FontFeatures::Ligatures;
        if (m_smallCapsCheck->isChecked())   ff |= FontFeatures::SmallCaps;
        if (m_oldStyleNumsCheck->isChecked())ff |= FontFeatures::OldStyleNums;
        if (m_liningNumsCheck->isChecked())  ff |= FontFeatures::LiningNums;
        if (m_kerningCheck->isChecked())     ff |= FontFeatures::Kerning;
        if (m_contextAltsCheck->isChecked()) ff |= FontFeatures::ContextAlts;
        style.setFontFeatures(ff);
    }
}

void StylePropertiesEditor::clear()
{
    blockAllSignals(true);
    m_parentCombo->clear();
    m_fontCombo->setCurrentFont(QFont());
    m_sizeSpin->setValue(11.0);
    m_underlineBtn->setChecked(false);
    m_strikeBtn->setChecked(false);
    m_sectionBox->setItemEnabled(m_paragraphPageIndex, true);
    m_explicit = {};
    m_resolvedPara = ParagraphStyle();
    m_resolvedChar = CharacterStyle();
    blockAllSignals(false);
    updatePropertyIndicators();
}
