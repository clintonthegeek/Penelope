/*
 * codespancollector.h — KSyntaxHighlighting adapter for code span collection
 *
 * Lightweight AbstractHighlighter subclass that runs KSyntaxHighlighting
 * over a code block and collects styled spans (foreground, background,
 * bold, italic).  Used by both the layout engine (PDF rendering) and the
 * RTF exporter (clipboard copy).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_CODESPANCOLLECTOR_H
#define PENELOPE_CODESPANCOLLECTOR_H

#include <QColor>
#include <QList>
#include <QString>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

// Lightweight AbstractHighlighter subclass that collects styled spans
class CodeSpanCollector : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    struct Span {
        int start;
        int length;
        QColor foreground;
        QColor background;
        bool bold = false;
        bool italic = false;
    };

    CodeSpanCollector()
    {
        static KSyntaxHighlighting::Repository repo;
        m_repo = &repo;
        auto defaultTheme = repo.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
        setTheme(defaultTheme);
    }

    QList<Span> highlight(const QString &code, const QString &language)
    {
        m_spans.clear();
        m_lineOffset = 0;

        auto def = m_repo->definitionForName(language);
        if (!def.isValid())
            def = m_repo->definitionForFileName(QStringLiteral("file.") + language);
        if (!def.isValid())
            return {};

        setDefinition(def);

        KSyntaxHighlighting::State state;
        const auto lines = code.split(QLatin1Char('\n'));
        for (const auto &line : lines) {
            state = highlightLine(line, state);
            m_lineOffset += line.size() + 1; // +1 for the \n
        }

        return m_spans;
    }

protected:
    void applyFormat(int offset, int length,
                     const KSyntaxHighlighting::Format &format) override
    {
        if (length == 0)
            return;

        Span span;
        span.start = m_lineOffset + offset;
        span.length = length;
        if (format.hasTextColor(theme()))
            span.foreground = format.textColor(theme());
        if (format.hasBackgroundColor(theme()))
            span.background = format.backgroundColor(theme());
        span.bold = format.isBold(theme());
        span.italic = format.isItalic(theme());
        m_spans.append(span);
    }

private:
    KSyntaxHighlighting::Repository *m_repo = nullptr;
    QList<Span> m_spans;
    int m_lineOffset = 0;
};

#endif // PENELOPE_CODESPANCOLLECTOR_H
