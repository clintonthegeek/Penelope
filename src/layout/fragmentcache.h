#ifndef PENELOPE_FRAGMENTCACHE_H
#define PENELOPE_FRAGMENTCACHE_H

#include "layoutengine.h"

#include <QCache>
#include <QList>
#include <QString>

struct FragmentCacheShapeKey {
    quint64 blockId = 0;
    quint64 contentHash = 0;
    QString fontFamily;
    int pixelSize = 0;
    int weight = 0;
    bool italic = false;
    bool operator==(const FragmentCacheShapeKey &o) const {
        return blockId == o.blockId && contentHash == o.contentHash
            && fontFamily == o.fontFamily && pixelSize == o.pixelSize
            && weight == o.weight && italic == o.italic;
    }
};
inline size_t qHash(const FragmentCacheShapeKey &k, size_t seed = 0) {
    return qHashMulti(seed, k.blockId, k.contentHash, k.fontFamily, k.pixelSize, k.weight, k.italic);
}

struct FragmentCacheBreakKey : FragmentCacheShapeKey {
    int width = 0;
    bool hyphenate = false;
    bool operator==(const FragmentCacheBreakKey &o) const {
        return FragmentCacheShapeKey::operator==(o) && width == o.width && hyphenate == o.hyphenate;
    }
};
inline size_t qHash(const FragmentCacheBreakKey &k, size_t seed = 0) {
    return qHashMulti(seed, static_cast<const FragmentCacheShapeKey &>(k), k.width, k.hyphenate);
}

struct FragmentCacheCodeKey {
    quint64 blockId = 0;
    int lineIndex = 0;
    quint64 textHash = 0;
    int width = 0;
    bool operator==(const FragmentCacheCodeKey &o) const {
        return blockId == o.blockId && lineIndex == o.lineIndex && textHash == o.textHash && width == o.width;
    }
};
inline size_t qHash(const FragmentCacheCodeKey &k, size_t seed = 0) {
    return qHashMulti(seed, k.blockId, k.lineIndex, k.textHash, k.width);
}

struct FragmentCacheValue {
    QList<Layout::LineBox> lines;
    QList<quint32> glyphIds;
};

class FragmentCache {
public:
    FragmentCache() {
        m_shapeCache.setMaxCost(4 * 1024 * 1024);
        m_breakCache.setMaxCost(4 * 1024 * 1024);
        m_codeCache.setMaxCost(4 * 1024 * 1024);
    }

    void setMaxCost(int bytes) {
        m_shapeCache.setMaxCost(bytes);
        m_breakCache.setMaxCost(bytes);
        m_codeCache.setMaxCost(bytes);
    }

    int totalCost() const { return m_shapeCache.totalCost() + m_breakCache.totalCost() + m_codeCache.totalCost(); }
    int maxCost() const { return m_shapeCache.maxCost(); }

    bool storeParagraph(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                        int pixelSize, int weight, bool italic,
                        qreal width, bool hyphenate,
                        const QList<Layout::LineBox> &lines,
                        const QList<quint32> &glyphIds = {},
                        int cost = -1);

    bool loadParagraphBreaks(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                             int pixelSize, int weight, bool italic,
                             qreal width, bool hyphenate,
                             QList<Layout::LineBox> *lines,
                             QList<quint32> *glyphIds = nullptr);
    bool loadParagraphBreaks(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                             int pixelSize, int weight, bool italic,
                             qreal width, bool hyphenate,
                             QList<Layout::LineBox> &lines,
                             QList<quint32> &glyphIds);

    bool loadParagraphShapes(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                             int pixelSize, int weight, bool italic,
                             QList<Layout::LineBox> *lines,
                             QList<quint32> *glyphIds = nullptr);
    bool loadParagraphShapes(quint64 blockId, quint64 contentHash, const QString &fontFamily,
                             int pixelSize, int weight, bool italic,
                             QList<Layout::LineBox> &lines,
                             QList<quint32> &glyphIds);

    bool storeCodeLine(quint64 blockId, int lineIndex, quint64 textHash,
                       qreal width, const QList<Layout::LineBox> &lines,
                       const QList<quint32> &glyphIds = {},
                       int cost = -1);

    bool loadCodeLine(quint64 blockId, int lineIndex, quint64 textHash,
                      qreal width, QList<Layout::LineBox> *lines,
                      QList<quint32> *glyphIds = nullptr);
    bool loadCodeLine(quint64 blockId, int lineIndex, quint64 textHash,
                      qreal width, QList<Layout::LineBox> &lines,
                      QList<quint32> &glyphIds);

    quint64 breakHits = 0;
    quint64 breakMisses = 0;

private:
    QCache<FragmentCacheShapeKey, FragmentCacheValue> m_shapeCache;
    QCache<FragmentCacheBreakKey, FragmentCacheValue> m_breakCache;
    QCache<FragmentCacheCodeKey, FragmentCacheValue> m_codeCache;
};

#endif
