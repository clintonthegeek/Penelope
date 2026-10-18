#include "stylemanager.h"

#include <QSet>

StyleManager::StyleManager(QObject *parent)
    : QObject(parent)
{
}

void StyleManager::addParagraphStyle(const ParagraphStyle &style)
{
    m_paraStyles.insert(style.name(), style);
}

void StyleManager::addCharacterStyle(const CharacterStyle &style)
{
    m_charStyles.insert(style.name(), style);
}

ParagraphStyle *StyleManager::paragraphStyle(const QString &name)
{
    auto it = m_paraStyles.find(name);
    return it != m_paraStyles.end() ? &it.value() : nullptr;
}

CharacterStyle *StyleManager::characterStyle(const QString &name)
{
    auto it = m_charStyles.find(name);
    return it != m_charStyles.end() ? &it.value() : nullptr;
}

QStringList StyleManager::paragraphStyleNames() const
{
    return m_paraStyles.keys();
}

QStringList StyleManager::characterStyleNames() const
{
    return m_charStyles.keys();
}

ParagraphStyle StyleManager::resolvedParagraphStyle(const QString &name)
{
    auto it = m_paraStyles.find(name);
    if (it == m_paraStyles.end())
        return ParagraphStyle(name);

    ParagraphStyle resolved = it.value();
    QSet<QString> visited;
    visited.insert(name);

    // Cross-type linkage: fill unset char properties from the nearest base
    // character style on this style or an ancestor. Applied BEFORE the
    // paragraph parent chain so Code's monospace face wins over the serif
    // face inherited from Default Paragraph Style.
    QString baseCharName;
    {
        QString look = name;
        QSet<QString> seen;
        while (!look.isEmpty() && !seen.contains(look)) {
            seen.insert(look);
            auto bit = m_paraStyles.find(look);
            if (bit == m_paraStyles.end())
                break;
            if (bit->hasBaseCharacterStyle()) {
                baseCharName = bit->baseCharacterStyleName();
                break;
            }
            look = bit->parentStyleName();
        }
    }
    if (!baseCharName.isEmpty()) {
        CharacterStyle charResolved = resolvedCharacterStyle(baseCharName);
        if (!resolved.hasFontFamily() && charResolved.hasFontFamily())
            resolved.setFontFamily(charResolved.fontFamily());
        if (!resolved.hasFontSize() && charResolved.hasFontSize())
            resolved.setFontSize(charResolved.fontSize());
        if (!resolved.hasFontWeight() && charResolved.hasFontWeight())
            resolved.setFontWeight(charResolved.fontWeight());
        if (!resolved.hasFontItalic() && charResolved.hasFontItalic())
            resolved.setFontItalic(charResolved.fontItalic());
        if (!resolved.hasForeground() && charResolved.hasForeground())
            resolved.setForeground(charResolved.foreground());
        if (!resolved.hasFontFeatures() && charResolved.hasFontFeatures())
            resolved.setFontFeatures(charResolved.fontFeatures());
    }

    QString parentName = resolved.parentStyleName();
    while (!parentName.isEmpty() && !visited.contains(parentName)) {
        visited.insert(parentName);
        auto pit = m_paraStyles.find(parentName);
        if (pit == m_paraStyles.end())
            break;
        resolved.inheritFrom(pit.value());
        parentName = pit.value().parentStyleName();
    }

    return resolved;
}

CharacterStyle StyleManager::resolvedCharacterStyle(const QString &name)
{
    auto it = m_charStyles.find(name);
    if (it == m_charStyles.end())
        return CharacterStyle(name);

    CharacterStyle resolved = it.value();
    QSet<QString> visited;
    visited.insert(name);

    QString parentName = resolved.parentStyleName();
    while (!parentName.isEmpty() && !visited.contains(parentName)) {
        visited.insert(parentName);
        auto pit = m_charStyles.find(parentName);
        if (pit == m_charStyles.end())
            break;
        resolved.inheritFrom(pit.value());
        parentName = pit.value().parentStyleName();
    }

    return resolved;
}

CharacterStyle StyleManager::characterStyleOverrides(const QString &name)
{
    QList<CharacterStyle> chain;
    QSet<QString> visited;
    QString current = name;
    while (!current.isEmpty()
           && current != QLatin1String("Default Character Style")
           && !visited.contains(current)) {
        visited.insert(current);
        auto it = m_charStyles.find(current);
        if (it == m_charStyles.end())
            break;
        chain.append(it.value());
        current = it->parentStyleName();
    }

    CharacterStyle result(name);
    for (int i = chain.size() - 1; i >= 0; --i) {
        const CharacterStyle &style = chain.at(i);
        if (style.hasFontFamily()) result.setFontFamily(style.fontFamily());
        if (style.hasFontSize()) result.setFontSize(style.fontSize());
        if (style.hasFontWeight()) result.setFontWeight(style.fontWeight());
        if (style.hasFontItalic()) result.setFontItalic(style.fontItalic());
        if (style.hasFontUnderline()) result.setFontUnderline(style.fontUnderline());
        if (style.hasFontStrikeOut()) result.setFontStrikeOut(style.fontStrikeOut());
        if (style.hasForeground()) result.setForeground(style.foreground());
        if (style.hasBackground()) result.setBackground(style.background());
        if (style.hasLetterSpacing()) result.setLetterSpacing(style.letterSpacing());
        if (style.hasFontFeatures()) result.setFontFeatures(style.fontFeatures());
    }
    return result;
}

void StyleManager::addTableStyle(const TableStyle &style)
{
    m_tableStyles.insert(style.name(), style);
}

TableStyle *StyleManager::tableStyle(const QString &name)
{
    auto it = m_tableStyles.find(name);
    return it != m_tableStyles.end() ? &it.value() : nullptr;
}

QStringList StyleManager::tableStyleNames() const
{
    return m_tableStyles.keys();
}

StyleManager *StyleManager::clone(QObject *parent) const
{
    auto *copy = new StyleManager(parent);
    copy->m_paraStyles = m_paraStyles;
    copy->m_charStyles = m_charStyles;
    copy->m_tableStyles = m_tableStyles;
    copy->m_footnoteStyle = m_footnoteStyle;
    return copy;
}
