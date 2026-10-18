#include "pagefingerprint.h"

#include <QtGlobal>

quint64 pageFingerprint(const Layout::LayoutCursor &incoming,
                        const QList<quint64> &blockIds,
                        quint64 blockContentHashes,
                        bool keepWithNextIn,
                        int tableHeaderRows,
                        int noteCount)
{
    quint64 hash = qHashMulti(quint64(0),
                              qRound64(incoming.y * 100.0),
                              qRound64(incoming.yOnPage * 100.0),
                              incoming.nextBlock,
                              incoming.currentPageNumber,
                              incoming.prevKeepWithNext ? 1 : 0,
                              blockContentHashes,
                              keepWithNextIn ? 1 : 0,
                              tableHeaderRows,
                              noteCount);
    for (quint64 id : blockIds)
        hash = qHashMulti(hash, id);
    return hash;
}
