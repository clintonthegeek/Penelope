#include "paragraphstyle.h"
#include "fontfeatures.h"

ParagraphStyle::ParagraphStyle(const QString &name)
    : m_name(name)
{
}

void ParagraphStyle::inheritFrom(const ParagraphStyle &parent)
{
    if (!m_hasAlignment)    { m_alignment = parent.m_alignment; m_hasAlignment = parent.m_hasAlignment; }
    if (!m_hasSpaceBefore)  { m_spaceBefore = parent.m_spaceBefore; m_hasSpaceBefore = parent.m_hasSpaceBefore; }
    if (!m_hasSpaceAfter)   { m_spaceAfter = parent.m_spaceAfter; m_hasSpaceAfter = parent.m_hasSpaceAfter; }
    if (!m_hasLeftMargin)   { m_leftMargin = parent.m_leftMargin; m_hasLeftMargin = parent.m_hasLeftMargin; }
    if (!m_hasRightMargin)  { m_rightMargin = parent.m_rightMargin; m_hasRightMargin = parent.m_hasRightMargin; }
    if (!m_hasLineHeight)   { m_lineHeightPct = parent.m_lineHeightPct; m_hasLineHeight = parent.m_hasLineHeight; }
    if (!m_hasBackground)   { m_background = parent.m_background; m_hasBackground = parent.m_hasBackground; }
    if (!m_hasFontFamily)   { m_fontFamily = parent.m_fontFamily; m_hasFontFamily = parent.m_hasFontFamily; }
    if (!m_hasFontSize)     { m_fontSize = parent.m_fontSize; m_hasFontSize = parent.m_hasFontSize; }
    if (!m_hasFontWeight)   { m_fontWeight = parent.m_fontWeight; m_hasFontWeight = parent.m_hasFontWeight; }
    if (!m_hasFontItalic)   { m_fontItalic = parent.m_fontItalic; m_hasFontItalic = parent.m_hasFontItalic; }
    if (!m_hasForeground)   { m_foreground = parent.m_foreground; m_hasForeground = parent.m_hasForeground; }
    if (!m_hasFirstLineIndent) { m_firstLineIndent = parent.m_firstLineIndent; m_hasFirstLineIndent = parent.m_hasFirstLineIndent; }
    if (!m_hasWordSpacing)  { m_wordSpacing = parent.m_wordSpacing; m_hasWordSpacing = parent.m_hasWordSpacing; }
    if (!m_hasFontFeatures) { m_fontFeatures = parent.m_fontFeatures; m_hasFontFeatures = parent.m_hasFontFeatures; }
}
