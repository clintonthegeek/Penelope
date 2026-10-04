#include "characterstyle.h"

CharacterStyle::CharacterStyle(const QString &name)
    : m_name(name)
{
}

void CharacterStyle::inheritFrom(const CharacterStyle &parent)
{
    if (!m_hasFontFamily)    { m_fontFamily = parent.m_fontFamily; m_hasFontFamily = parent.m_hasFontFamily; }
    if (!m_hasFontSize)      { m_fontSize = parent.m_fontSize; m_hasFontSize = parent.m_hasFontSize; }
    if (!m_hasFontWeight)    { m_fontWeight = parent.m_fontWeight; m_hasFontWeight = parent.m_hasFontWeight; }
    if (!m_hasFontItalic)    { m_fontItalic = parent.m_fontItalic; m_hasFontItalic = parent.m_hasFontItalic; }
    if (!m_hasFontUnderline) { m_fontUnderline = parent.m_fontUnderline; m_hasFontUnderline = parent.m_hasFontUnderline; }
    if (!m_hasFontStrikeOut) { m_fontStrikeOut = parent.m_fontStrikeOut; m_hasFontStrikeOut = parent.m_hasFontStrikeOut; }
    if (!m_hasForeground)    { m_foreground = parent.m_foreground; m_hasForeground = parent.m_hasForeground; }
    if (!m_hasBackground)    { m_background = parent.m_background; m_hasBackground = parent.m_hasBackground; }
    if (!m_hasLetterSpacing) { m_letterSpacing = parent.m_letterSpacing; m_hasLetterSpacing = parent.m_hasLetterSpacing; }
    if (!m_hasFontFeatures)  { m_fontFeatures = parent.m_fontFeatures; m_hasFontFeatures = parent.m_hasFontFeatures; }
}
