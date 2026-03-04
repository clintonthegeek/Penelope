#ifndef PENELOPE_FOOTNOTEPARSER_H
#define PENELOPE_FOOTNOTEPARSER_H

#include <QList>
#include <QString>
#include <QStringList>

// Extracts footnote definitions and references from markdown text
// before it is passed to MD4C (which does not support footnotes).
//
// Supports PHP Markdown Extra / GFM footnote syntax:
//   Reference:   [^label]
//   Definition:  [^label]: Content text
//                    Continuation lines indented 4 spaces.
//
//                    Additional paragraphs also indented.

struct FootnoteDefinition {
    QString label;         // Original label (e.g., "1", "note")
    int sequentialNumber;  // 1-based sequential number by order of first reference
    QString content;       // Full content (may be multi-paragraph)
};

class FootnoteParser
{
public:
    FootnoteParser() = default;

    // Parse markdown text: extract footnote definitions and rewrite references.
    // Returns the cleaned markdown with:
    //  - Footnote definitions removed
    //  - [^label] references left intact (DocumentBuilder handles rendering)
    // Populates the footnotes() list.
    QString process(const QString &markdownText);

    // Get parsed footnotes in order of first reference
    const QList<FootnoteDefinition> &footnotes() const { return m_footnotes; }

private:
    void extractDefinitions(const QString &text);
    void orderByReference(const QString &text);

    // Raw definitions keyed by label
    struct RawDefinition {
        QString label;
        QString content;
        int sourceOrder;  // position in source text
    };
    QList<RawDefinition> m_rawDefs;

    // Final ordered footnotes
    QList<FootnoteDefinition> m_footnotes;
};

#endif // PENELOPE_FOOTNOTEPARSER_H
