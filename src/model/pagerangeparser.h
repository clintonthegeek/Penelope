/*
 * pagerangeparser.h — Parse page range expressions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PAGERANGEPARSER_H
#define PENELOPE_PAGERANGEPARSER_H

#include <QSet>
#include <QString>

namespace PageRangeParser {

struct Result {
    QSet<int> pages;        // 1-based page numbers
    bool valid = true;
    QString errorMessage;   // non-empty if invalid
};

// Parse an expression like "1-5, 8, first, (last-3)-last"
// totalPages is required to resolve "last" keyword.
Result parse(const QString &expr, int totalPages);

} // namespace PageRangeParser

#endif // PENELOPE_PAGERANGEPARSER_H
