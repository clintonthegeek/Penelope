#include "frontmatterparser.h"

QString FrontMatterParser::process(const QString &markdownText)
{
    m_frontMatter = FrontMatter{};

    if (!markdownText.startsWith(QLatin1String("---\n"))
        && !markdownText.startsWith(QLatin1String("---\r\n")))
        return markdownText;

    const int closePos = markdownText.indexOf(QLatin1String("\n---"), 3);
    int bodyStart = -1;
    if (closePos >= 0) {
        const int fmStart = markdownText.indexOf(QLatin1Char('\n')) + 1;
        m_frontMatter.raw = markdownText.mid(fmStart, closePos - fmStart);
        bodyStart = closePos + 4; // past "\n---"
        if (bodyStart < markdownText.size() && markdownText.at(bodyStart) == QLatin1Char('\r'))
            ++bodyStart;
        if (bodyStart < markdownText.size() && markdownText.at(bodyStart) == QLatin1Char('\n'))
            ++bodyStart;
    } else if (markdownText.endsWith(QLatin1String("\n---"))) {
        const int fmStart = markdownText.indexOf(QLatin1Char('\n')) + 1;
        m_frontMatter.raw = markdownText.mid(fmStart, markdownText.size() - fmStart - 4);
        bodyStart = markdownText.size();
    }
    if (bodyStart < 0)
        return markdownText;

    // Carriage returns belong to the line endings, not to the metadata.
    while (m_frontMatter.raw.endsWith(QLatin1Char('\r')))
        m_frontMatter.raw.chop(1);
    m_frontMatter.present = true;

    // Blank the block by keeping only its line separators, so the line count
    // and line numbering the source map depends on are preserved. This also
    // covers a closing "---" that ends the file without a trailing newline.
    QString blanked;
    const QString prefix = markdownText.left(bodyStart);
    blanked.reserve(prefix.size());
    for (const QChar ch : prefix) {
        if (ch == QLatin1Char('\n'))
            blanked.append(ch);
    }
    return blanked + markdownText.mid(bodyStart);
}
