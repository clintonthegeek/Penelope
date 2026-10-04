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
    static const QRegularExpression dateRx(
        QStringLiteral(R"(\{date:([^}]+)\})"));
    QRegularExpressionMatch match = dateRx.match(result);
    if (match.hasMatch()) {
        QString fmt = match.captured(1);
        result.replace(match.captured(0),
                       QDate::currentDate().toString(fmt));
    }

    return result;
}

} // namespace HeaderFooterRenderer
