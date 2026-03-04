/*
 * pdfexportoptions.h — Options struct for PDF export dialog
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFEXPORTOPTIONS_H
#define PENELOPE_PDFEXPORTOPTIONS_H

#include <QSet>
#include <QString>

struct PdfExportOptions {
    // General — metadata
    QString title;
    QString author;
    QString subject;
    QString keywords;           // comma-separated

    // General — text copy behavior (independent flags, can be combined)
    bool markdownCopy = false;      // embed hidden markdown syntax in PDF
    bool unwrapParagraphs = false;  // embed unwrapped paragraph text in PDF
    bool xobjectGlyphs = false;     // render all glyphs as Form XObjects (no text operators)

    // General — font rendering
    bool useHersheyFonts = false;   // substitute TTF fonts with Hershey stroke fonts

    // Content — section selection
    QSet<int> excludedHeadingIndices;   // indices into doc.blocks of unchecked headings
    bool sectionsModified = false;      // true if user changed any checkboxes

    // Content — page range
    QString pageRangeExpr;              // raw expression, empty = all pages
    bool pageRangeModified = false;     // true if user entered a range

    // Output — bookmarks
    bool includeBookmarks = true;
    int bookmarkMaxDepth = 6;           // 1–6

    // Output — viewer preferences
    enum InitialView { ViewerDefault, ShowBookmarks, ShowThumbnails };
    InitialView initialView = ShowBookmarks;

    enum PageLayout { SinglePage, Continuous, FacingPages, FacingPagesFirstAlone };
    PageLayout pageLayout = Continuous;
};

#endif // PENELOPE_PDFEXPORTOPTIONS_H
