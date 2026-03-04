#ifndef PENELOPE_STYLEMANAGER_H
#define PENELOPE_STYLEMANAGER_H

#include "characterstyle.h"
#include "footnotestyle.h"
#include "paragraphstyle.h"
#include "tablestyle.h"

#include <QHash>
#include <QObject>
#include <QString>

class StyleManager : public QObject
{
    Q_OBJECT

public:
    explicit StyleManager(QObject *parent = nullptr);

    void addParagraphStyle(const ParagraphStyle &style);
    void addCharacterStyle(const CharacterStyle &style);

    ParagraphStyle *paragraphStyle(const QString &name);
    CharacterStyle *characterStyle(const QString &name);

    const QHash<QString, ParagraphStyle> &paragraphStyles() const { return m_paraStyles; }
    const QHash<QString, CharacterStyle> &characterStyles() const { return m_charStyles; }

    // Table styles
    void addTableStyle(const TableStyle &style);
    TableStyle *tableStyle(const QString &name);
    const QHash<QString, TableStyle> &tableStyles() const { return m_tableStyles; }
    QStringList tableStyleNames() const;

    QStringList paragraphStyleNames() const;
    QStringList characterStyleNames() const;

    // Resolve a style by walking its parent chain.
    // Returns a fully-resolved copy with all inherited properties filled in.
    ParagraphStyle resolvedParagraphStyle(const QString &name);
    CharacterStyle resolvedCharacterStyle(const QString &name);

    // Footnote style
    FootnoteStyle footnoteStyle() const { return m_footnoteStyle; }
    void setFootnoteStyle(const FootnoteStyle &style) { m_footnoteStyle = style; }

    // Deep-copy this style manager
    StyleManager *clone(QObject *parent = nullptr) const;

private:
    QHash<QString, ParagraphStyle> m_paraStyles;
    QHash<QString, CharacterStyle> m_charStyles;
    QHash<QString, TableStyle> m_tableStyles;
    FootnoteStyle m_footnoteStyle;
};

#endif // PENELOPE_STYLEMANAGER_H
