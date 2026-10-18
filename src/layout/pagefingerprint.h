#ifndef PENELOPE_PAGEFINGERPRINT_H
#define PENELOPE_PAGEFINGERPRINT_H

#include "layoutengine.h"

// Hash of what flowed into a page. Pointers are not included.
quint64 pageFingerprint(const Layout::LayoutCursor &incoming,
                        const QList<quint64> &blockIds,
                        quint64 blockContentHashes,
                        bool keepWithNextIn,
                        int tableHeaderRows,
                        int noteCount);

#endif
