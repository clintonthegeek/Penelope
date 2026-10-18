#include "documentview.h"
#include "pdfpageitem.h"
#include "boxpageitem.h"
#include "rendercache.h"
#include "webviewitem.h"
#include "pagelayout.h"
#include "contentrtfexporter.h"
#include "contentbuilder.h"
#include "contentplaintext.h"

#include <climits>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QGraphicsScene>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <poppler-qt6.h>

DocumentView::DocumentView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    // Hover hints (links, image titles) need move events without a button held.
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    setBackgroundBrush(QBrush(QColor(0x3c, 0x3c, 0x3c)));
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    // Default A4 page size in points (595 x 842)
    m_pageSize = QSizeF(595, 842);

    // Create render cache for PDF mode
    m_renderCache = new RenderCache(this);
    connect(m_renderCache, &RenderCache::pixmapReady,
            this, &DocumentView::onPixmapReady);

    // Web view relayout debounce timer
    m_relayoutTimer.setSingleShot(true);
    m_relayoutTimer.setInterval(50);
    connect(&m_relayoutTimer, &QTimer::timeout, this, [this]() {
        Q_EMIT webRelayoutRequested();
    });

    // Scrollbar keys, wheel steps, and dragging the bar are the reader moving.
    // Layout and contents jumps also set the bar value, and must keep their target.
    auto *bar = verticalScrollBar();
    connect(bar, &QAbstractSlider::sliderPressed, this, [this]() { noteUserScroll(); });
    connect(bar, &QAbstractSlider::actionTriggered, this, [this](int) { noteUserScroll(); });
}

DocumentView::~DocumentView()
{
    clearPdfPages();
    // Detach render cache before freeing the Poppler document —
    // blocks until any in-progress render finishes, preventing use-after-free.
    m_renderCache->setDocument(nullptr);
    delete m_popplerDoc;
}

// --- PDF path ---

void DocumentView::setPdfData(const QByteArray &pdf)
{
    clearSelection();
    // Clean up old PDF
    clearPdfPages();
    clearBoxPages();
    m_scene->clear();
    m_webViewItem = nullptr;

    m_pdfData = pdf;
    m_pdfMode = true;
    m_linkCache.clear();  // A7: invalidate link cache for new document
    m_sourceMap.clear();
    m_processedMarkdown.clear();
    m_contentDoc.blocks.clear();
    m_codeBlockRegions.clear();

    // Detach render cache before freeing the old Poppler document —
    // blocks until any in-progress render finishes, preventing use-after-free.
    m_renderCache->setDocument(nullptr);
    delete m_popplerDoc;
    m_popplerDoc = Poppler::Document::loadFromData(pdf).release();
    if (!m_popplerDoc) {
        m_pageCount = 0;
        return;
    }
    m_popplerDoc->setRenderHint(Poppler::Document::Antialiasing, true);
    m_popplerDoc->setRenderHint(Poppler::Document::TextAntialiasing, true);

    m_renderCache->setDocument(m_popplerDoc);
    m_pageCount = m_popplerDoc->numPages();

    layoutPages();

    if (!m_pdfPageItems.isEmpty()) {
        m_currentPage = 0;
        if (!m_skipAutoFit) {
            QTimer::singleShot(0, this, &DocumentView::fitWidth);
        }
        m_skipAutoFit = false;
    }
}

void DocumentView::clearPdfPages()
{
    for (auto *item : m_pdfPageItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_pdfPageItems.clear();
}

void DocumentView::clearBoxPages()
{
    for (auto *item : m_boxPageItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_boxPageItems.clear();
}

void DocumentView::layoutBoxPages()
{
    qreal yOffset = kPageGap;
    const qreal pageWidth = m_pageSize.width() > 0 ? m_pageSize.width()
                                                    : (m_boxPageItems.isEmpty() ? 0 : m_boxPageItems.first()->pageSize().width());
    const qreal sceneWidth = pageWidth + kSceneMargin * 2;
    for (auto *item : m_boxPageItems) {
        const QSizeF pageSize = item->pageSize();
        item->setPos((sceneWidth - pageSize.width()) / 2.0, yOffset);
        yOffset += pageSize.height() + kPageGap;
    }
    m_scene->setSceneRect(0, 0, sceneWidth, yOffset + kPageGap);
}

void DocumentView::onPixmapReady(int pageNumber)
{
    // Trigger repaint for the affected page item
    for (auto *item : m_pdfPageItems) {
        if (item->pageNumber() == pageNumber) {
            item->update();
            break;
        }
    }
}

// --- Common ---

ViewState DocumentView::saveViewState() const
{
    ViewState state;
    state.zoomPercent = m_currentZoom;
    state.currentPage = m_currentPage;
    auto *vbar = verticalScrollBar();
    if (vbar && vbar->maximum() > 0)
        state.scrollFraction = static_cast<qreal>(vbar->value()) / vbar->maximum();
    else
        state.scrollFraction = 0.0;
    state.valid = true;
    return state;
}

void DocumentView::restoreViewState(const ViewState &state, const ScrollAnchor &anchor)
{
    if (!state.valid)
        return;

    m_skipAutoFit = true;
    m_pendingPage = state.currentPage;
    setZoomPercent(state.zoomPercent);

    QTimer::singleShot(0, this, [this, state, anchor]() {
        auto *vbar = verticalScrollBar();
        if (vbar && vbar->maximum() > 0) {
            vbar->setValue(qRound(state.scrollFraction * vbar->maximum()));
        }
        if (m_pageCount > 0) {
            m_currentPage = qBound(0, state.currentPage, m_pageCount - 1);
            const bool continuous = m_viewMode == Continuous || m_viewMode == ContinuousFacing
                || m_viewMode == ContinuousFacingFirstAlone;
            if (!continuous && !m_webViewItem) layoutPages();
        }
        if (anchor.isValid())
            scrollToAnchor(anchor);
    });
}

// Maps a source-map entry into scene coordinates. Web columns start at the
// origin; paginated entries are offset by the page item they belong to.
bool DocumentView::sourceMapEntrySceneRect(const Layout::SourceMapEntry &entry, QRectF *rect) const
{
    if (m_webViewItem && m_renderMode == WebMode) {
        *rect = entry.rect.translated(m_webViewItem->pos());
        return true;
    }
    for (auto *item : m_boxPageItems) {
        if (item->pageNumber() == entry.pageNumber) {
            *rect = entry.rect.translated(item->pos());
            return true;
        }
    }
    for (auto *item : m_pdfPageItems) {
        if (item->pageNumber() == entry.pageNumber) {
            *rect = entry.rect.translated(item->pos());
            return true;
        }
    }
    return false;
}

ScrollAnchor DocumentView::viewportAnchor() const
{
    ScrollAnchor anchor;
    if (m_sourceMap.isEmpty() || !viewport())
        return anchor;

    const qreal top = mapToScene(viewport()->rect()).boundingRect().top();
    const Layout::SourceMapEntry *best = nullptr;
    QRectF bestRect;
    const Layout::SourceMapEntry *first = nullptr;
    QRectF firstRect;
    for (const auto &entry : m_sourceMap) {
        QRectF rect;
        if (!sourceMapEntrySceneRect(entry, &rect) || rect.height() <= 0)
            continue;
        if (!first || rect.top() < firstRect.top()) {
            first = &entry;
            firstRect = rect;
        }
        if (rect.top() > top + 0.5)
            continue;
        if (!best || rect.top() > bestRect.top()) {
            best = &entry;
            bestRect = rect;
        }
    }
    // Above the first mapped block the page margin is visible; anchor the
    // leading block so the top of the content stays at the top.
    if (!best) {
        best = first;
        bestRect = firstRect;
    }
    if (!best)
        return anchor;

    anchor.sourceLine = best->startLine;
    anchor.fraction = qBound(0.0, (top - bestRect.top()) / bestRect.height(), 1.0);
    anchor.context = anchorContext(*best);
    return anchor;
}

// Leading source text of a block, used to find it again after line shifts.
QString DocumentView::anchorContext(const Layout::SourceMapEntry &entry) const
{
    if (m_processedMarkdown.isEmpty() || entry.startLine <= 0)
        return {};
    const QStringList lines = m_processedMarkdown.split(QLatin1Char('\n'));
    for (int line = entry.startLine; line <= entry.endLine && line <= lines.size(); ++line) {
        const QString text = lines.at(line - 1).trimmed();
        if (!text.isEmpty())
            return text.left(120);
    }
    return {};
}

// Prefers the same line number, then the nearest line whose text still
// matches. Unmatched anchors keep the old line as the best approximation.
int DocumentView::resolveAnchorLine(const ScrollAnchor &anchor) const
{
    if (anchor.context.isEmpty() || m_processedMarkdown.isEmpty())
        return anchor.sourceLine;

    const QStringList lines = m_processedMarkdown.split(QLatin1Char('\n'));
    auto matches = [&lines, &anchor](int line) {
        return line >= 1 && line <= lines.size()
            && lines.at(line - 1).trimmed().left(120) == anchor.context;
    };
    if (matches(anchor.sourceLine))
        return anchor.sourceLine;

    int best = -1;
    int bestDistance = INT_MAX;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().left(120) != anchor.context)
            continue;
        const int distance = qAbs(i + 1 - anchor.sourceLine);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i + 1;
        }
    }
    return best > 0 ? best : anchor.sourceLine;
}

bool DocumentView::applyPendingAnchor()
{
    if (!m_pendingAnchor.isValid())
        return false;

    const int targetLine = resolveAnchorLine(m_pendingAnchor);
    // Only a block that actually contains the line proves the revision has
    // been laid out far enough. Picking the nearest earlier block would jump
    // to the end of a partial chunk instead of waiting for the real target.
    const Layout::SourceMapEntry *best = nullptr;
    QRectF bestRect;
    for (const auto &entry : m_sourceMap) {
        if (entry.startLine > targetLine || entry.endLine < targetLine)
            continue;
        QRectF rect;
        if (!sourceMapEntrySceneRect(entry, &rect) || rect.height() <= 0)
            continue;
        if (!best || entry.startLine > best->startLine) {
            best = &entry;
            bestRect = rect;
        }
    }
    if (!best)
        return false;

    const qreal targetY = bestRect.top()
        + qBound(0.0, m_pendingAnchor.fraction, 1.0) * bestRect.height();
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    m_applyingScroll = true;
    centerOn(visible.center().x(), targetY + visible.height() / 2.0);
    m_applyingScroll = false;

    // A partial chunk can map the block before enough content follows it to
    // bring it to the top. Keep the anchor until a later chunk makes it
    // reachable; clearing it here would leave the view at the chunk edge.
    const qreal viewTop = mapToScene(viewport()->rect().topLeft()).y();
    if (qAbs(viewTop - targetY) >= 32.0) {
        // At the end of the document the block may never reach the top.
        if (m_layoutCursor.finished)
            m_pendingAnchor = {};
        else
            requestMoreLayout();
        return true;
    }
    m_pendingAnchor = {};
    m_scrollChase = false;
    updateCurrentPage();
    return true;
}

void DocumentView::scrollToAnchor(const ScrollAnchor &anchor)
{
    if (!anchor.isValid()) {
        m_pendingAnchor = {};
        return;
    }
    m_pendingAnchor = anchor;
    if (applyPendingAnchor())
        return;
    // The anchored block has not been laid out in this revision yet. Follow-up
    // chunks call back through setHeadingPositions; until then the restored
    // fraction stays in place instead of chasing the partial scene edge.
    if (m_layoutCursor.finished)
        m_pendingAnchor = {};
    else
        requestMoreLayout();
}

// --- Layout ---

void DocumentView::layoutPages()
{
    if (m_webViewItem || !m_pdfMode) return;
    switch (m_viewMode) {
    case Continuous:                    layoutPagesContinuous(); break;
    case SinglePage:                    layoutPagesSingle(); break;
    case FacingPages:                   layoutPagesFacing(); break;
    case FacingPagesFirstAlone:         layoutPagesFacingFirstAlone(); break;
    case ContinuousFacing:              layoutPagesContinuousFacing(); break;
    case ContinuousFacingFirstAlone:    layoutPagesContinuousFacingFirstAlone(); break;
    }
    for (auto *item : m_pdfPageItems)
        item->setPageBackground(m_pageLayout.pageBackground);
}

void DocumentView::layoutPagesContinuous()
{
    if (!m_pdfMode) return;
    clearPdfPages();
    qreal yOffset = kPageGap;
    const qreal sceneWidth = m_pageSize.width() + kSceneMargin * 2;
    for (int i = 0; i < m_pageCount; ++i) {
        QSizeF pageSize = m_pageSize;
        if (m_popplerDoc) {
            std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(i));
            if (pp) pageSize = pp->pageSizeF();
        }
        auto *item = new PdfPageItem(i, pageSize, m_renderCache);
        item->setZoomFactor(m_currentZoom / 100.0);
        item->setPos((sceneWidth - pageSize.width()) / 2.0, yOffset);
        m_scene->addItem(item);
        m_pdfPageItems.append(item);
        yOffset += pageSize.height() + kPageGap;
    }
    m_scene->setSceneRect(0, 0, sceneWidth, yOffset + kPageGap);
}

void DocumentView::layoutPagesSingle()
{
    if (!m_pdfMode) return;
    clearPdfPages();

    if (m_pageCount == 0) return;
    int page = qBound(0, m_currentPage, m_pageCount - 1);

    QSizeF pageSize = m_pageSize;
    if (m_popplerDoc) {
        std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(page));
        if (pp) pageSize = pp->pageSizeF();
    }

    auto *item = new PdfPageItem(page, pageSize, m_renderCache);
    item->setZoomFactor(m_currentZoom / 100.0);
    item->setPos(kSceneMargin, kPageGap);
    m_scene->addItem(item);
    m_pdfPageItems.append(item);

    m_scene->setSceneRect(0, 0,
                          pageSize.width() + kSceneMargin * 2,
                          pageSize.height() + kPageGap * 2);
}

void DocumentView::layoutPagesFacing()
{
    if (!m_pdfMode) return;
    clearPdfPages();

    if (m_pageCount == 0) return;

    // Show current page and its facing partner
    int leftPage = (m_currentPage / 2) * 2;
    int rightPage = leftPage + 1;

    qreal maxHeight = 0;
    qreal totalWidth = 0;

    for (int p : {leftPage, rightPage}) {
        if (p >= m_pageCount) continue;
        QSizeF pageSize = m_pageSize;
        if (m_popplerDoc) {
            std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(p));
            if (pp) pageSize = pp->pageSizeF();
        }

        auto *item = new PdfPageItem(p, pageSize, m_renderCache);
        item->setZoomFactor(m_currentZoom / 100.0);
        item->setPos(kSceneMargin + totalWidth, kPageGap);
        m_scene->addItem(item);
        m_pdfPageItems.append(item);

        totalWidth += pageSize.width() + kPageGap;
        maxHeight = qMax(maxHeight, pageSize.height());
    }

    m_scene->setSceneRect(0, 0,
                          totalWidth + kSceneMargin * 2,
                          maxHeight + kPageGap * 2);
}

void DocumentView::layoutPagesContinuousFacing()
{
    if (!m_pdfMode) return;
    clearPdfPages();

    qreal yOffset = kPageGap;
    qreal maxWidth = m_pageSize.width() * 2 + kPageGap + kSceneMargin * 2;

    for (int i = 0; i < m_pageCount; i += 2) {
        qreal rowHeight = 0;
        qreal xOffset = kSceneMargin;

        for (int j = 0; j < 2 && (i + j) < m_pageCount; ++j) {
            int page = i + j;
            QSizeF pageSize = m_pageSize;
            if (m_popplerDoc) {
                std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(page));
                if (pp) pageSize = pp->pageSizeF();
            }

            auto *item = new PdfPageItem(page, pageSize, m_renderCache);
            item->setZoomFactor(m_currentZoom / 100.0);
            item->setPos(xOffset, yOffset);
            m_scene->addItem(item);
            m_pdfPageItems.append(item);

            xOffset += pageSize.width() + kPageGap;
            rowHeight = qMax(rowHeight, pageSize.height());
        }

        yOffset += rowHeight + kPageGap;
    }

    m_scene->setSceneRect(0, 0, maxWidth, yOffset + kPageGap);
}

void DocumentView::layoutPagesFacingFirstAlone()
{
    if (!m_pdfMode) return;
    clearPdfPages();

    if (m_pageCount == 0) return;

    // First page standalone: page 0 alone, then 1-2, 3-4, etc.
    // Determine which spread contains the current page
    int spreadStart;
    if (m_currentPage == 0) {
        spreadStart = 0;
    } else {
        // Pages 1-2 = spread 1, 3-4 = spread 2, etc.
        spreadStart = 1 + ((m_currentPage - 1) / 2) * 2;
    }

    qreal maxHeight = 0;
    qreal totalWidth = 0;
    int spreadEnd = (spreadStart == 0) ? 1 : spreadStart + 2;

    for (int p = spreadStart; p < spreadEnd && p < m_pageCount; ++p) {
        QSizeF pageSize = m_pageSize;
        if (m_popplerDoc) {
            std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(p));
            if (pp) pageSize = pp->pageSizeF();
        }

        auto *item = new PdfPageItem(p, pageSize, m_renderCache);
        item->setZoomFactor(m_currentZoom / 100.0);
        item->setPos(kSceneMargin + totalWidth, kPageGap);
        m_scene->addItem(item);
        m_pdfPageItems.append(item);

        totalWidth += pageSize.width() + kPageGap;
        maxHeight = qMax(maxHeight, pageSize.height());
    }

    m_scene->setSceneRect(0, 0,
                          totalWidth + kSceneMargin * 2,
                          maxHeight + kPageGap * 2);
}

void DocumentView::layoutPagesContinuousFacingFirstAlone()
{
    if (!m_pdfMode) return;
    clearPdfPages();

    qreal yOffset = kPageGap;
    qreal maxWidth = m_pageSize.width() * 2 + kPageGap + kSceneMargin * 2;

    // Page 0 is alone (centered), then 1-2, 3-4, etc.
    if (m_pageCount > 0) {
        QSizeF pageSize = m_pageSize;
        if (m_popplerDoc) {
            std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(0));
            if (pp) pageSize = pp->pageSizeF();
        }

        auto *item = new PdfPageItem(0, pageSize, m_renderCache);
        item->setZoomFactor(m_currentZoom / 100.0);
        // Center the first page (offset to the right half of the spread)
        qreal xOffset = kSceneMargin + m_pageSize.width() + kPageGap;
        xOffset = (maxWidth - pageSize.width()) / 2.0;
        item->setPos(xOffset, yOffset);
        m_scene->addItem(item);
        m_pdfPageItems.append(item);

        yOffset += pageSize.height() + kPageGap;
    }

    // Remaining pages in pairs: 1-2, 3-4, 5-6, ...
    for (int i = 1; i < m_pageCount; i += 2) {
        qreal rowHeight = 0;
        qreal xOffset = kSceneMargin;

        for (int j = 0; j < 2 && (i + j) < m_pageCount; ++j) {
            int page = i + j;
            QSizeF pageSize = m_pageSize;
            if (m_popplerDoc) {
                std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(page));
                if (pp) pageSize = pp->pageSizeF();
            }

            auto *item = new PdfPageItem(page, pageSize, m_renderCache);
            item->setZoomFactor(m_currentZoom / 100.0);
            item->setPos(xOffset, yOffset);
            m_scene->addItem(item);
            m_pdfPageItems.append(item);

            xOffset += pageSize.width() + kPageGap;
            rowHeight = qMax(rowHeight, pageSize.height());
        }

        yOffset += rowHeight + kPageGap;
    }

    m_scene->setSceneRect(0, 0, maxWidth, yOffset + kPageGap);
}

// --- Page size/layout ---

void DocumentView::setPageLayout(const PageLayout &layout)
{
    m_pageLayout = layout;
    m_pageSize = layout.pageSizePoints();

    if (m_renderMode == WebMode)
        applyRenderChrome();

    if (m_pdfMode) {
        layoutPages();
    }
}

// --- Zoom ---

void DocumentView::setZoomPercent(int percent)
{
    percent = qBound(25, percent, 400);
    if (percent == m_currentZoom)
        return;

    qreal factor = percent / 100.0;
    resetTransform();
    scale(factor, factor);
    m_currentZoom = percent;

    if (m_renderMode == WebMode) {
        // Zoom changes the column width. Coalesce with the resize timer so a
        // zoom applied before the window has its real size does not install
        // a throwaway layout.
        Q_EMIT zoomChanged(percent);
        scheduleWebRelayout();
        return;
    }

    // Update PDF page items zoom
    for (auto *item : m_pdfPageItems)
        item->setZoomFactor(factor);

    Q_EMIT zoomChanged(percent);
}

void DocumentView::zoomIn()
{
    setZoomPercent(m_currentZoom + 10);
}

void DocumentView::zoomOut()
{
    setZoomPercent(m_currentZoom - 10);
}

void DocumentView::fitWidth()
{
    qreal pageWidth = m_pageSize.width();
    if (!m_pdfPageItems.isEmpty())
        pageWidth = m_pdfPageItems.first()->boundingRect().width();
    else
        return;

    qreal viewWidth = viewport()->width();
    qreal factor = viewWidth / (pageWidth + kSceneMargin);
    resetTransform();
    scale(factor, factor);
    m_currentZoom = qRound(factor * 100);

    for (auto *item : m_pdfPageItems)
        item->setZoomFactor(factor);

    Q_EMIT zoomChanged(m_currentZoom);
}

void DocumentView::fitPage()
{
    QRectF pageRect;
    if (!m_pdfPageItems.isEmpty())
        pageRect = m_pdfPageItems.first()->boundingRect();
    else
        return;

    pageRect.adjust(-20, -20, 20, 20);
    fitInView(pageRect, Qt::KeepAspectRatio);
    m_currentZoom = qRound(transform().m11() * 100);

    for (auto *item : m_pdfPageItems)
        item->setZoomFactor(transform().m11());

    Q_EMIT zoomChanged(m_currentZoom);
}

// --- Navigation ---

void DocumentView::goToPage(int page)
{
    if (page < 0 || page >= m_pageCount) return;
    m_pendingAnchor = {};
    m_currentPage = page;
    if (!m_pdfMode) {
        for (auto *item : m_boxPageItems) {
            if (item->pageNumber() == page) {
                // ensureVisible on a page taller than the viewport pins the
                // bottom edge, so the center falls on the next page.
                m_applyingScroll = true;
                centerOn(item->sceneBoundingRect().center());
                m_applyingScroll = false;
                m_currentPage = page;
                break;
            }
        }
        return;
    }
    if (!isContinuous()) {
        layoutPages();
    } else {
        for (auto *item : m_pdfPageItems) {
            if (item->pageNumber() == page) {
                ensureVisible(item);
                break;
            }
        }
    }
}

void DocumentView::noteUserScroll()
{
    if (m_applyingScroll)
        return;
    m_scrollChase = false;
    m_pendingHeadingLine = -1;
    m_pendingAnchor = {};
}

void DocumentView::scrollToPosition(int page, qreal yOffset)
{
    m_scrollChase = true;
    m_scrollChasePage = page;
    m_scrollChaseY = yOffset;
    applyScrollChase();
}

void DocumentView::requestMoreLayout()
{
    if (m_layoutCursor.finished || m_moreRequested)
        return;
    m_moreRequested = true;
    Q_EMIT moreLayoutRequested();
}

bool DocumentView::headingSceneY(const HeadingPosition &hp, qreal *sceneY) const
{
    if (m_renderMode == WebMode && m_webViewItem) {
        *sceneY = m_webViewItem->pos().y() + hp.yOffset;
        return true;
    }
    for (auto *item : m_boxPageItems) {
        if (item->pageNumber() == hp.page) {
            *sceneY = item->pos().y() + hp.yOffset;
            return true;
        }
    }
    for (auto *item : m_pdfPageItems) {
        if (item->pageNumber() == hp.page) {
            *sceneY = item->pos().y() + hp.yOffset;
            return true;
        }
    }
    return false;
}

void DocumentView::applyScrollChase()
{
    if (!m_scrollChase)
        return;

    // Put the heading at the top of the viewport. Centering it leaves the
    // previous heading as the one the contents list would highlight.
    auto alignTop = [this](const QPointF &scenePoint) {
        const qreal visibleH = mapToScene(viewport()->rect()).boundingRect().height();
        constexpr qreal margin = 8;
        centerOn(scenePoint.x(), scenePoint.y() + visibleH / 2.0 - margin);
    };

    const int page = m_scrollChasePage;
    const qreal yOffset = m_scrollChaseY;
    bool placed = false;
    m_applyingScroll = true;
    if (m_renderMode == WebMode && m_webViewItem) {
        const QRectF bounds = m_webViewItem->mapRectToScene(m_webViewItem->boundingRect());
        alignTop(QPointF(bounds.center().x(), m_webViewItem->pos().y() + yOffset));
        placed = true;
    } else {
        for (auto *item : m_boxPageItems) {
            if (item->pageNumber() == page) {
                m_currentPage = page;
                alignTop(item->pos() + QPointF(item->boundingRect().width() / 2.0, yOffset));
                placed = true;
                break;
            }
        }
        if (!placed && m_pdfMode && page >= 0 && page < m_pageCount) {
            m_currentPage = page;
            const bool isContinuous = (m_viewMode == Continuous || m_viewMode == ContinuousFacing
                                       || m_viewMode == ContinuousFacingFirstAlone);
            if (!isContinuous)
                layoutPages();
            for (auto *item : m_pdfPageItems) {
                if (item->pageNumber() == page) {
                    alignTop(item->pos() + QPointF(item->boundingRect().width() / 2.0, yOffset));
                    placed = true;
                    break;
                }
            }
        }
    }
    m_applyingScroll = false;

    HeadingPosition target;
    target.page = page;
    target.yOffset = yOffset;
    qreal sceneY = 0;
    const qreal viewTop = mapToScene(viewport()->rect().topLeft()).y();
    const bool reached = placed && headingSceneY(target, &sceneY) && sceneY <= viewTop + 32;
    if (reached || (placed && m_layoutCursor.finished))
        m_scrollChase = false;
    else {
        if (!placed) {
            m_applyingScroll = true;
            if (auto *bar = verticalScrollBar())
                bar->setValue(bar->maximum());
            m_applyingScroll = false;
        }
        requestMoreLayout();
    }
    updateCurrentPage();
}

void DocumentView::maintainHeadingScroll()
{
    if (m_pendingHeadingLine > 0) {
        for (const auto &hp : m_headingPositions) {
            if (hp.sourceLine != m_pendingHeadingLine)
                continue;
            m_pendingHeadingLine = -1;
            scrollToPosition(m_renderMode == WebMode ? 0 : hp.page, hp.yOffset);
            return;
        }
        m_applyingScroll = true;
        if (auto *bar = verticalScrollBar())
            bar->setValue(bar->maximum());
        m_applyingScroll = false;
        requestMoreLayout();
        updateCurrentPage();
        return;
    }
    applyScrollChase();
}

void DocumentView::followHeading(int sourceLine)
{
    if (sourceLine <= 0)
        return;
    m_pendingAnchor = {};
    for (const auto &hp : m_headingPositions) {
        if (hp.sourceLine == sourceLine) {
            scrollToPosition(m_renderMode == WebMode ? 0 : hp.page, hp.yOffset);
            return;
        }
    }
    m_scrollChase = false;
    m_pendingHeadingLine = sourceLine;
    maintainHeadingScroll();
}

void DocumentView::setHeadingPositions(const QList<HeadingPosition> &positions)
{
    m_headingPositions = positions;
    maintainHeadingScroll();
    // Follow-up chunks extend the source map; the anchor may now be laid out.
    if (m_pendingAnchor.isValid())
        scrollToAnchor(m_pendingAnchor);
    updateCurrentPage();
}

void DocumentView::previousPage()
{
    if (m_currentPage > 0)
        goToPage(m_currentPage - 1);
}

void DocumentView::nextPage()
{
    if (m_currentPage < m_pageCount - 1)
        goToPage(m_currentPage + 1);
}

// --- View mode ---

void DocumentView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode)
        return;
    m_viewMode = mode;

    // Web view is one continuous column. Page arrangement must not turn its scrollbar off.
    if (m_renderMode == WebMode)
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    else {
        const bool continuous = (mode == Continuous || mode == ContinuousFacing
                                 || mode == ContinuousFacingFirstAlone);
        setVerticalScrollBarPolicy(continuous ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    }

    layoutPages();
}

// --- Render mode ---

void DocumentView::applyRenderChrome()
{
    const QColor page = m_pageLayout.pageBackground.isValid()
        ? m_pageLayout.pageBackground : QColor(Qt::white);
    const QColor bg = m_renderMode == WebMode ? page : QColor(0x3c, 0x3c, 0x3c);
    setBackgroundBrush(bg);
    if (m_renderMode == WebMode && m_webViewItem)
        m_webViewItem->setPageBackground(page);
    if (viewport()) {
        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Base, bg);
        pal.setColor(QPalette::Window, bg);
        viewport()->setPalette(pal);
        viewport()->setAutoFillBackground(false);
    }
}

void DocumentView::setRenderMode(RenderMode mode)
{
    if (m_renderMode != mode) {
        m_renderMode = mode;
        m_moreRequested = false;
        m_scrollChase = false;
        m_pendingHeadingLine = -1;
        if (mode == WebMode) {
            // One full repaint per update. The scroll blit plus a growing column
            // leaves a second copy of the text offset from the first.
            setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
            setTransformationAnchor(QGraphicsView::NoAnchor);
            setResizeAnchor(QGraphicsView::NoAnchor);
            // Drop print pages now. Waiting for the async web result leaves them
            // painted under the new column, and single-page mode has no scrollbar.
            clearPdfPages();
            clearBoxPages();
            m_pdfMode = false;
            m_renderCache->setDocument(nullptr);
            delete m_popplerDoc;
            m_popplerDoc = nullptr;
            m_pdfData.clear();
            if (m_webViewItem)
                m_scene->setSceneRect(m_webViewItem->mapRectToScene(m_webViewItem->boundingRect()));
            else
                m_scene->setSceneRect(0, 0, 1, 1);
            setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        } else {
            setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
            setResizeAnchor(QGraphicsView::AnchorViewCenter);
        }
        if (mode != WebMode && m_webViewItem) {
            m_scene->removeItem(m_webViewItem);
            delete m_webViewItem;
            m_webViewItem = nullptr;
            m_scene->setSceneRect(0, 0, 1, 1);
            const bool continuous = isContinuous();
            setVerticalScrollBarPolicy(continuous ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        }
    }
    applyRenderChrome();
    if (viewport())
        viewport()->update();
}

void DocumentView::setWebContent(Layout::ContinuousLayoutResult &&result)
{
    Q_ASSERT(m_webFontManager);
    // Clearing the scene hides the scrollbar and showing the column brings it
    // back. That resize must not schedule another full relayout.
    m_applyingWebContent = true;
    clearSelection();
    m_renderCache->setDocument(nullptr);
    delete m_popplerDoc;
    m_popplerDoc = nullptr;
    m_pdfData.clear();
    m_pdfMode = false;
    m_linkCache.clear();
    m_pageCount = 1;
    m_currentPage = 0;

    m_scene->clear();
    m_webViewItem = nullptr;
    m_pdfPageItems.clear();
    m_boxPageItems.clear();
    m_moreRequested = false;

    m_webViewItem = new WebViewItem(m_webFontManager, m_webFontOwner);
    m_webViewItem->setPageBackground(m_pageLayout.pageBackground);
    m_scene->addItem(m_webViewItem);

    m_webViewItem->setLayoutResult(std::move(result));
    m_webViewItem->setPos(0, 0);

    qreal sceneW = m_webViewItem->boundingRect().width();
    qreal sceneH = m_webViewItem->boundingRect().height();
    m_scene->setSceneRect(0, 0, sceneW, sceneH);

    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_applyingWebContent = false;
}

void DocumentView::layoutExtended()
{
    m_moreRequested = false;
}

void DocumentView::scheduleMoreLayout()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_renderMode == WebMode && !m_layoutCursor.finished)
            Q_EMIT moreLayoutRequested();
    });
}

void DocumentView::scheduleWebRelayout()
{
    if (m_renderMode == WebMode)
        m_relayoutTimer.start();
}

void DocumentView::requestScrollToBlock(int blockIndex)
{
    m_scrollToBlock = blockIndex;
}

void DocumentView::integrateCursor(const Layout::LayoutCursor &cursor, int blockIndex, qreal blockY)
{
    m_layoutCursor = cursor;
    if (blockIndex >= 0)
        m_blockY.insert(blockIndex, blockY);
    if (m_scrollToBlock >= 0 && cursor.nextBlock > m_scrollToBlock) {
        m_scrollTargetY = m_blockY.value(m_scrollToBlock, blockY);
        const int target = m_scrollToBlock;
        m_scrollToBlock = -1;
        if (m_renderMode == WebMode && m_webViewItem)
            scrollToPosition(0, m_blockY.value(target, m_scrollTargetY));
    }
}

void DocumentView::setBoxPages(const QList<std::shared_ptr<const Layout::Page>> &pages,
                               std::shared_ptr<FontManager> fonts, const PageLayout &layout,
                               bool append, const QString &fileName, const QString &title)
{
    m_pageLayout = layout;
    m_pageSize = layout.pageSizePoints();
    if (m_webViewItem) {
        m_scene->removeItem(m_webViewItem);
        delete m_webViewItem;
        m_webViewItem = nullptr;
    }
    if (!append) {
        clearSelection();
        m_renderCache->setDocument(nullptr);
        delete m_popplerDoc;
        m_popplerDoc = nullptr;
        m_pdfData.clear();
        m_pdfMode = false;
        m_linkCache.clear();
        clearPdfPages();
        clearBoxPages();
        m_scene->clear();
        m_webViewItem = nullptr;
        m_pdfPageItems.clear();
        m_boxPageItems.clear();
    }
    const int totalPages = append ? m_boxPageItems.size() + pages.size() : pages.size();
    for (const auto &page : pages) {
        auto *item = new BoxPageItem(page, m_pageSize, fonts);
        item->setPageChrome(layout, qMax(1, totalPages), fileName, title);
        m_scene->addItem(item);
        m_boxPageItems.append(item);
    }
    for (auto *item : m_boxPageItems)
        item->setPageChrome(layout, qMax(1, m_boxPageItems.size()), fileName, title);
    m_pageCount = m_boxPageItems.size();
    m_pdfMode = false;
    layoutBoxPages();
    if (!append)
        m_currentPage = 0;
    if (m_pendingPage >= 0 && m_pendingPage < m_pageCount)
        goToPage(m_pendingPage);
}

void DocumentView::appendWebContent(Layout::ContinuousLayoutResult &&extra)
{
    Q_ASSERT(m_webViewItem);
    Q_ASSERT(!m_pdfMode);
    auto &res = m_webViewItem->layoutResult();
    const qreal baseHeight = res.totalHeight;
    for (auto &el : extra.elements)
        std::visit([&](auto &e) { e.y += baseHeight; }, el);
    for (auto &sm : extra.sourceMap)
        sm.rect.translate(0, baseHeight);
    const qreal newHeight = baseHeight + extra.totalHeight;
    auto elements = std::move(extra.elements);
    m_webViewItem->appendElements(std::move(elements), newHeight);
    auto &stored = m_webViewItem->layoutResult();
    stored.sourceMap.append(extra.sourceMap);
    stored.codeBlockRegions.append(extra.codeBlockRegions);
    if (extra.contentWidth > 0)
        stored.contentWidth = extra.contentWidth;
    m_sourceMap.append(extra.sourceMap);
    m_codeBlockRegions.append(extra.codeBlockRegions);
    const qreal sceneW = m_webViewItem->boundingRect().width();
    const qreal sceneH = m_webViewItem->boundingRect().height();
    m_scene->setSceneRect(0, 0, sceneW, sceneH);
    layoutExtended();
}

// --- Events ---

void DocumentView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y() > 0 ? 10 : -10;
        setZoomPercent(m_currentZoom + delta);
        centerOn(mapToScene(event->position().toPoint()));
        event->accept();
    } else {
        noteUserScroll();
        QGraphicsView::wheelEvent(event);
    }
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_applyingWebContent)
        return;
    if (m_renderMode == WebMode
        && event->size().width() != event->oldSize().width())
        scheduleWebRelayout();
}

void DocumentView::scrollContentsBy(int dx, int dy)
{
    // Web mode: suppress horizontal scrolling entirely, like a browser
    if (m_renderMode == WebMode)
        dx = 0;
    QGraphicsView::scrollContentsBy(dx, dy);
    updateCurrentPage();
    if (m_renderMode == WebMode && !m_layoutCursor.finished && !m_moreRequested) {
        int max = verticalScrollBar()->maximum();
        int val = verticalScrollBar()->value();
        int vp = viewport()->height();
        if (max - val < vp) {
            m_moreRequested = true;
            Q_EMIT moreLayoutRequested();
        }
    }
}

// --- Middle-mouse smooth zoom (Okular pattern) ---
// Middle-click + drag up → zoom in, drag down → zoom out, centered on click point.

void DocumentView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_cursorMode == HandTool)
        noteUserScroll();
    if (event->button() == Qt::MiddleButton) {
        m_middleZooming = true;
        m_middleZoomOrigin = event->pos();
        m_middleZoomSceneAnchor = mapToScene(event->pos());
        m_middleZoomStartPercent = m_currentZoom;
        setCursor(Qt::SizeVerCursor);
        event->accept();
    } else if (event->button() == Qt::LeftButton && m_renderMode == WebMode && m_webViewItem) {
        // Web mode: check for link click before selection
        QPointF scenePos = mapToScene(event->pos());
        QPointF itemPos = m_webViewItem->mapFromScene(scenePos);
        QString href = m_webViewItem->linkAt(itemPos);
        if (!href.isEmpty()) {
            if (!activateInternalLink(href))
                QDesktopServices::openUrl(QUrl(href));
            event->accept();
            return;
        }
        if (m_cursorMode == SelectionTool) {
            m_selectPressPos = mapToScene(event->pos());
            m_selectCurrentPos = m_selectPressPos;
            m_wordSelection = false;
            clearSelection();
            event->accept();
        } else {
            QGraphicsView::mousePressEvent(event);
        }
    } else if (event->button() == Qt::LeftButton && !m_boxPageItems.isEmpty() && m_cursorMode != SelectionTool) {
        const QPointF scenePos = mapToScene(event->pos());
        for (auto *item : m_boxPageItems) {
            const QRectF itemRect = QRectF(0, 0, item->pageSize().width(), item->pageSize().height())
                                        .translated(item->pos());
            if (!itemRect.contains(scenePos))
                continue;
            const QString href = item->linkAt(item->mapFromScene(scenePos));
            if (!href.isEmpty()) {
                QDesktopServices::openUrl(QUrl(href));
                event->accept();
                return;
            }
            break;
        }
        QGraphicsView::mousePressEvent(event);
    } else if (event->button() == Qt::LeftButton && m_cursorMode == SelectionTool) {
        // B2: Start selection (don't select yet — wait for threshold)
        m_selectPressPos = mapToScene(event->pos());
        m_selectCurrentPos = m_selectPressPos;
        m_wordSelection = false;
        clearSelection();
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}

void DocumentView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_middleZooming) {
        // Vertical delta: drag up = positive = zoom in, drag down = negative = zoom out
        int dy = m_middleZoomOrigin.y() - event->pos().y();
        // 200 pixels of drag = double/halve the zoom
        qreal sensitivity = 200.0;
        qreal factor = qPow(2.0, dy / sensitivity);
        int newZoom = qBound(25, qRound(m_middleZoomStartPercent * factor), 400);

        if (newZoom != m_currentZoom) {
            qreal scaleFactor = newZoom / 100.0;
            resetTransform();
            scale(scaleFactor, scaleFactor);
            m_currentZoom = newZoom;

            centerOn(m_middleZoomSceneAnchor);
            Q_EMIT zoomChanged(m_currentZoom);
        }
        event->accept();
    } else if (m_cursorMode == SelectionTool && (event->buttons() & Qt::LeftButton)) {
        // B2: Selection drag
        QPointF scenePos = mapToScene(event->pos());
        if (!m_textSelecting) {
            // Check 5px threshold before starting selection
            QPointF delta = scenePos - m_selectPressPos;
            if (QPointF::dotProduct(delta, delta) > kSelectionThreshold * kSelectionThreshold) {
                m_textSelecting = true;
            }
        }
        if (m_textSelecting) {
            m_selectCurrentPos = scenePos;
            updateTextSelection();
        }
        event->accept();
    } else {
        QGraphicsView::mouseMoveEvent(event);

        // A7: Check for link hover (only when not dragging)
        if (m_cursorMode != SelectionTool || !(event->buttons() & Qt::LeftButton)) {
            checkLinkHover(mapToScene(event->pos()));
        }
    }
}

void DocumentView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_middleZooming) {
        m_middleZooming = false;
        setCursor(Qt::ArrowCursor);

        if (m_renderMode == WebMode) {
            // Web mode: reflow content at the final zoom level
            Q_EMIT webRelayoutRequested();
        } else {
            // Print mode: crisp Poppler re-render at the final zoom level
            qreal scaleFactor = m_currentZoom / 100.0;
            for (auto *item : m_pdfPageItems)
                item->setZoomFactor(scaleFactor);
        }

        event->accept();
    } else if (event->button() == Qt::LeftButton && m_cursorMode == SelectionTool) {
        // B2: Finish selection — highlights stay until cleared by next press or mode switch
        m_textSelecting = false;
        event->accept();
    } else {
        QGraphicsView::mouseReleaseEvent(event);
    }
}

void DocumentView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_cursorMode == SelectionTool
        && m_pdfMode && m_popplerDoc) {
        // B2: Double-click to select a word
        QPointF scenePos = mapToScene(event->pos());

        for (auto *pageItem : m_pdfPageItems) {
            QRectF itemRect = QRectF(0, 0, pageItem->pageSize().width(),
                                      pageItem->pageSize().height())
                                  .translated(pageItem->pos());
            if (!itemRect.contains(scenePos))
                continue;

            int pageNum = pageItem->pageNumber();
            QSizeF pageSz = pageItem->pageSize();
            QPointF localPos = scenePos - pageItem->pos();

            std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(pageNum));
            if (!pp)
                break;

            auto textBoxes = pp->textList();

            for (const auto &tb : textBoxes) {
                QRectF tbRect = tb->boundingBox();
                if (tbRect.contains(localPos)) {
                    // Select this word
                    clearSelection();
                    m_wordSelection = true;
                    pageItem->setSelectionRects({tbRect});
                    m_pagesWithSelection.insert(pageNum);
                    // Store selection extents for extractSelectedText
                    m_selectPressPos = QPointF(tbRect.topLeft()) + pageItem->pos();
                    m_selectCurrentPos = QPointF(tbRect.bottomRight()) + pageItem->pos();
                    // Auto-copy word (Okular pattern)
                    auto *mimeData = new QMimeData;
                    mimeData->setText(tb->text());
                    QApplication::clipboard()->setMimeData(mimeData);
                    break;
                }
            }
            break;
        }
        event->accept();
    } else {
        QGraphicsView::mouseDoubleClickEvent(event);
    }
}

// --- Source breadcrumbs ---

void DocumentView::setSourceData(const QString &processedMarkdown,
                                  const QList<Layout::SourceMapEntry> &sourceMap,
                                  const Content::Document &contentDoc,
                                  const QList<Layout::CodeBlockRegion> &codeBlockRegions)
{
    m_processedMarkdown = processedMarkdown;
    m_sourceMap = sourceMap;
    m_contentDoc = contentDoc;
    m_codeBlockRegions = codeBlockRegions;
}

// --- Code block language overrides ---

void DocumentView::setCodeBlockLanguageOverrides(const QHash<QString, QString> &overrides)
{
    m_codeBlockLanguageOverrides = overrides;
}

QHash<QString, QString> DocumentView::codeBlockLanguageOverrides() const
{
    return m_codeBlockLanguageOverrides;
}

void DocumentView::applyLanguageOverrides(Content::Document &doc) const
{
    if (m_codeBlockLanguageOverrides.isEmpty())
        return;

    const auto apply = [this](auto &&self, QList<Content::BlockNode> &blocks) -> void {
        for (auto &block : blocks) {
            if (auto *cb = std::get_if<Content::CodeBlock>(&block)) {
                auto it = m_codeBlockLanguageOverrides.constFind(cb->code.trimmed());
                if (it != m_codeBlockLanguageOverrides.cend()) cb->language = it.value();
            } else if (auto *quote = std::get_if<Content::BlockQuote>(&block)) {
                self(self, quote->children);
            } else if (auto *list = std::get_if<Content::List>(&block)) {
                for (auto &item : list->items) self(self, item.children);
            }
        }
    };
    apply(apply, doc.blocks);
}

const Content::CodeBlock *DocumentView::codeBlockAtScenePos(const QPointF &scenePos) const
{
    if (m_codeBlockRegions.isEmpty() || m_contentDoc.blocks.isEmpty())
        return nullptr;

    // Determine local coordinates depending on render mode
    QPointF localPos;
    int pageNum = 0;

    if (m_pdfMode) {
        // Print view: find which page was clicked
        bool found = false;
        for (auto *pageItem : m_pdfPageItems) {
            QRectF itemRect = QRectF(0, 0, pageItem->pageSize().width(),
                                      pageItem->pageSize().height())
                                  .translated(pageItem->pos());
            if (itemRect.contains(scenePos)) {
                pageNum = pageItem->pageNumber();
                localPos = scenePos - pageItem->pos();
                found = true;
                break;
            }
        }
        if (!found)
            return nullptr;
    } else if (m_webViewItem) {
        // Web view: single continuous item, page 0
        localPos = scenePos - m_webViewItem->pos();
    } else {
        return nullptr;
    }

    // Check code block hit regions (provided by layout engine)
    for (const auto &region : m_codeBlockRegions) {
        if (region.pageNumber != pageNum)
            continue;
        if (!region.rect.contains(localPos))
            continue;

        const auto find = [&region](auto &&self, const QList<Content::BlockNode> &blocks)
            -> const Content::CodeBlock * {
            for (const auto &block : blocks) {
                if (const auto *cb = std::get_if<Content::CodeBlock>(&block)) {
                    if (cb->source.startLine == region.startLine
                        && cb->source.endLine == region.endLine) return cb;
                } else if (const auto *quote = std::get_if<Content::BlockQuote>(&block)) {
                    if (const auto *cb = self(self, quote->children)) return cb;
                } else if (const auto *list = std::get_if<Content::List>(&block)) {
                    for (const auto &item : list->items)
                        if (const auto *cb = self(self, item.children)) return cb;
                }
            }
            return nullptr;
        };
        if (const auto *cb = find(find, m_contentDoc.blocks)) return cb;
    }
    return nullptr;
}

// --- B1: Cursor mode ---

void DocumentView::setCursorMode(CursorMode mode)
{
    m_cursorMode = mode;
    clearSelection();
    if (mode == HandTool) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        viewport()->setCursor(Qt::OpenHandCursor);
    } else {
        setDragMode(QGraphicsView::NoDrag);
        viewport()->setCursor(Qt::IBeamCursor);
    }
}

// --- B2: Text selection ---

void DocumentView::clearSelection()
{
    if (m_webViewItem) m_webViewItem->setSelectionRects({});
    for (int pageNum : m_pagesWithSelection) {
        for (auto *item : m_pdfPageItems) {
            if (item->pageNumber() == pageNum)
                item->clearSelection();
        }
    }
    m_pagesWithSelection.clear();
    m_textSelecting = false;
}

void DocumentView::updateTextSelection()
{
    if (m_webViewItem) {
        QRectF selection = QRectF(m_selectPressPos, m_selectCurrentPos).normalized()
                               .translated(-m_webViewItem->pos());
        QList<QRectF> rects;
        for (const auto &entry : m_sourceMap)
            if (entry.rect.intersects(selection)) rects.append(entry.rect);
        m_webViewItem->setSelectionRects(rects);
        m_pagesWithSelection.clear();
        if (!rects.isEmpty()) m_pagesWithSelection.insert(0);
        return;
    }
    if (!m_pdfMode || !m_popplerDoc)
        return;

    // Build selection rect from press to current pos (scene coords)
    QRectF selRect = QRectF(m_selectPressPos, m_selectCurrentPos).normalized();

    // Clear previous highlights
    for (int pageNum : m_pagesWithSelection) {
        for (auto *item : m_pdfPageItems) {
            if (item->pageNumber() == pageNum)
                item->clearSelection();
        }
    }
    m_pagesWithSelection.clear();

    // Prefer source map rects for continuous, clean band-style highlighting
    // (since we generated the PDF ourselves and know the exact layout).
    // Fall back to Poppler text boxes only when source map is unavailable.
    bool useSourceMap = !m_sourceMap.isEmpty();

    for (auto *pageItem : m_pdfPageItems) {
        QRectF itemRect = pageItem->boundingRect().translated(pageItem->pos());
        if (!selRect.intersects(itemRect))
            continue;

        int pageNum = pageItem->pageNumber();
        QSizeF pageSz = pageItem->pageSize();

        // Map selection rect to page-local coords
        QRectF localSel = selRect.translated(-pageItem->pos());
        localSel = localSel.intersected(QRectF(0, 0, pageSz.width(), pageSz.height()));
        if (localSel.isEmpty())
            continue;

        QList<QRectF> selRects;

        if (useSourceMap) {
            // Source map approach: use full block rects for continuous highlighting.
            // Once the drag rect touches a block, highlight the entire block —
            // no vertical clipping to the cursor position.
            for (const auto &entry : m_sourceMap) {
                if (entry.pageNumber != pageNum)
                    continue;
                if (!entry.rect.intersects(localSel))
                    continue;

                selRects.append(entry.rect);
            }
        } else {
            // Poppler text box fallback
            QRectF normSel(localSel.x() / pageSz.width(),
                           localSel.y() / pageSz.height(),
                           localSel.width() / pageSz.width(),
                           localSel.height() / pageSz.height());

            std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(pageNum));
            if (!pp)
                continue;

            auto textBoxes = pp->textList();
            for (const auto &tb : textBoxes) {
                QRectF tbRect = tb->boundingBox();
                QRectF normTb(tbRect.x() / pageSz.width(),
                              tbRect.y() / pageSz.height(),
                              tbRect.width() / pageSz.width(),
                              tbRect.height() / pageSz.height());

                if (normSel.intersects(normTb))
                    selRects.append(tbRect);
            }
        }

        if (!selRects.isEmpty()) {
            pageItem->setSelectionRects(selRects);
            m_pagesWithSelection.insert(pageNum);
        }
    }
}


// The lines and runs the selection covers, clipped per run. The source map is
// line-based, so a styled copy taken from the blocks would include a whole
// paragraph when the selection covers a word of it.
// The selection as a one-paragraph document of the runs it covers, for a styled
// copy that stops at the selection rather than at the block boundary.
QList<Content::BlockNode> DocumentView::selectedRunsDocument() const
{
    const QList<Layout::GlyphBox> boxes = selectedGlyphBoxes();
    if (boxes.isEmpty())
        return {};

    Content::Paragraph paragraph;
    Content::TextStyle previous;
    bool havePrevious = false;
    for (const auto &box : boxes) {
        if (box.text.isEmpty())
            continue;
        Content::TextRun run;
        run.text = box.text;
        run.style = box.style;
        // A word break between two runs that share a style still needs its
        // space: the boxes carry their own trailing text, so only insert one
        // when the text does not already provide it.
        if (havePrevious && !previous.fontFamily.isEmpty() && run.style.fontFamily != previous.fontFamily
            && !paragraph.inlines.isEmpty()) {
            Content::TextRun space;
            space.text = QStringLiteral(" ");
            space.style = previous;
            paragraph.inlines.append(space);
        }
        paragraph.inlines.append(run);
        previous = run.style;
        havePrevious = true;
    }
    if (paragraph.inlines.isEmpty())
        return {};
    return {paragraph};
}

QList<Layout::GlyphBox> DocumentView::selectedGlyphBoxes() const
{
    QList<Layout::GlyphBox> selected;
    if (m_pagesWithSelection.isEmpty())
        return selected;
    const QRectF sel = QRectF(m_selectPressPos, m_selectCurrentPos).normalized();

    auto takeLine = [&](const Layout::LineBox &line, const Layout::BlockBox &box,
                        qreal originX, qreal originY) {
        qreal lineY = originY;
        Q_UNUSED(lineY);
        qreal minX = std::numeric_limits<qreal>::max();
        qreal maxX = 0;
        for (const auto &gbox : line.glyphs) {
            minX = qMin(minX, originX + box.x + line.x);
            maxX = qMax(maxX, originX + box.x + line.x + gbox.width);
        }
        if (minX > maxX)
            return;
        const QRectF lineRect(minX, originY + box.y, maxX - minX, line.height);
        if (!lineRect.intersects(sel))
            return;
        qreal x = originX + box.x + line.x;
        for (const auto &gbox : line.glyphs) {
            const QRectF boxRect(x, originY + box.y, gbox.width, line.height);
            if (gbox.width > 0 && boxRect.intersects(sel))
                selected.append(gbox);
            x += gbox.width;
        }
    };

    auto takeBlocks = [&](const QList<Layout::PageElement> &elements, qreal originX, qreal originY) {
        for (const auto &element : elements) {
            if (const auto *box = std::get_if<Layout::BlockBox>(&element)) {
                qreal y = originY + box->y;
                for (const auto &line : box->lines) {
                    takeLine(line, *box, originX, y);
                    y += line.height;
                }
            } else if (const auto *table = std::get_if<Layout::TableBox>(&element)) {
                for (const auto &row : table->rows) {
                    for (const auto &cell : row.cells) {
                        qreal y = originY + table->y + cell.y;
                        for (const auto &line : cell.lines) {
                            Layout::BlockBox synthetic;
                            synthetic.x = table->x + cell.x;
                            takeLine(line, synthetic, originX, y);
                            y += line.height;
                        }
                    }
                }
            }
        }
    };

    if (m_webViewItem) {
        takeBlocks(m_webViewItem->layoutResult().elements,
                   m_webViewItem->pos().x(), m_webViewItem->pos().y());
    } else {
        for (auto *item : m_boxPageItems) {
            if (!item->page())
                continue;
            takeBlocks(item->page()->elements,
                       item->pos().x() + item->contentOrigin().x(),
                       item->pos().y() + item->contentOrigin().y());
        }
    }
    return selected;
}

QString DocumentView::extractSelectedText() const
{
    if (m_pagesWithSelection.isEmpty())
        return {};

    // For word selection (double-click) or missing source data, use Poppler fallback
    if (m_wordSelection || m_sourceMap.isEmpty() || m_processedMarkdown.isEmpty())
        return extractSelectedTextFromPdf();

    const auto [minLine, maxLine] = selectedSourceLines();

    if (minLine > maxLine)
        return {};

    // Extract lines from processed markdown
    const QStringList lines = m_processedMarkdown.split(QLatin1Char('\n'));
    QStringList selected;
    for (int i = minLine - 1; i < maxLine && i < lines.size(); ++i) {
        selected.append(lines[i]);
    }
    return selected.join(QLatin1Char('\n'));
}

QString DocumentView::extractSelectedTextFromPdf() const
{
    if (!m_pdfMode || !m_popplerDoc || m_pagesWithSelection.isEmpty())
        return {};

    QList<int> pages(m_pagesWithSelection.begin(), m_pagesWithSelection.end());
    std::sort(pages.begin(), pages.end());

    QRectF selRect = QRectF(m_selectPressPos, m_selectCurrentPos).normalized();

    QString result;
    for (int pageNum : pages) {
        PdfPageItem *pageItem = nullptr;
        for (auto *item : m_pdfPageItems) {
            if (item->pageNumber() == pageNum) {
                pageItem = item;
                break;
            }
        }
        if (!pageItem)
            continue;

        QSizeF pageSz = pageItem->pageSize();
        QRectF localSel = selRect.translated(-pageItem->pos());
        localSel = localSel.intersected(QRectF(0, 0, pageSz.width(), pageSz.height()));

        QRectF normSel(localSel.x() / pageSz.width(),
                       localSel.y() / pageSz.height(),
                       localSel.width() / pageSz.width(),
                       localSel.height() / pageSz.height());

        std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(pageNum));
        if (!pp)
            continue;

        auto textBoxes = pp->textList();

        struct BoxInfo {
            QRectF rect;
            QString text;
            bool hasSpaceAfter;
        };
        QList<BoxInfo> matches;

        for (const auto &tb : textBoxes) {
            QRectF tbRect = tb->boundingBox();
            QRectF normTb(tbRect.x() / pageSz.width(),
                          tbRect.y() / pageSz.height(),
                          tbRect.width() / pageSz.width(),
                          tbRect.height() / pageSz.height());

            if (normSel.intersects(normTb)) {
                matches.append({tbRect, tb->text(), tb->hasSpaceAfter()});
            }
        }

        std::sort(matches.begin(), matches.end(),
                  [](const BoxInfo &a, const BoxInfo &b) {
            qreal avgH = (a.rect.height() + b.rect.height()) / 2.0;
            if (qAbs(a.rect.y() - b.rect.y()) > avgH * 0.5)
                return a.rect.y() < b.rect.y();
            return a.rect.x() < b.rect.x();
        });

        qreal prevY = -1;
        for (const auto &box : matches) {
            if (prevY >= 0) {
                qreal dy = qAbs(box.rect.y() - prevY);
                if (dy > box.rect.height() * 0.5)
                    result += QLatin1Char('\n');
                else if (box.hasSpaceAfter || !result.isEmpty())
                    result += QLatin1Char(' ');
            }
            result += box.text;
            prevY = box.rect.y();
        }

        if (!result.isEmpty() && pageNum != pages.last())
            result += QLatin1Char('\n');
    }

    return result;
}

QList<Content::BlockNode> DocumentView::extractSelectedBlocks(int minLine, int maxLine) const
{
    QList<Content::BlockNode> result;
    for (const auto &block : m_contentDoc.blocks) {
        Content::SourceRange sr;
        std::visit([&sr](const auto &b) {
            using T = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<T, Content::Paragraph>) {
                sr = b.source;
            } else if constexpr (std::is_same_v<T, Content::Heading>) {
                sr = b.source;
            } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
                sr = b.source;
            } else if constexpr (std::is_same_v<T, Content::List>) {
                sr = b.source;
            } else if constexpr (std::is_same_v<T, Content::Table>) {
                sr = b.source;
            } else if constexpr (std::is_same_v<T, Content::HorizontalRule>) {
                sr = b.source;
            } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
                sr = b.source;
            }
            // FootnoteSection has no top-level source range.
        }, block);

        if (sr.startLine < 0 || sr.endLine < 0)
            continue;

        // Check overlap: block range [sr.startLine, sr.endLine] vs [minLine, maxLine]
        if (sr.startLine <= maxLine && sr.endLine >= minLine)
            result.append(block);
    }
    return result;
}

void DocumentView::copySelection()
{
    QString text = extractSelectedText();
    if (text.isEmpty())
        return;

    auto *mimeData = new QMimeData;
    mimeData->setText(selectedPlainText());

    bool hasSourceData = !m_wordSelection && !m_sourceMap.isEmpty()
                         && !m_processedMarkdown.isEmpty();

    // If we extracted markdown source, also provide it as text/markdown
    if (hasSourceData)
        mimeData->setData(QStringLiteral("text/markdown"), text.toUtf8());

    // Generate styled RTF from content model blocks
    if (hasSourceData && !m_contentDoc.blocks.isEmpty()) {
        const auto [minLine, maxLine] = selectedSourceLines();

        if (minLine <= maxLine) {
            QList<Content::BlockNode> filteredBlocks = extractSelectedBlocks(minLine, maxLine);
            if (!filteredBlocks.isEmpty()) {
                ContentRtfExporter rtfExporter;
                QByteArray rtf = rtfExporter.exportBlocks(filteredBlocks, RtfFilterOptions(),
                                                        m_contentDoc.footnoteStyle);
                mimeData->setData(QStringLiteral("text/rtf"), rtf);
                mimeData->setData(QStringLiteral("application/rtf"), rtf);
            }
        }
    }

    QApplication::clipboard()->setMimeData(mimeData);
}

void DocumentView::copySelectionAsRtf()
{
    bool hasSourceData = !m_wordSelection && !m_sourceMap.isEmpty()
                         && !m_processedMarkdown.isEmpty()
                         && !m_contentDoc.blocks.isEmpty();
    if (!hasSourceData || m_pagesWithSelection.isEmpty())
        return;

    const auto [minLine, maxLine] = selectedSourceLines();

    if (minLine > maxLine)
        return;

    QList<Content::BlockNode> filteredBlocks = selectedRunsDocument();
    if (filteredBlocks.isEmpty())
        filteredBlocks = extractSelectedBlocks(minLine, maxLine); // the PDF path
    if (filteredBlocks.isEmpty())
        return;

    ContentRtfExporter rtfExporter;
    QByteArray rtf = rtfExporter.exportBlocks(filteredBlocks, RtfFilterOptions(),
                                             m_contentDoc.footnoteStyle);

    auto *mimeData = new QMimeData;
    mimeData->setData(QStringLiteral("text/rtf"), rtf);
    mimeData->setData(QStringLiteral("application/rtf"), rtf);
    // Plain text fallback: use Poppler-extracted text (no markdown syntax)
    // so paste targets that prefer text/plain get clean rendered text.
    QString plainText = selectedPlainText();
    if (!plainText.isEmpty())
        mimeData->setText(plainText);
    QApplication::clipboard()->setMimeData(mimeData);
}

void DocumentView::copySelectionAsMarkdown()
{
    if (m_pagesWithSelection.isEmpty())
        return;

    QString text = extractSelectedText();
    if (text.isEmpty())
        return;

    auto *mimeData = new QMimeData;
    mimeData->setText(text);
    mimeData->setData(QStringLiteral("text/markdown"), text.toUtf8());
    QApplication::clipboard()->setMimeData(mimeData);
}

void DocumentView::copySelectionAsComplexRtf()
{
    bool hasSourceData = !m_wordSelection && !m_sourceMap.isEmpty()
                         && !m_processedMarkdown.isEmpty()
                         && !m_contentDoc.blocks.isEmpty();
    if (!hasSourceData || m_pagesWithSelection.isEmpty())
        return;

    Q_EMIT rtfCopyOptionsRequested();
}

void DocumentView::copySelectionWithFilter(const RtfFilterOptions &filter)
{
    bool hasSourceData = !m_wordSelection && !m_sourceMap.isEmpty()
                         && !m_processedMarkdown.isEmpty()
                         && !m_contentDoc.blocks.isEmpty();
    if (!hasSourceData || m_pagesWithSelection.isEmpty())
        return;

    const auto [minLine, maxLine] = selectedSourceLines();

    if (minLine > maxLine)
        return;

    QList<Content::BlockNode> filteredBlocks = selectedRunsDocument();
    if (filteredBlocks.isEmpty())
        filteredBlocks = extractSelectedBlocks(minLine, maxLine); // the PDF path
    if (filteredBlocks.isEmpty())
        return;

    ContentRtfExporter rtfExporter;
    QByteArray rtf = rtfExporter.exportBlocks(filteredBlocks, filter, m_contentDoc.footnoteStyle);

    auto *mimeData = new QMimeData;
    mimeData->setData(QStringLiteral("text/rtf"), rtf);
    mimeData->setData(QStringLiteral("application/rtf"), rtf);
    // Plain text fallback via Poppler
    QString plainText = selectedPlainText();
    if (!plainText.isEmpty())
        mimeData->setText(plainText);
    QApplication::clipboard()->setMimeData(mimeData);
}

// --- Context menu ---

void DocumentView::contextMenuEvent(QContextMenuEvent *event)
{
    // Code block language override: works in any cursor mode
    QPointF scenePos = mapToScene(event->pos());
    const auto *cb = codeBlockAtScenePos(scenePos);

    // Selection copy actions require SelectionTool mode
    bool hasSelection = (m_cursorMode == SelectionTool && !m_pagesWithSelection.isEmpty());

    if (!hasSelection && !cb) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    bool hasSourceData = !m_wordSelection && !m_sourceMap.isEmpty()
                         && !m_processedMarkdown.isEmpty();

    QMenu menu(this);

    // Selection-based copy actions
    if (hasSelection) {
        if (hasSourceData && !m_contentDoc.blocks.isEmpty()) {
            auto *copyRtf = menu.addAction(tr("Copy as Styled Text"));
            connect(copyRtf, &QAction::triggered, this, &DocumentView::copySelectionAsRtf);

            auto *copyComplex = menu.addAction(tr("Copy with Style Options..."));
            connect(copyComplex, &QAction::triggered, this, &DocumentView::copySelectionAsComplexRtf);
        }

        if (hasSourceData) {
            auto *copyMd = menu.addAction(tr("Copy as Markdown"));
            connect(copyMd, &QAction::triggered, this, &DocumentView::copySelectionAsMarkdown);
        }

        // Fallback for word selection or missing source data: plain copy
        if (!hasSourceData || m_contentDoc.blocks.isEmpty()) {
            auto *copyPlain = menu.addAction(tr("Copy"));
            connect(copyPlain, &QAction::triggered, this, &DocumentView::copySelection);
        }
    }

    // Code block language override action
    if (cb) {
        if (!menu.isEmpty())
            menu.addSeparator();

        QString label;
        if (cb->language.isEmpty())
            label = tr("Set Syntax Language...");
        else
            label = tr("Syntax: %1...").arg(cb->language);

        auto *langAction = menu.addAction(label);
        QString codeKey = cb->code.trimmed();
        QString currentLang = cb->language;

        connect(langAction, &QAction::triggered, this, [this, codeKey, currentLang]() {
            Q_EMIT languageOverrideRequested(codeKey, currentLang);
        });
    }

    menu.exec(event->globalPos());
    event->accept();
}

// --- A7: Link hover ---

void DocumentView::ensureLinkCacheForPage(int pageNum)
{
    if (m_linkCache.contains(pageNum))
        return;
    if (!m_popplerDoc)
        return;

    std::unique_ptr<Poppler::Page> pp(m_popplerDoc->page(pageNum));
    if (!pp)
        return;

    QList<PageLinkInfo> links;
    auto popplerLinks = pp->links();  // std::vector<std::unique_ptr<Poppler::Link>>
    for (const auto &link : popplerLinks) {
        if (link->linkType() == Poppler::Link::Browse) {
            auto *browseLink = static_cast<const Poppler::LinkBrowse *>(link.get());
            PageLinkInfo info;
            // Poppler::Link::linkArea() returns a normalized QRectF (0-1)
            QRectF normArea = link->linkArea();
            QSizeF pageSz = pp->pageSizeF();
            info.rect = QRectF(normArea.x() * pageSz.width(),
                               normArea.y() * pageSz.height(),
                               normArea.width() * pageSz.width(),
                               normArea.height() * pageSz.height());
            info.url = browseLink->url();
            links.append(info);
        } else if (link->linkType() == Poppler::Link::Goto) {
            auto *gotoLink = static_cast<const Poppler::LinkGoto *>(link.get());
            PageLinkInfo info;
            QRectF normArea = link->linkArea();
            QSizeF pageSz = pp->pageSizeF();
            info.rect = QRectF(normArea.x() * pageSz.width(),
                               normArea.y() * pageSz.height(),
                               normArea.width() * pageSz.width(),
                               normArea.height() * pageSz.height());
            if (gotoLink->isExternal()) {
                info.url = gotoLink->fileName();
            } else {
                int destPage = gotoLink->destination().pageNumber();
                info.url = QStringLiteral("Page %1").arg(destPage);
            }
            links.append(info);
        }
    }
    m_linkCache.insert(pageNum, links);
}


bool DocumentView::scrollToHrefRect(const QString &href)
{
    if (m_webViewItem) {
        for (const auto &link : m_webViewItem->linkHitRects()) {
            if (link.href != href)
                continue;
            m_applyingScroll = true;
            centerOn(m_webViewItem->pos() + link.rect.center());
            m_applyingScroll = false;
            updateCurrentPage();
            return true;
        }
        return false;
    }
    for (auto *item : m_boxPageItems) {
        const HitIndex &index = item->hitIndex();
        for (int i = 0; i < index.linkHrefs.size(); ++i) {
            if (index.linkHrefs.at(i) != href)
                continue;
            const QPointF center = item->pos() + item->contentOrigin()
                + index.linkBounds.at(i).center();
            m_applyingScroll = true;
            centerOn(center);
            m_applyingScroll = false;
            updateCurrentPage();
            return true;
        }
    }
    return false;
}

QString DocumentView::noteText(int index) const
{
    for (const auto &block : m_contentDoc.blocks) {
        const auto *section = std::get_if<Content::FootnoteSection>(&block);
        if (!section || index < 0 || index >= section->footnotes.size())
            continue;
        Content::Document document;
        document.blocks = section->footnotes.at(index).blocks;
        return Content::toPlainText(document).simplified();
    }
    return {};
}

bool DocumentView::activateInternalLink(const QString &href)
{
    int noteIndex = -1;
    if (!Content::isFootnoteInternalUrl(href, &noteIndex))
        return false;

    if (href.startsWith(QStringLiteral("penelope-footnote-ref:"))) {
        // A note's marker: go back to the first page that cites it.
        for (const auto &block : m_contentDoc.blocks) {
            const auto *section = std::get_if<Content::FootnoteSection>(&block);
            if (!section || noteIndex < 0 || noteIndex >= section->footnotes.size())
                continue;
            const auto &references = section->footnotes.at(noteIndex).references;
            if (references.isEmpty())
                return false;
            const int line = references.first().sourceLine;
            for (const auto &entry : m_sourceMap) {
                if (line < entry.startLine || line > entry.endLine)
                    continue;
                scrollToPosition(m_renderMode == WebMode ? 0 : entry.pageNumber, entry.rect.top());
                return true;
            }
            // Without a source map the citation's own rectangle is the anchor.
            return scrollToHrefRect(Content::footnoteRefUrl(noteIndex));
        }
        return false;
    }

    // A citation: go to its note, wherever the layout put it.
    if (scrollToHrefRect(Content::footnoteBacklinkUrl(noteIndex)))
        return true;
    // The notes are an endnote section in the continuous reader: they are at the
    // end of the column.
    if (m_webViewItem && verticalScrollBar()) {
        m_applyingScroll = true;
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        m_applyingScroll = false;
        updateCurrentPage();
        return true;
    }
    return false;
}

QString DocumentView::linkHint(const QString &href) const
{
    int noteIndex = -1;
    if (!Content::isFootnoteInternalUrl(href, &noteIndex))
        return href;
    if (href.startsWith(QStringLiteral("penelope-footnote:")))
        return noteText(noteIndex);
    return tr("Go to the reference", "footnote back-link hover hint");
}

void DocumentView::checkLinkHover(const QPointF &scenePos)
{
    // Web mode: use WebViewItem link hit-testing
    if (m_renderMode == WebMode && m_webViewItem) {
        QPointF itemPos = m_webViewItem->mapFromScene(scenePos);
        QString href = m_webViewItem->linkAt(itemPos);
        if (!href.isEmpty()) {
            if (m_currentHoverLink != href) {
                m_currentHoverLink = href;
                viewport()->setCursor(Qt::PointingHandCursor);
                const QString title = m_webViewItem->linkTitleAt(itemPos);
                Q_EMIT statusHintChanged(title.isEmpty() ? linkHint(href) : title);
            }
        } else {
            const QString title = m_webViewItem->imageTitleAt(itemPos);
            if (!title.isEmpty()) {
                if (m_currentHoverLink != title) {
                    m_currentHoverLink = title;
                    viewport()->setCursor(Qt::ArrowCursor);
                    Q_EMIT statusHintChanged(title);
                }
                return;
            }
            if (!m_currentHoverLink.isEmpty()) {
                m_currentHoverLink.clear();
                viewport()->setCursor(Qt::ArrowCursor);
                Q_EMIT statusHintChanged(QString());
            }
        }
        return;
    }

    if (!m_boxPageItems.isEmpty()) {
        for (auto *item : m_boxPageItems) {
            const QRectF itemRect = QRectF(0, 0, item->pageSize().width(), item->pageSize().height())
                                        .translated(item->pos());
            if (!itemRect.contains(scenePos))
                continue;
            const QPointF itemPos = item->mapFromScene(scenePos);
            const QString href = item->linkAt(itemPos);
            if (!href.isEmpty()) {
                if (m_currentHoverLink != href) {
                    m_currentHoverLink = href;
                    viewport()->setCursor(Qt::PointingHandCursor);
                    const QString title = item->linkTitleAt(itemPos);
                    Q_EMIT statusHintChanged(title.isEmpty() ? linkHint(href) : title);
                }
            } else if (!item->imageTitleAt(item->mapFromScene(scenePos)).isEmpty()) {
                const QString title = item->imageTitleAt(item->mapFromScene(scenePos));
                if (m_currentHoverLink != title) {
                    m_currentHoverLink = title;
                    viewport()->setCursor(Qt::ArrowCursor);
                    Q_EMIT statusHintChanged(title);
                }
            } else if (!m_currentHoverLink.isEmpty()) {
                m_currentHoverLink.clear();
                viewport()->setCursor(Qt::ArrowCursor);
                Q_EMIT statusHintChanged(QString());
            }
            return;
        }
    }

    if (!m_pdfMode || !m_popplerDoc)
        return;

    // Find page item under cursor
    for (auto *pageItem : m_pdfPageItems) {
        QRectF itemRect = QRectF(0, 0, pageItem->pageSize().width(),
                                  pageItem->pageSize().height())
                              .translated(pageItem->pos());
        if (!itemRect.contains(scenePos))
            continue;

        int pageNum = pageItem->pageNumber();
        ensureLinkCacheForPage(pageNum);

        QPointF localPos = scenePos - pageItem->pos();
        const auto &links = m_linkCache[pageNum];
        for (const auto &linkInfo : links) {
            if (linkInfo.rect.contains(localPos)) {
                if (m_currentHoverLink != linkInfo.url) {
                    m_currentHoverLink = linkInfo.url;
                    Q_EMIT statusHintChanged(linkInfo.url);
                }
                return;
            }
        }
        break; // cursor is on a page but not on a link
    }

    // Not hovering over any link
    if (!m_currentHoverLink.isEmpty()) {
        m_currentHoverLink.clear();
        Q_EMIT statusHintChanged(QString());
    }
}

void DocumentView::updateCurrentPage()
{
    QPointF center = mapToScene(viewport()->rect().center());

    // Check pages under the viewport center
    auto pageAt = [&](auto *item) {
        const QRectF r = item->boundingRect().translated(item->pos());
        if (!r.contains(center))
            return;
        const int page = item->pageNumber();
        if (m_currentPage != page)
            m_currentPage = page;
    };
    for (auto *item : m_boxPageItems)
        pageAt(item);
    for (auto *item : m_pdfPageItems)
        pageAt(item);

    // --- Heading scroll-sync ---
    if (m_headingPositions.isEmpty())
        return;

    const QPointF viewTop = mapToScene(viewport()->rect().topLeft());
    qreal limit = viewTop.y() + 32;
    if (auto *bar = verticalScrollBar()) {
        // A short last section cannot be scrolled to the top. At the end of
        // the document the heading in view is the one the reader has reached.
        if (bar->maximum() > 0 && bar->value() >= bar->maximum() - 2)
            limit = mapToScene(viewport()->rect().bottomLeft()).y();
    }

    int bestHeading = -1;
    for (int i = m_headingPositions.size() - 1; i >= 0; --i) {
        const auto &hp = m_headingPositions[i];
        qreal sceneY = 0;
        if (!headingSceneY(hp, &sceneY))
            continue;
        if (sceneY <= limit) {
            bestHeading = i;
            break;
        }
    }

    // If no heading is above viewport top, use the first heading
    if (bestHeading == -1 && !m_headingPositions.isEmpty())
        bestHeading = 0;

    int sourceLine = (bestHeading >= 0 && bestHeading < m_headingPositions.size())
                         ? m_headingPositions[bestHeading].sourceLine
                         : -1;
    if (sourceLine != m_currentHeadingLine) {
        m_currentHeadingLine = sourceLine;
        Q_EMIT currentHeadingChanged(sourceLine);
    }
}

QPair<int, int> DocumentView::selectedSourceLines() const
{
    int first = INT_MAX, last = -1;
    QRectF selected = QRectF(m_selectPressPos, m_selectCurrentPos).normalized();
    auto collect = [&](int page, const QRectF &local) {
        for (const auto &entry : m_sourceMap) {
            if (entry.pageNumber == page && entry.startLine > 0 && entry.rect.intersects(local)) {
                first = qMin(first, entry.startLine);
                last = qMax(last, entry.endLine);
            }
        }
    };
    if (m_webViewItem) collect(0, selected.translated(-m_webViewItem->pos()));
    else if (!m_boxPageItems.isEmpty()) {
        for (auto *page : m_boxPageItems)
            collect(page->pageNumber(), selected.translated(-page->pos()).intersected(
                QRectF(QPointF(), page->pageSize())));
    } else for (auto *page : m_pdfPageItems)
        collect(page->pageNumber(), selected.translated(-page->pos()).intersected(
            QRectF(QPointF(), page->pageSize())));
    return {first, last};
}

QString DocumentView::selectedPlainText() const
{
    if (m_wordSelection || m_processedMarkdown.isEmpty()) return extractSelectedTextFromPdf();
    ContentBuilder builder;
    builder.setRenderImages(false);
    QString text = Content::toPlainText(builder.build(extractSelectedText()));
    return text;
}
