#include "footnotestyle.h"

static QString toRoman(int n)
{
    static const struct { int value; const char *numeral; } table[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
        {100, "c"}, {90, "xc"}, {50, "l"}, {40, "xl"},
        {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}
    };
    QString result;
    for (const auto &entry : table) {
        while (n >= entry.value) {
            result += QString::fromLatin1(entry.numeral);
            n -= entry.value;
        }
    }
    return result;
}

static QString toAlpha(int n)
{
    // 1->a, 2->b, ... 26->z, 27->aa, 28->ab, ...
    QString result;
    while (n > 0) {
        n--; // make 0-based
        result.prepend(QChar(QLatin1Char('a').unicode() + (n % 26)));
        n /= 26;
    }
    return result;
}

static QString toAsterisk(int n)
{
    // 1->*, 2->†, 3->‡, 4->**, 5->††, 6->‡‡, ...
    static const QChar symbols[] = {
        QChar(0x002A), // *
        QChar(0x2020), // †
        QChar(0x2021), // ‡
    };
    int cycle = (n - 1) / 3;
    int idx = (n - 1) % 3;
    return QString(cycle + 1, symbols[idx]);
}

QString FootnoteStyle::formatNumber(int n) const
{
    QString num;
    switch (format) {
    case Arabic:
        num = QString::number(n);
        break;
    case RomanLower:
        num = toRoman(n);
        break;
    case RomanUpper:
        num = toRoman(n).toUpper();
        break;
    case AlphaLower:
        num = toAlpha(n);
        break;
    case AlphaUpper:
        num = toAlpha(n).toUpper();
        break;
    case Asterisk:
        num = toAsterisk(n);
        break;
    }
    return prefix + num + suffix;
}

QJsonObject FootnoteStyle::toJson() const
{
    QJsonObject obj;
    switch (format) {
    case RomanLower: obj[QStringLiteral("format")] = QStringLiteral("roman_lower"); break;
    case RomanUpper: obj[QStringLiteral("format")] = QStringLiteral("roman_upper"); break;
    case AlphaLower: obj[QStringLiteral("format")] = QStringLiteral("alpha_lower"); break;
    case AlphaUpper: obj[QStringLiteral("format")] = QStringLiteral("alpha_upper"); break;
    case Asterisk:   obj[QStringLiteral("format")] = QStringLiteral("asterisk"); break;
    case Arabic:     obj[QStringLiteral("format")] = QStringLiteral("arabic"); break;
    }
    obj[QStringLiteral("startNumber")] = startNumber;
    obj[QStringLiteral("restart")] = restart == PerPage ? QStringLiteral("per_page")
                                                       : QStringLiteral("per_document");
    obj[QStringLiteral("prefix")] = prefix;
    obj[QStringLiteral("suffix")] = suffix;
    obj[QStringLiteral("superscriptRef")] = superscriptRef;
    obj[QStringLiteral("superscriptNote")] = superscriptNote;
    obj[QStringLiteral("asEndnotes")] = asEndnotes;
    obj[QStringLiteral("showSeparator")] = showSeparator;
    obj[QStringLiteral("separatorWidth")] = separatorWidth;
    obj[QStringLiteral("separatorLength")] = separatorLength;
    return obj;
}

void FootnoteStyle::applyJson(const QJsonObject &obj)
{
    if (obj.contains(QLatin1String("format"))) {
        const QString fmt = obj.value(QLatin1String("format")).toString();
        if (fmt == QLatin1String("roman_lower"))       format = RomanLower;
        else if (fmt == QLatin1String("roman_upper"))  format = RomanUpper;
        else if (fmt == QLatin1String("alpha_lower"))  format = AlphaLower;
        else if (fmt == QLatin1String("alpha_upper"))  format = AlphaUpper;
        else if (fmt == QLatin1String("asterisk"))     format = Asterisk;
        else                                           format = Arabic;
    }
    if (obj.contains(QLatin1String("startNumber")))
        startNumber = obj.value(QLatin1String("startNumber")).toInt(1);
    if (obj.contains(QLatin1String("restart")))
        restart = obj.value(QLatin1String("restart")).toString() == QLatin1String("per_page")
            ? PerPage : PerDocument;
    if (obj.contains(QLatin1String("prefix")))
        prefix = obj.value(QLatin1String("prefix")).toString();
    if (obj.contains(QLatin1String("suffix")))
        suffix = obj.value(QLatin1String("suffix")).toString();
    if (obj.contains(QLatin1String("superscriptRef")))
        superscriptRef = obj.value(QLatin1String("superscriptRef")).toBool(true);
    if (obj.contains(QLatin1String("superscriptNote")))
        superscriptNote = obj.value(QLatin1String("superscriptNote")).toBool(false);
    if (obj.contains(QLatin1String("asEndnotes")))
        asEndnotes = obj.value(QLatin1String("asEndnotes")).toBool(true);
    if (obj.contains(QLatin1String("showSeparator")))
        showSeparator = obj.value(QLatin1String("showSeparator")).toBool(true);
    if (obj.contains(QLatin1String("separatorWidth")))
        separatorWidth = obj.value(QLatin1String("separatorWidth")).toDouble(0.5);
    if (obj.contains(QLatin1String("separatorLength")))
        separatorLength = obj.value(QLatin1String("separatorLength")).toDouble(72.0);
}
