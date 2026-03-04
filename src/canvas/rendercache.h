/*
 * rendercache.h — Async render cache with LRU eviction (Okular pattern)
 *
 * Renders PDF pages via Poppler in a background thread.
 * Caches rendered pixmaps with configurable memory limit.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_RENDERCACHE_H
#define PENELOPE_RENDERCACHE_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QThread>

namespace Poppler { class Document; class Page; }

class RenderCache : public QObject {
    Q_OBJECT
public:
    struct Request {
        int pageNumber = 0;
        int width = 0;
        int height = 0;
        qreal dpr = 1.0;       // device pixel ratio
        int priority = 0;      // lower = higher priority
    };

    explicit RenderCache(QObject *parent = nullptr);
    ~RenderCache() override;

    void setDocument(Poppler::Document *doc);
    void requestPixmap(const Request &req);
    QImage cachedPixmap(int page, int width, int height) const;
    void invalidateAll();

Q_SIGNALS:
    void pixmapReady(int pageNumber);

private Q_SLOTS:
    void onRenderFinished(int pageNumber, QImage image, int width, int height, int generation);

private:
    struct CacheEntry {
        QImage image;
        int width = 0;
        int height = 0;
        qint64 sizeBytes = 0;
        mutable qint64 lastAccess = 0;
    };

    struct CacheKey {
        int page;
        int width;
        int height;
        bool operator==(const CacheKey &o) const {
            return page == o.page && width == o.width && height == o.height;
        }
    };
    friend size_t qHash(const CacheKey &k, size_t seed) {
        return qHash(k.page, seed) ^ qHash(k.width, seed) ^ qHash(k.height, seed);
    }

    void evictIfNeeded();

    QHash<CacheKey, CacheEntry> m_cache;
    Poppler::Document *m_doc = nullptr;
    qint64 m_memoryLimit = 100 * 1024 * 1024; // 100MB default
    qint64 m_currentMemory = 0;
    mutable qint64 m_accessCounter = 0;
    mutable QMutex m_mutex;
    int m_generation = 0;

    // Render thread
    class RenderWorker;
    QThread m_renderThread;
    RenderWorker *m_worker = nullptr;
};

#endif // PENELOPE_RENDERCACHE_H
