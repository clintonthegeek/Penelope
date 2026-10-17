#include "fragmentcache.h"

#include <QCache>

static int estimateCost(const FragmentCacheValue &v) {
    int c = sizeof(FragmentCacheValue);
    for (const auto &l : v.lines)
        c += sizeof(Layout::LineBox) + l.glyphs.size() * 16;
    c += v.glyphIds.size() * sizeof(quint32);
    if (c < 1) c = 1;
    return c;
}

bool FragmentCache::storeParagraph(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                                  int pixelSize, int weight, bool italic,
                                  qreal width, bool hyphenate,
                                  const QList<Layout::LineBox> &lines,
                                  const QList<quint32> &glyphIds,
                                  int cost) {
    FragmentCacheValue v;
    v.lines = lines;
    v.glyphIds = glyphIds;
    int c = cost >= 0 ? cost : estimateCost(v);
    if (c > m_shapeCache.maxCost() || c > m_breakCache.maxCost())
        return false;

    FragmentCacheShapeKey sk;
    sk.blockId = blockId; sk.contentHash = contentHash; sk.fontFamily = fontFamily;
    sk.pixelSize = pixelSize; sk.weight = weight; sk.italic = italic;

    FragmentCacheBreakKey bk;
    static_cast<FragmentCacheShapeKey &>(bk) = sk;
    bk.width = qRound(width * 100);
    bk.hyphenate = hyphenate;

    auto *sv = new FragmentCacheValue(v);
    auto *bv = new FragmentCacheValue(v);
    bool ok1 = m_shapeCache.insert(sk, sv, c);
    bool ok2 = m_breakCache.insert(bk, bv, c);
    if (!ok1 || !ok2) {
        // remove if partial? QCache takes ownership; if insert fails, object deleted by QCache? Qt docs: if cost > maxCost, returns false and object deleted? Actually insert returns false if cost > maxCost, object not inserted but not deleted? To be safe, not delete.
        return false;
    }
    return true;
}

bool FragmentCache::loadParagraphBreaks(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                                       int pixelSize, int weight, bool italic,
                                       qreal width, bool hyphenate,
                                       QList<Layout::LineBox> *lines,
                                       QList<quint32> *glyphIds) {
    FragmentCacheBreakKey bk;
    bk.blockId = blockId; bk.contentHash = contentHash; bk.fontFamily = fontFamily;
    bk.pixelSize = pixelSize; bk.weight = weight; bk.italic = italic;
    bk.width = qRound(width * 100);
    bk.hyphenate = hyphenate;
    if (auto *v = m_breakCache.object(bk)) {
        ++breakHits;
        if (lines) *lines = v->lines;
        if (glyphIds) *glyphIds = v->glyphIds;
        return true;
    }
    ++breakMisses;
    return false;
}

bool FragmentCache::loadParagraphBreaks(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                                       int pixelSize, int weight, bool italic,
                                       qreal width, bool hyphenate,
                                       QList<Layout::LineBox> &lines,
                                       QList<quint32> &glyphIds) {
    return loadParagraphBreaks(blockId, contentHash, fontFamily, pixelSize, weight, italic, width, hyphenate, &lines, &glyphIds);
}

bool FragmentCache::loadParagraphShapes(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                                       int pixelSize, int weight, bool italic,
                                       QList<Layout::LineBox> *lines,
                                       QList<quint32> *glyphIds) {
    FragmentCacheShapeKey sk;
    sk.blockId = blockId; sk.contentHash = contentHash; sk.fontFamily = fontFamily;
    sk.pixelSize = pixelSize; sk.weight = weight; sk.italic = italic;
    if (auto *v = m_shapeCache.object(sk)) {
        if (lines) *lines = v->lines;
        if (glyphIds) *glyphIds = v->glyphIds;
        return true;
    }
    return false;
}

bool FragmentCache::loadParagraphShapes(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                                       int pixelSize, int weight, bool italic,
                                       QList<Layout::LineBox> &lines,
                                       QList<quint32> &glyphIds) {
    return loadParagraphShapes(blockId, contentHash, fontFamily, pixelSize, weight, italic, &lines, &glyphIds);
}

bool FragmentCache::storeCodeLine(quint64 blockId, int lineIndex, quint64 textHash,
                                  qreal width, const QList<Layout::LineBox> &lines,
                                  const QList<quint32> &glyphIds,
                                  int cost) {
    FragmentCacheValue v;
    v.lines = lines;
    v.glyphIds = glyphIds;
    int c = cost >= 0 ? cost : estimateCost(v);
    if (c > m_codeCache.maxCost())
        return false;
    FragmentCacheCodeKey k;
    k.blockId = blockId; k.lineIndex = lineIndex; k.textHash = textHash;
    k.width = qRound(width * 100);
    auto *val = new FragmentCacheValue(v);
    return m_codeCache.insert(k, val, c);
}

bool FragmentCache::loadCodeLine(quint64 blockId, int lineIndex, quint64 textHash,
                                qreal width, QList<Layout::LineBox> *lines,
                                QList<quint32> *glyphIds) {
    FragmentCacheCodeKey k;
    k.blockId = blockId; k.lineIndex = lineIndex; k.textHash = textHash;
    k.width = qRound(width * 100);
    if (auto *v = m_codeCache.object(k)) {
        if (lines) *lines = v->lines;
        if (glyphIds) *glyphIds = v->glyphIds;
        return true;
    }
    return false;
}

bool FragmentCache::loadCodeLine(quint64 blockId, int lineIndex, quint64 textHash,
                                qreal width, QList<Layout::LineBox> &lines,
                                QList<quint32> &glyphIds) {
    return loadCodeLine(blockId, lineIndex, textHash, width, &lines, &glyphIds);
}
