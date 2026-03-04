#ifndef PENELOPE_PARAGRAPHSTYLE_H
#define PENELOPE_PARAGRAPHSTYLE_H

#include <QFont>
#include <QString>
#include <QTextBlockFormat>
#include <QTextCharFormat>

#include "fontfeatures.h"

class ParagraphStyle
{
public:
    ParagraphStyle() = default;
    explicit ParagraphStyle(const QString &name);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Parent style for cascading hierarchy
    QString parentStyleName() const { return m_parentStyleName; }
    void setParentStyleName(const QString &name) { m_parentStyleName = name; }

    // Base character style — a character style whose text properties (font, etc.)
    // fill in unset char properties after the paragraph parent chain walk.
    QString baseCharacterStyleName() const { return m_baseCharStyleName; }
    void setBaseCharacterStyleName(const QString &name) { m_baseCharStyleName = name; }
    bool hasBaseCharacterStyle() const { return !m_baseCharStyleName.isEmpty(); }

    // Block formatting — setters
    void setAlignment(Qt::Alignment align) { m_alignment = align; m_hasAlignment = true; }
    void setSpaceBefore(qreal pts) { m_spaceBefore = pts; m_hasSpaceBefore = true; }
    void setSpaceAfter(qreal pts) { m_spaceAfter = pts; m_hasSpaceAfter = true; }
    void setLeftMargin(qreal pts) { m_leftMargin = pts; m_hasLeftMargin = true; }
    void setRightMargin(qreal pts) { m_rightMargin = pts; m_hasRightMargin = true; }
    void setLineHeightPercent(int pct) { m_lineHeightPct = pct; m_hasLineHeight = true; }
    void setBackground(const QColor &c) { m_background = c; m_hasBackground = true; }
    void setHeadingLevel(int level) { m_headingLevel = level; }
    void setFirstLineIndent(qreal pts) { m_firstLineIndent = pts; m_hasFirstLineIndent = true; }
    void setWordSpacing(qreal pts) { m_wordSpacing = pts; m_hasWordSpacing = true; }
    void setFontFeatures(FontFeatures::Features f) { m_fontFeatures = f; m_hasFontFeatures = true; }

    // Block formatting — getters
    Qt::Alignment alignment() const { return m_alignment; }
    qreal spaceBefore() const { return m_spaceBefore; }
    qreal spaceAfter() const { return m_spaceAfter; }
    qreal leftMargin() const { return m_leftMargin; }
    qreal rightMargin() const { return m_rightMargin; }
    int lineHeightPercent() const { return m_lineHeightPct; }
    QColor background() const { return m_background; }
    int headingLevel() const { return m_headingLevel; }
    qreal firstLineIndent() const { return m_firstLineIndent; }
    qreal wordSpacing() const { return m_wordSpacing; }
    FontFeatures::Features fontFeatures() const { return m_fontFeatures; }

    // Block formatting — has* flags
    bool hasAlignment() const { return m_hasAlignment; }
    bool hasSpaceBefore() const { return m_hasSpaceBefore; }
    bool hasSpaceAfter() const { return m_hasSpaceAfter; }
    bool hasLeftMargin() const { return m_hasLeftMargin; }
    bool hasRightMargin() const { return m_hasRightMargin; }
    bool hasLineHeight() const { return m_hasLineHeight; }
    bool hasBackground() const { return m_hasBackground; }
    bool hasFirstLineIndent() const { return m_hasFirstLineIndent; }
    bool hasWordSpacing() const { return m_hasWordSpacing; }
    bool hasFontFeatures() const { return m_hasFontFeatures; }

    // Character formatting (inherited by text in this paragraph) — setters
    void setFontFamily(const QString &family) { m_fontFamily = family; m_hasFontFamily = true; }
    void setFontSize(qreal pts) { m_fontSize = pts; m_hasFontSize = true; }
    void setFontWeight(QFont::Weight w) { m_fontWeight = w; m_hasFontWeight = true; }
    void setFontItalic(bool on) { m_fontItalic = on; m_hasFontItalic = true; }
    void setForeground(const QColor &c) { m_foreground = c; m_hasForeground = true; }

    // Character formatting — getters
    QString fontFamily() const { return m_fontFamily; }
    qreal fontSize() const { return m_fontSize; }
    QFont::Weight fontWeight() const { return m_fontWeight; }
    bool fontItalic() const { return m_fontItalic; }
    QColor foreground() const { return m_foreground; }

    // Character formatting — has* flags
    bool hasFontFamily() const { return m_hasFontFamily; }
    bool hasFontSize() const { return m_hasFontSize; }
    bool hasFontWeight() const { return m_hasFontWeight; }
    bool hasFontItalic() const { return m_hasFontItalic; }
    bool hasForeground() const { return m_hasForeground; }

    // Apply to QTextBlockFormat + QTextCharFormat
    void applyBlockFormat(QTextBlockFormat &bf) const;
    void applyCharFormat(QTextCharFormat &cf) const;

    // Inherit unset properties from parent
    void inheritFrom(const ParagraphStyle &parent);

private:
    QString m_name;
    QString m_parentStyleName;
    QString m_baseCharStyleName;

    // Block properties
    Qt::Alignment m_alignment = Qt::AlignLeft;
    qreal m_spaceBefore = 0;
    qreal m_spaceAfter = 0;
    qreal m_leftMargin = 0;
    qreal m_rightMargin = 0;
    int m_lineHeightPct = 100;
    QColor m_background;
    int m_headingLevel = 0;

    bool m_hasAlignment = false;
    bool m_hasSpaceBefore = false;
    bool m_hasSpaceAfter = false;
    bool m_hasLeftMargin = false;
    bool m_hasRightMargin = false;
    bool m_hasLineHeight = false;
    bool m_hasBackground = false;
    qreal m_firstLineIndent = 0;
    bool m_hasFirstLineIndent = false;
    qreal m_wordSpacing = 0;
    bool m_hasWordSpacing = false;
    FontFeatures::Features m_fontFeatures = FontFeatures::defaultFeatures();
    bool m_hasFontFeatures = false;

    // Char properties
    QString m_fontFamily;
    qreal m_fontSize = 0;
    QFont::Weight m_fontWeight = QFont::Normal;
    bool m_fontItalic = false;
    QColor m_foreground;

    bool m_hasFontFamily = false;
    bool m_hasFontSize = false;
    bool m_hasFontWeight = false;
    bool m_hasFontItalic = false;
    bool m_hasForeground = false;
};

#endif // PENELOPE_PARAGRAPHSTYLE_H
