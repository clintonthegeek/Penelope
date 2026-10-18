#include "headerfooterrenderer.h"

#include <QDate>
#include <QLocale>
#include <QRegularExpression>

namespace HeaderFooterRenderer {

QString resolveField(const QString &text, const PageMetadata &meta)
{
    if (text.isEmpty())
        return {};

    QString result = text;
    result.replace(QLatin1String("{page}"), QString::number(meta.pageNumber + 1));
    result.replace(QLatin1String("{pages}"), QString::number(meta.totalPages));
    result.replace(QLatin1String("{filename}"), meta.fileName);
    result.replace(QLatin1String("{title}"), meta.title);
    result.replace(QLatin1String("{date}"),
                   QLocale().toString(QDate::currentDate(), QLocale::ShortFormat));

    // Custom date format: {date:yyyy-MM-dd}
    // Every occurrence, not just the first: a field may carry two date formats.
    static const QRegularExpression dateRx(
        QStringLiteral(R"(\{date:([^}]+)\})"));
    const QDate today = QDate::currentDate();
    auto match = dateRx.match(result);
    while (match.hasMatch()) {
        const QString rendered = today.toString(match.captured(1));
        result.replace(match.capturedStart(), match.capturedLength(), rendered);
        match = dateRx.match(result, match.capturedStart() + rendered.size());
    }

    return result;
}

} // namespace HeaderFooterRenderer
