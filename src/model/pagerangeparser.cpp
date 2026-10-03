/*
 * pagerangeparser.cpp — Parse page range expressions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pagerangeparser.h"

#include <QRegularExpression>
#include <QStringList>

namespace PageRangeParser {

// Resolve a single token: a number, "first", "last", or "(last-N)"
static int resolveToken(const QString &token, int totalPages, bool &ok)
{
    ok = true;
    QString t = token.trimmed().toLower();
    if (totalPages < 1) { ok = false; return -1; }
    if (t == QLatin1String("first")) return 1;
    if (t == QLatin1String("last")) return totalPages;

    // "(last-N)" pattern
    static QRegularExpression parenRe(
        QStringLiteral(R"(^\(last\s*-\s*(\d+)\)$)"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = parenRe.match(t);
    if (m.hasMatch()) {
        int offset = m.captured(1).toInt(&ok);
        if (!ok) return -1;
        int page = totalPages - offset;
        if (page < 1) { ok = false; return -1; }
        return page;
    }

    // "last-N" without parens (when used standalone, not as range endpoint)
    static QRegularExpression lastMinusRe(
        QStringLiteral(R"(^last\s*-\s*(\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    m = lastMinusRe.match(t);
    if (m.hasMatch()) {
        int offset = m.captured(1).toInt(&ok);
        if (!ok) return -1;
        int page = totalPages - offset;
        if (page < 1) { ok = false; return -1; }
        return page;
    }

    // Plain number
    int page = t.toInt(&ok);
    if (!ok || page < 1 || page > totalPages) {
        ok = false;
        return -1;
    }
    return page;
}

Result parse(const QString &expr, int totalPages)
{
    Result result;
    if (totalPages < 1) {
        result.valid = false;
        result.errorMessage = QObject::tr("No pages to export.");
        return result;
    }
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty()) {
        // Empty = all pages
        for (int i = 1; i <= totalPages; ++i)
            result.pages.insert(i);
        return result;
    }

    // Split by comma
    QStringList parts = trimmed.split(QLatin1Char(','));
    for (const QString &part : parts) {
        QString p = part.trimmed();
        if (p.isEmpty()) {
            result.valid = false;
            result.errorMessage = QObject::tr("Empty page range item.");
            return result;
        }
        static const QRegularExpression arithmetic(QStringLiteral(R"(^last\s*-\s*\d+$)"),
                                                    QRegularExpression::CaseInsensitiveOption);
        if (arithmetic.match(p).hasMatch()) {
            bool ok;
            int page = resolveToken(p, totalPages, ok);
            if (!ok) {
                result.valid = false;
                result.errorMessage = QObject::tr("Invalid page: %1").arg(p);
                return result;
            }
            result.pages.insert(page);
            continue;
        }

        // Find a '-' that acts as range separator (not inside parentheses,
        // not part of "last-N" arithmetic)
        int splitPos = -1;
        int parenDepth = 0;
        for (int i = 0; i < p.size(); ++i) {
            if (p[i] == QLatin1Char('(')) parenDepth++;
            else if (p[i] == QLatin1Char(')')) parenDepth--;
            else if (p[i] == QLatin1Char('-') && parenDepth == 0) {
                // Check if this '-' is part of "last-N"
                QString left = p.left(i).trimmed().toLower();
                if (left.endsWith(QLatin1String("last"))) {
                    bool leftOk;
                    resolveToken(left, totalPages, leftOk);
                    if (leftOk && left == QLatin1String("last")) {
                        // "last-N" is ambiguous. Treat as (last-N) single page
                        // unless right side is also a resolvable token.
                        QString right = p.mid(i + 1).trimmed();
                        bool rightIsToken;
                        resolveToken(right, totalPages, rightIsToken);
                        if (!rightIsToken) {
                            continue; // part of "last-N"
                        }
                        // Both sides resolve -> range "last" to "right"
                        splitPos = i;
                        break;
                    }
                    continue; // skip, part of "last-N"
                }
                splitPos = i;
                break;
            }
        }

        if (splitPos >= 0) {
            // Range
            QString leftStr = p.left(splitPos).trimmed();
            QString rightStr = p.mid(splitPos + 1).trimmed();
            bool leftOk, rightOk;
            int start = resolveToken(leftStr, totalPages, leftOk);
            int end = resolveToken(rightStr, totalPages, rightOk);
            if (!leftOk || !rightOk || start > end) {
                result.valid = false;
                result.errorMessage = QObject::tr("Invalid range: %1").arg(p);
                return result;
            }
            for (int i = start; i <= end; ++i)
                result.pages.insert(i);
        } else {
            // Single page
            bool ok;
            int page = resolveToken(p, totalPages, ok);
            if (!ok) {
                result.valid = false;
                result.errorMessage = QObject::tr("Invalid page: %1").arg(p);
                return result;
            }
            result.pages.insert(page);
        }
    }

    return result;
}

} // namespace PageRangeParser
