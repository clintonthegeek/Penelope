#include "smartpunctuation.h"

namespace {

bool isWordCharacter(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

bool opensAQuote(QChar previous, QChar next)
{
    // An opening quote follows nothing, a space, or an opening bracket, and is
    // followed by something visible.
    const bool beforeIsOpen = previous.isNull() || previous.isSpace()
        || QStringLiteral("([{<\u2018\u201C").contains(previous);
    const bool afterIsText = !next.isNull() && !next.isSpace();
    return beforeIsOpen && afterIsText;
}

} // namespace

QString applySmartPunctuation(const QString &text)
{
    if (text.isEmpty())
        return text;

    QString result;
    result.reserve(text.size() + 8);

    int i = 0;
    while (i < text.size()) {
        const QChar ch = text.at(i);
        const QChar previous = result.isEmpty() ? QChar() : result.at(result.size() - 1);
        const QChar next = (i + 1 < text.size()) ? text.at(i + 1) : QChar();

        if (ch == QLatin1Char('-')) {
            if (next == QLatin1Char('-')) {
                const bool third = (i + 2 < text.size()) && text.at(i + 2) == QLatin1Char('-');
                result.append(third ? QChar(0x2014) : QChar(0x2013));
                i += third ? 3 : 2;
                continue;
            }
        } else if (ch == QLatin1Char('.')) {
            if (next == QLatin1Char('.') && i + 2 < text.size() && text.at(i + 2) == QLatin1Char('.')) {
                result.append(QChar(0x2026));
                i += 3;
                continue;
            }
        } else if (ch == QLatin1Char('\'')) {
            // Apostrophes inside a word close; a leading quote opens.
            result.append(isWordCharacter(previous) ? QChar(0x2019)
                                                    : (opensAQuote(previous, next) ? QChar(0x2018)
                                                                                   : QChar(0x2019)));
            ++i;
            continue;
        } else if (ch == QLatin1Char('"')) {
            result.append(opensAQuote(previous, next) ? QChar(0x201C) : QChar(0x201D));
            ++i;
            continue;
        }
        result.append(ch);
        ++i;
    }
    return result;
}
