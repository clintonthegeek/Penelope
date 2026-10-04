#include "markdownhighlighter.h"

#include <QRegularExpression>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    buildRules();
}

void MarkdownHighlighter::buildRules()
{
    m_rules.clear();

    // Headings: # through ######
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^#{1,6}\\s.*$"));
        rule.format.setFontWeight(QFont::Bold);
        rule.format.setForeground(m_headingColor);
        m_headingRuleIdx = m_rules.size();
        m_rules.append(rule);
    }

    // Bold: **text** or __text__
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral(R"((\*\*|__)(.*?)\1)"));
        rule.format.setFontWeight(QFont::Bold);
        m_rules.append(rule);
    }

    // Italic: *text* or _text_
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral(R"((?<!\*)\*(?!\*)(.*?)(?<!\*)\*(?!\*))"));
        rule.format.setFontItalic(true);
        m_rules.append(rule);
    }

    // Inline code: `text`
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("`[^`]+`"));
        rule.format.setForeground(m_codeTextColor);
        rule.format.setBackground(m_inlineCodeBgColor);
        QFont mono(QStringLiteral("JetBrains Mono"));
        mono.setStyleHint(QFont::Monospace);
        rule.format.setFont(mono);
        m_inlineCodeRuleIdx = m_rules.size();
        m_rules.append(rule);
    }

    // Links: [text](url)
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral(R"(\[([^\]]+)\]\([^\)]+\))"));
        rule.format.setForeground(QColor(0x03, 0x66, 0xd6));
        rule.format.setFontUnderline(true);
        m_rules.append(rule);
    }

    // Images: ![alt](url)
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral(R"(!\[([^\]]*)\]\([^\)]+\))"));
        rule.format.setForeground(QColor(0x6f, 0x42, 0xc1));
        m_rules.append(rule);
    }

    // Blockquote: > text
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^>+\\s.*$"));
        rule.format.setForeground(QColor(0x6a, 0x73, 0x7d));
        rule.format.setFontItalic(true);
        m_rules.append(rule);
    }

    // List markers: -, *, +, or 1.
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral(R"(^\s*[-*+](?=\s)|^\s*\d+\.(?=\s))"));
        rule.format.setForeground(QColor(0xe3, 0x6c, 0x09));
        rule.format.setFontWeight(QFont::Bold);
        m_rules.append(rule);
    }

    // Horizontal rule: ---, ***, ___
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral(R"(^(\*{3,}|-{3,}|_{3,})\s*$)"));
        rule.format.setForeground(QColor(0xaa, 0xaa, 0xaa));
        m_rules.append(rule);
    }

    // Strikethrough: ~~text~~
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("~~.+?~~"));
        rule.format.setFontStrikeOut(true);
        rule.format.setForeground(QColor(0x99, 0x99, 0x99));
        m_rules.append(rule);
    }

    // Code fence pattern (used for state tracking, not as a normal rule)
    m_fencePattern = QRegularExpression(QStringLiteral("^(```|~~~).*$"));
    m_fenceFormat.setForeground(m_codeTextColor);
    m_fenceFormat.setBackground(m_codeBgColor);

    // Code body format (lines between fences)
    m_codeBodyFormat.setForeground(m_codeTextColor);
    m_codeBodyFormat.setBackground(m_codeBgColor);

    // Table line pattern: lines starting and ending with |
    m_tablePattern = QRegularExpression(QStringLiteral(R"(^\|.*\|$)"));
    m_tableFormat.setForeground(m_tableColor);
}

void MarkdownHighlighter::setPaletteColors(const QColor &headingColor,
                                            const QColor &codeText,
                                            const QColor &codeBg,
                                            const QColor &inlineCodeBg,
                                            const QColor &tableColor)
{
    if (m_headingColor == headingColor && m_codeTextColor == codeText
        && m_codeBgColor == codeBg && m_inlineCodeBgColor == inlineCodeBg
        && m_tableColor == tableColor)
        return;
    m_headingColor = headingColor;
    m_codeTextColor = codeText;
    m_codeBgColor = codeBg;
    m_inlineCodeBgColor = inlineCodeBg;
    m_tableColor = tableColor;

    buildRules();
    rehighlight();
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    // State: 0 = normal, 1 = inside fenced code block
    int prevState = previousBlockState();
    bool inCodeBlock = (prevState == 1);

    // Check for fence delimiter
    auto fenceMatch = m_fencePattern.match(text);
    if (fenceMatch.hasMatch()) {
        setFormat(0, text.length(), m_fenceFormat);
        // Toggle state
        setCurrentBlockState(inCodeBlock ? 0 : 1);
        return;
    }

    if (inCodeBlock) {
        // Inside code block: apply code body format to entire line
        setFormat(0, text.length(), m_codeBodyFormat);
        setCurrentBlockState(1);
        return;
    }

    setCurrentBlockState(0);

    // Apply normal rules
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(),
                      rule.format);
        }
    }

    // Table lines
    auto tableMatch = m_tablePattern.match(text);
    if (tableMatch.hasMatch()) {
        setFormat(0, text.length(), m_tableFormat);
    }
}
