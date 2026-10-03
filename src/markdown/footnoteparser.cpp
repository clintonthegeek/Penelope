#include "footnoteparser.h"

#include <QHash>
#include <QRegularExpression>

QString FootnoteParser::process(const QString &markdownText)
{
    m_rawDefs.clear();
    m_footnotes.clear();

    // Extract and blank definitions in a single pass. Keeping newlines makes
    // source ranges usable by the TOC and source editor after preprocessing.
    QStringList lines = markdownText.split(QLatin1Char('\n'));
    QStringList referenceLines = lines;
    static const QRegularExpression definition(QStringLiteral(R"(^\[\^([^\]]+)\]:[ \t]*(.*)$)"));
    static const QRegularExpression fence(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})(.*)$)"));
    QChar fenceChar;
    int fenceLength = 0;
    for (int i = 0; i < lines.size(); ++i) {
        auto fm = fence.match(lines[i]);
        if (!fenceChar.isNull()) {
            referenceLines[i].clear();
            if (fm.hasMatch() && fm.captured(1)[0] == fenceChar
                && fm.captured(1).size() >= fenceLength && fm.captured(2).trimmed().isEmpty())
                fenceChar = QChar();
            continue;
        }
        if (fm.hasMatch()) {
            fenceChar = fm.captured(1)[0];
            fenceLength = fm.captured(1).size();
            referenceLines[i].clear();
            continue;
        }
        if (lines[i].startsWith(QLatin1String("    ")) || lines[i].startsWith(QLatin1Char('\t'))) {
            referenceLines[i].clear();
            continue;
        }
        auto match = definition.match(lines[i]);
        if (!match.hasMatch()) continue;
        RawDefinition def;
        def.label = match.captured(1);
        def.content = match.captured(2).trimmed();
        def.sourceOrder = m_rawDefs.size();
        const int start = i;
        int end = i + 1;
        bool blank = false;
        while (end < lines.size()) {
            if (lines[end].trimmed().isEmpty()) {
                int next = end;
                while (next < lines.size() && lines[next].trimmed().isEmpty()) ++next;
                if (next == lines.size() || !(lines[next].startsWith(QLatin1String("    "))
                    || lines[next].startsWith(QLatin1Char('\t')))) break;
                blank = true;
                end = next;
            }
            if (!(lines[end].startsWith(QLatin1String("    "))
                || lines[end].startsWith(QLatin1Char('\t')))) break;
            def.content += blank ? QStringLiteral("\n\n") : QStringLiteral(" ");
            def.content += lines[end].mid(lines[end].startsWith(QLatin1Char('\t')) ? 1 : 4).trimmed();
            blank = false;
            ++end;
        }
        m_rawDefs.append(def);
        for (int j = start; j < end; ++j) { lines[j].clear(); referenceLines[j].clear(); }
        i = end - 1;
    }
    QString references = referenceLines.join(QLatin1Char('\n'));
    // Code spans and escaped references are literal text, not numbering sites.
    static const QRegularExpression code(QStringLiteral(R"((`+).*?\1)"), QRegularExpression::DotMatchesEverythingOption);
    references.replace(code, QString());
    static const QRegularExpression escaped(QStringLiteral(R"(\\\[\^([^\]]+)\])"));
    references.replace(escaped, QString());
    orderByReference(references);
    return lines.join(QLatin1Char('\n'));
}

void FootnoteParser::orderByReference(const QString &text)
{
    // Find all [^label] references in order of appearance
    static const QRegularExpression refRx(
        QStringLiteral(R"(\[\^([^\]]+)\])"));

    // Build a label -> raw definition map
    QHash<QString, RawDefinition> defMap;
    for (const auto &def : m_rawDefs)
        defMap.insert(def.label, def);

    // Scan references and assign sequential numbers
    QHash<QString, int> assignedNumbers;
    int nextNumber = 1;

    auto it = refRx.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        QString label = match.captured(1);

        // Skip if this is actually a definition line (has ]: after it)
        int afterEnd = match.capturedEnd();
        if (afterEnd < text.length() && text[afterEnd] == QLatin1Char(':'))
            continue;

        if (!assignedNumbers.contains(label) && defMap.contains(label)) {
            assignedNumbers.insert(label, nextNumber++);
        }
    }

    // Build the ordered footnotes list
    m_footnotes.clear();
    m_footnotes.reserve(assignedNumbers.size());

    // Create a sorted list by assigned number
    QList<QPair<int, QString>> ordered;
    for (auto it = assignedNumbers.constBegin(); it != assignedNumbers.constEnd(); ++it)
        ordered.append({it.value(), it.key()});
    std::sort(ordered.begin(), ordered.end());

    for (const auto &pair : ordered) {
        const RawDefinition &raw = defMap.value(pair.second);
        FootnoteDefinition fn;
        fn.label = raw.label;
        fn.sequentialNumber = pair.first;
        fn.content = raw.content;
        m_footnotes.append(fn);
    }

    // Add any definitions that were never referenced (at the end)
    for (const auto &def : m_rawDefs) {
        if (!assignedNumbers.contains(def.label)) {
            FootnoteDefinition fn;
            fn.label = def.label;
            fn.sequentialNumber = nextNumber++;
            fn.content = def.content;
            m_footnotes.append(fn);
        }
    }
}
