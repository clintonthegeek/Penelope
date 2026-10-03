/*
 * rendercache.cpp — Async render cache with LRU eviction
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rendercache.h"

#include <QDebug>
#include <QMutexLocker>

#include <poppler-qt6.h>

// --- Render worker (runs in background thread) ---

class RenderCache::RenderWorker : public QObject {
    Q_OBJECT
public:
    RenderWorker() = default;

    void setDocument(Poppler::Document *doc, int generation) {
        QMutexLocker lock(&m_docMutex);
        m_doc = doc;
        m_generation = generation;
        clearQueue();
    }

    // Enqueue a render request. Replaces any prior request for the same page,
    // so only the latest requested size is rendered.
    void enqueue(int pageNumber, int width, int height, qreal dpr, int generation) {
        QMutexLocker lock(&m_queueMutex);
        m_queue[pageNumber] = {pageNumber, width, height, dpr, generation};
    }

    void clearQueue() {
        QMutexLocker lock(&m_queueMutex);
        m_queue.clear();
    }

public Q_SLOTS:
    void processQueue() {
        // Take one request from the queue
        PendingRequest req;
        {
            QMutexLocker lock(&m_queueMutex);
            if (m_queue.isEmpty())
                return;
            auto it = m_queue.begin();
            req = it.value();
            m_queue.erase(it);
        }

        // Render (expensive Poppler call)
        {
            QMutexLocker lock(&m_docMutex);
            if (req.generation != m_generation || !m_doc || req.pageNumber < 0 || req.pageNumber >= m_doc->numPages())
                goto scheduleMore;

            {
                std::unique_ptr<Poppler::Page> page(m_doc->page(req.pageNumber));
                if (!page)
                    goto scheduleMore;

                QSizeF pageSize = page->pageSizeF(); // in points (72 dpi)
                qreal xres = 72.0 * req.width / pageSize.width() * req.dpr;
                qreal yres = 72.0 * req.height / pageSize.height() * req.dpr;

                QImage image = page->renderToImage(xres, yres, -1, -1,
                                                   req.width * req.dpr, req.height * req.dpr);
                image.setDevicePixelRatio(req.dpr);

                int gen = m_generation;
                page.reset();
                lock.unlock();
                Q_EMIT finished(req.pageNumber, image, req.width, req.height, gen);
            }
        }

    scheduleMore:
        // If more work exists, yield to the event loop then continue.
        // This lets new enqueue() calls coalesce before we pick the next item.
        QMutexLocker lock(&m_queueMutex);
        if (!m_queue.isEmpty())
            QMetaObject::invokeMethod(this, "processQueue", Qt::QueuedConnection);
    }

Q_SIGNALS:
    void finished(int pageNumber, QImage image, int width, int height, int generation);

private:
    struct PendingRequest {
        int pageNumber = 0;
        int width = 0;
        int height = 0;
        qreal dpr = 1.0;
        int generation = 0;
    };

    Poppler::Document *m_doc = nullptr;
    int m_generation = 0;
    QMutex m_docMutex;
    QHash<int, PendingRequest> m_queue; // pageNumber -> latest request
    QMutex m_queueMutex;
};

// --- RenderCache ---

RenderCache::RenderCache(QObject *parent)
    : QObject(parent)
{
    m_worker = new RenderWorker;
    m_worker->moveToThread(&m_renderThread);

    connect(m_worker, &RenderWorker::finished,
            this, &RenderCache::onRenderFinished, Qt::QueuedConnection);

    m_renderThread.start();
}

RenderCache::~RenderCache()
{
    m_worker->clearQueue();
    m_renderThread.quit();
    m_renderThread.wait();
    delete m_worker;
}

void RenderCache::setDocument(Poppler::Document *doc)
{
    m_doc = doc;
    invalidateAll();
}

void RenderCache::requestPixmap(const Request &req)
{
    if (req.width <= 0 || req.height <= 0 || req.dpr <= 0) return;
    CacheKey key{req.pageNumber, req.width, req.height, req.dpr};

    {
        QMutexLocker lock(&m_mutex);
        if (m_cache.contains(key))
            return; // already cached
    }

    // Enqueue on worker — coalesces with any prior request for this page
    m_worker->enqueue(req.pageNumber, req.width, req.height, req.dpr, m_generation);
    QMetaObject::invokeMethod(m_worker, "processQueue", Qt::QueuedConnection);
}

QImage RenderCache::cachedPixmap(int page, int width, int height, qreal dpr) const
{
    QMutexLocker lock(&m_mutex);
    CacheKey key{page, width, height, dpr};
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        it->lastAccess = ++m_accessCounter;
        return it->image;
    }
    return {};
}

void RenderCache::invalidateAll()
{
    ++m_generation;
    m_worker->setDocument(m_doc, m_generation);
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_currentMemory = 0;
}

void RenderCache::onRenderFinished(int pageNumber, QImage image, int width, int height, int generation)
{
    if (image.isNull())
        return;

    // Discard stale results from a previous document generation
    if (generation != m_generation)
        return;

    CacheKey key{pageNumber, width, height, image.devicePixelRatio()};
    CacheEntry entry;
    entry.image = image;
    entry.width = width;
    entry.height = height;
    entry.sizeBytes = static_cast<qint64>(image.sizeInBytes());
    entry.lastAccess = ++m_accessCounter;

    {
        QMutexLocker lock(&m_mutex);
        auto existing = m_cache.find(key);
        if (existing != m_cache.end())
            m_currentMemory -= existing->sizeBytes;
        m_cache[key] = entry;
        m_currentMemory += entry.sizeBytes;
    }

    evictIfNeeded();
    Q_EMIT pixmapReady(pageNumber);
}

void RenderCache::evictIfNeeded()
{
    QMutexLocker lock(&m_mutex);
    while (m_currentMemory > m_memoryLimit && !m_cache.isEmpty()) {
        // Find LRU entry
        auto lruIt = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->lastAccess < lruIt->lastAccess)
                lruIt = it;
        }
        m_currentMemory -= lruIt->sizeBytes;
        m_cache.erase(lruIt);
    }
}

#include "rendercache.moc"
