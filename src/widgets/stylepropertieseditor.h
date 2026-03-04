#ifndef PENELOPE_STYLEPROPERTIESEDITOR_H
#define PENELOPE_STYLEPROPERTIESEDITOR_H

#include "paragraphstyle.h"
#include "characterstyle.h"
#include "fontfeatures.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QSpinBox;
class QToolBox;
class QToolButton;
class StyleManager;

class StylePropertiesEditor : public QWidget
{
    Q_OBJECT

public:
    explicit StylePropertiesEditor(QWidget *parent = nullptr);

    // Load a style for editing. The unresolved style provides has* flags;
    // resolved values are shown for inherited properties.
    void loadParagraphStyle(const ParagraphStyle &style,
                            const ParagraphStyle &resolved,
                            const QStringList &availableParents);
    void loadCharacterStyle(const CharacterStyle &style,
                            const CharacterStyle &resolved,
                            const QStringList &availableParents);

    // Apply only explicitly-set properties back to the style
    void applyToParagraphStyle(ParagraphStyle &style) const;
    void applyToCharacterStyle(CharacterStyle &style) const;

    void clear();

Q_SIGNALS:
    void propertyChanged();

private:
    void buildUI();
    void blockAllSignals(bool block);
    void updatePropertyIndicators();
    void repopulateFontStyleCombo(const QString &family);
    QString currentFontFamily() const;
    QToolButton *createResetButton();

    // Label + reset button pair for each property
    struct PropIndicator {
        QLabel *label = nullptr;
        QToolButton *resetBtn = nullptr;
        QWidget *control = nullptr; // editable widget — shown italic when inherited
    };

    // Style section
    QComboBox *m_parentCombo = nullptr;

    // Character section
    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_fontStyleCombo = nullptr;
    QDoubleSpinBox *m_sizeSpin = nullptr;
    // TODO: implement underline and strikethrough functionality
    QToolButton *m_underlineBtn = nullptr;
    QToolButton *m_strikeBtn = nullptr;
    // Section toolbox
    QToolBox *m_sectionBox = nullptr;
    QCheckBox *m_ligaturesCheck = nullptr;
    QCheckBox *m_smallCapsCheck = nullptr;
    QCheckBox *m_oldStyleNumsCheck = nullptr;
    QCheckBox *m_liningNumsCheck = nullptr;
    QCheckBox *m_kerningCheck = nullptr;
    QCheckBox *m_contextAltsCheck = nullptr;

    // Paragraph section
    int m_paragraphPageIndex = -1;
    QToolButton *m_alignLeftBtn = nullptr;
    QToolButton *m_alignCenterBtn = nullptr;
    QToolButton *m_alignRightBtn = nullptr;
    QToolButton *m_alignJustifyBtn = nullptr;
    QDoubleSpinBox *m_spaceBeforeSpin = nullptr;
    QDoubleSpinBox *m_spaceAfterSpin = nullptr;
    QSpinBox *m_lineHeightSpin = nullptr;
    QDoubleSpinBox *m_firstIndentSpin = nullptr;
    QDoubleSpinBox *m_leftMarginSpin = nullptr;
    QDoubleSpinBox *m_rightMarginSpin = nullptr;
    QDoubleSpinBox *m_wordSpacingSpin = nullptr;
    QDoubleSpinBox *m_letterSpacingSpin = nullptr;

    // Property indicators (labels + reset buttons)
    PropIndicator m_fontInd;
    PropIndicator m_fontStyleInd;
    PropIndicator m_sizeInd;
    PropIndicator m_alignInd;
    PropIndicator m_spaceBeforeInd;
    PropIndicator m_spaceAfterInd;
    PropIndicator m_lineHeightInd;
    PropIndicator m_firstIndentInd;
    PropIndicator m_leftMarginInd;
    PropIndicator m_rightMarginInd;
    PropIndicator m_wordSpacingInd;
    PropIndicator m_letterSpacingInd;

    bool m_isParagraphMode = true;

    // Track which properties are explicitly set on the loaded style
    struct ExplicitFlags {
        bool fontFamily = false;
        bool fontSize = false;
        bool fontWeight = false;
        bool fontItalic = false;
        bool fontUnderline = false;
        bool fontStrikeOut = false;
        bool alignment = false;
        bool spaceBefore = false;
        bool spaceAfter = false;
        bool lineHeight = false;
        bool firstLineIndent = false;
        bool leftMargin = false;
        bool rightMargin = false;
        bool wordSpacing = false;
        bool letterSpacing = false;
        bool fontFeatures = false;
    };
    ExplicitFlags m_explicit;

    // Resolved style values for reset functionality
    ParagraphStyle m_resolvedPara;
    CharacterStyle m_resolvedChar;
};

#endif // PENELOPE_STYLEPROPERTIESEDITOR_H
