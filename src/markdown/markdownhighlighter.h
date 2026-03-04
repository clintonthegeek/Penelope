#ifndef PENELOPE_MARKDOWNHIGHLIGHTER_H
#define PENELOPE_MARKDOWNHIGHLIGHTER_H

#include <QColor>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *parent = nullptr);

    /// Apply palette colours to heading, code, and table rules.
    void setPaletteColors(const QColor &headingColor,
                          const QColor &codeText,
                          const QColor &codeBg,
                          const QColor &inlineCodeBg,
                          const QColor &tableColor);

protected:
    void highlightBlock(const QString &text) override;

private:
    void buildRules();

    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<Rule> m_rules;

    // Code fence state tracking
    QRegularExpression m_fencePattern;
    QTextCharFormat m_fenceFormat;
    QTextCharFormat m_codeBodyFormat;

    // Table line format
    QRegularExpression m_tablePattern;
    QTextCharFormat m_tableFormat;

    // Named rule indices for palette updates
    int m_headingRuleIdx = -1;
    int m_inlineCodeRuleIdx = -1;
    int m_fenceRuleIdx = -1;

    // Palette colours
    QColor m_headingColor{0x00, 0x55, 0x9e};
    QColor m_codeTextColor{0xc7, 0x25, 0x4e};
    QColor m_codeBgColor{0xf6, 0xf8, 0xfa};
    QColor m_inlineCodeBgColor{0xf0, 0xf0, 0xf0};
    QColor m_tableColor{0x6a, 0x73, 0x7d};
};

#endif // PENELOPE_MARKDOWNHIGHLIGHTER_H
