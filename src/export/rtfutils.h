/*
 * rtfutils.h — Shared RTF utility functions
 *
 * Provides escapeText(), toTwips(), and toHalfPoints() used by both
 * RtfExporter (QTextDocument-based) and ContentRtfExporter (Content model).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_RTFUTILS_H
#define PENELOPE_RTFUTILS_H

#include <QByteArray>
#include <QString>
#include <QtMath>

namespace RtfUtils {

/// Escape RTF special characters and map common Unicode to RTF keywords.
inline QByteArray escapeText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size() * 2);

    for (QChar ch : text) {
        ushort code = ch.unicode();
        if (code == '\\')
            result.append("\\\\");
        else if (code == '{')
            result.append("\\{");
        else if (code == '}')
            result.append("\\}");
        else if (code == '\t')
            result.append("\\tab ");
        else if (code == 0x00A0) // non-breaking space
            result.append("\\~");
        else if (code == 0x00AD) // soft hyphen
            result.append("\\-");
        else if (code == 0x2014) // em dash
            result.append("\\emdash ");
        else if (code == 0x2013) // en dash
            result.append("\\endash ");
        else if (code == 0x2018 || code == 0x2019) // smart single quotes
            result.append(code == 0x2018 ? "\\lquote " : "\\rquote ");
        else if (code == 0x201C || code == 0x201D) // smart double quotes
            result.append(code == 0x201C ? "\\ldblquote " : "\\rdblquote ");
        else if (code > 127) {
            // Unicode character
            result.append("\\u");
            result.append(QByteArray::number(static_cast<qint16>(code)));
            result.append("?"); // fallback character
        } else {
            result.append(static_cast<char>(code));
        }
    }

    return result;
}

/// Convert points to twips (1 point = 20 twips).
inline int toTwips(qreal points)
{
    return qRound(points * 20.0);
}

/// Convert points to half-points (1 point = 2 half-points).
inline int toHalfPoints(qreal points)
{
    return qRound(points * 2.0);
}

} // namespace RtfUtils

#endif // PENELOPE_RTFUTILS_H
