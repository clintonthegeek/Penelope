/*
 * contentfilter.h — Filter Content::Document by section selection
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_CONTENTFILTER_H
#define PENELOPE_CONTENTFILTER_H

#include <QSet>

#include "contentmodel.h"

namespace ContentFilter {

// Remove excluded sections from a document.
// excludedHeadingIndices: indices into doc.blocks that are Heading blocks.
// Removing a heading also removes all content up to the next heading of
// the same or higher level.
Content::Document filterSections(const Content::Document &doc,
                                  const QSet<int> &excludedHeadingIndices);

} // namespace ContentFilter

#endif // PENELOPE_CONTENTFILTER_H
