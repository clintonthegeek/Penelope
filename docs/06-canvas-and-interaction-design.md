# Penelope: Canvas & Interaction Design (Planning Stage 4)

## Architecture: QGraphicsView-Based Paginated Canvas

```
DocumentView (QGraphicsView subclass)
    |
    v
QGraphicsScene
    |
    +-- PageItem[0] (custom QGraphicsItem)
    |       +-- Renders page 0 of QTextDocument via QPainter
    |       +-- Drop shadow, page border, margin guides
    |       +-- ImageItem children (interactive image overlays)
    |
    +-- PageItem[1]
    |       +-- ...
    |
    +-- PageItem[N]
    |       +-- ...
    |
    +-- (Header/footer overlays per page)
```

Pages are laid out vertically with a gap between them. The scene rect covers
all pages plus surrounding margin for the gray background area.

---

## PageItem Class

```cpp
class PageItem : public QGraphicsItem {
public:
    PageItem(int pageNumber, QSizeF pageSize, QTextDocument *document,
             QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    int m_pageNumber;
    QSizeF m_pageSize;
    QTextDocument *m_document;
};
```

### Rendering: QTextDocument Page Clipping

Each PageItem renders one page of the QTextDocument by translating the painter
and clipping to the page's vertical slice:

```cpp
void PageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *) {
    // Use exposedRect for performance (only paint visible portion)
    QRectF exposed = option->exposedRect;
    painter->setClipRect(exposed);

    // Drop shadow (offset by 4px)
    painter->fillRect(boundingRect().translated(4, 4), QColor(0, 0, 0, 50));

    // White page background
    painter->fillRect(boundingRect(), Qt::white);

    // Page border
    painter->setPen(QPen(Qt::lightGray, 0.5));
    painter->drawRect(boundingRect());

    // Render document content for this page
    qreal pageHeight = m_pageSize.height();
    QRectF contentArea(margin.left, margin.top,
                       m_pageSize.width() - margin.left - margin.right,
                       m_pageSize.height() - margin.top - margin.bottom);

    painter->save();
    painter->translate(margin.left, margin.top);
    painter->translate(0, -m_pageNumber * pageHeight);

    QRectF clipRect(0, m_pageNumber * pageHeight,
                    contentArea.width(), contentArea.height());
    m_document->drawContents(painter, clipRect);
    painter->restore();

    // Render header/footer for this page
    renderHeader(painter);
    renderFooter(painter);
}
```

### Page Layout

```cpp
void DocumentView::layoutPages() {
    const qreal pageGap = 12.0;  // pixels between pages in scene coords
    qreal yOffset = pageGap;

    for (int i = 0; i < m_pageCount; ++i) {
        if (i >= m_pageItems.size()) {
            auto *page = new PageItem(i, m_pageSize, m_document);
            page->setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
            m_scene->addItem(page);
            m_pageItems.append(page);
        }
        // Center pages horizontally
        qreal xOffset = (m_sceneWidth - m_pageSize.width()) / 2.0;
        m_pageItems[i]->setPos(xOffset, yOffset);
        yOffset += m_pageSize.height() + pageGap;
    }

    // Remove excess page items if page count decreased
    while (m_pageItems.size() > m_pageCount) {
        delete m_pageItems.takeLast();
    }

    m_scene->setSceneRect(0, 0, m_sceneWidth,
                          yOffset + pageGap);
}
```

### Page Count Detection

After setting page size on the QTextDocument:
```cpp
m_document->setPageSize(m_pageSize);
m_pageCount = m_document->pageCount();
```

When using Calligra's KoTextDocumentLayout (which replaces
QTextDocument::documentLayout()), the page count comes from the number of
root areas the provider has created.

---

## DocumentView (QGraphicsView Subclass)

```cpp
class DocumentView : public QGraphicsView {
    Q_OBJECT

public:
    explicit DocumentView(QWidget *parent = nullptr);

    void setDocument(QTextDocument *doc);

    // Zoom
    void setZoomPercent(int percent);
    void fitWidth();
    void fitPage();
    int zoomPercent() const;

    // Navigation
    void goToPage(int page);
    int currentPage() const;
    int pageCount() const;

    // View mode
    void setContinuousMode(bool continuous);
    bool isContinuous() const;

signals:
    void zoomChanged(int percent);
    void currentPageChanged(int page);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    QGraphicsScene *m_scene;
    QTextDocument *m_document;
    QList<PageItem*> m_pageItems;
    QSizeF m_pageSize;
    int m_currentZoom = 100;
    bool m_continuousMode = true;
};
```

---

## Zoom Implementation

### Percentage Zoom

```cpp
void DocumentView::setZoomPercent(int percent) {
    percent = qBound(25, percent, 400);
    qreal factor = percent / 100.0;
    resetTransform();
    scale(factor, factor);
    m_currentZoom = percent;
    emit zoomChanged(percent);
}
```

### Fit Width

```cpp
void DocumentView::fitWidth() {
    if (m_pageItems.isEmpty()) return;
    QRectF pageRect = m_pageItems.first()->boundingRect();
    qreal viewWidth = viewport()->width();
    qreal factor = viewWidth / (pageRect.width() + 40);  // 20px margin each side
    resetTransform();
    scale(factor, factor);
    m_currentZoom = qRound(factor * 100);
    emit zoomChanged(m_currentZoom);
}
```

### Fit Page

```cpp
void DocumentView::fitPage() {
    if (m_pageItems.isEmpty()) return;
    QRectF pageRect = m_pageItems.first()->boundingRect();
    pageRect.adjust(-20, -20, 20, 20);
    fitInView(pageRect, Qt::KeepAspectRatio);
    m_currentZoom = qRound(transform().m11() * 100);
    emit zoomChanged(m_currentZoom);
}
```

### Mouse Wheel Zoom (Ctrl+Scroll)

```cpp
void DocumentView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y() > 0 ? 10 : -10;
        setZoomPercent(m_currentZoom + delta);
        // Zoom toward mouse position
        centerOn(mapToScene(event->position().toPoint()));
    } else {
        QGraphicsView::wheelEvent(event);
    }
}
```

---

## View Modes

### Continuous Scroll

All pages visible, user scrolls through them. Default mode.

```cpp
void DocumentView::setContinuousMode(bool continuous) {
    m_continuousMode = continuous;
    if (continuous) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Scene rect covers all pages
    } else {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        goToPage(currentPage());
    }
}
```

### Single Page

View snaps to show one page at a time. Navigation via Page Up/Down, arrow
keys, or toolbar buttons.

```cpp
void DocumentView::goToPage(int page) {
    if (page < 0 || page >= m_pageItems.size()) return;
    m_currentPage = page;

    if (!m_continuousMode) {
        QRectF pageRect = m_pageItems[page]->boundingRect()
            .translated(m_pageItems[page]->pos());
        pageRect.adjust(-20, -20, 20, 20);
        fitInView(pageRect, Qt::KeepAspectRatio);
    } else {
        ensureVisible(m_pageItems[page]);
    }
    emit currentPageChanged(page);
}
```

### Current Page Tracking (Continuous Mode)

When scrolling in continuous mode, detect which page is most visible:

```cpp
void DocumentView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);

    // Determine current page from viewport center
    QPointF center = mapToScene(viewport()->rect().center());
    for (int i = 0; i < m_pageItems.size(); ++i) {
        QRectF pageRect = m_pageItems[i]->boundingRect()
            .translated(m_pageItems[i]->pos());
        if (pageRect.contains(center)) {
            if (m_currentPage != i) {
                m_currentPage = i;
                emit currentPageChanged(i);
            }
            break;
        }
    }
}
```

---

## Image Interaction

### ImageItem (QGraphicsPixmapItem Subclass)

```cpp
class ImageItem : public QGraphicsPixmapItem {
public:
    ImageItem(const QPixmap &pixmap, const QString &imagePath,
              QGraphicsItem *parent = nullptr);

    enum WrapMode { Inline, Square, Tight, TopBottom };
    enum Alignment { Left, Center, Right };

    void setWrapMode(WrapMode mode);
    void setAlignment(Alignment align);

    // Called when resize handles are dragged
    void handleMoved(HandlePosition which, QPointF newPos);

signals:  // via a helper QObject
    void sizeChanged(const QString &imagePath, QSizeF newSize);
    void alignmentChanged(const QString &imagePath, Alignment align);

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant &value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    void updateHandlePositions();
    void showHandles(bool visible);

    QList<ResizeHandle*> m_handles;  // 8 child items
    QString m_imagePath;
    QSizeF m_originalSize;
    WrapMode m_wrapMode = Inline;
    Alignment m_alignment = Center;
};
```

### ResizeHandle (Child QGraphicsItem)

Following the child-item pattern recommended by the research:

```cpp
class ResizeHandle : public QGraphicsRectItem {
public:
    ResizeHandle(HandlePosition pos, QGraphicsItem *parent);

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant &value) override;

private:
    HandlePosition m_pos;
};
```

- 8 handles: TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left
- `setFlag(ItemIgnoresTransformations)` to keep handles constant visual size at
  any zoom level
- `setFlag(ItemIsMovable)` for drag interaction
- On drag, the handle notifies the parent ImageItem to resize
- Shift+drag constrains aspect ratio
- Handles only visible when the image is hovered or selected

### Image-Text Wrapping Integration

When an image's size or position changes:
1. ImageItem emits sizeChanged/alignmentChanged
2. DocumentController updates the QTextImageFormat in the document
3. The layout engine re-runs (KoTextDocumentLayout)
4. The obstruction system recalculates text wrapping around the image
5. Canvas repaints affected pages

For the initial reader-focused milestone, images are rendered inline (within
text flow) with optional alignment. Full floating/wrapping comes later when the
layout engine's obstruction system is integrated.

---

## Performance Strategies

### 1. ItemUsesExtendedStyleOption

Every PageItem sets this flag so `option->exposedRect` contains only the visible
portion. The paint method clips to this rect, avoiding rendering off-screen
content.

### 2. Lazy Page Loading

For large documents (100+ pages), only create PageItem objects for pages near
the viewport. Distant pages are represented by lightweight placeholder items
(gray rectangles).

```cpp
void DocumentView::updateVisiblePages() {
    QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    QRectF preload = visible.adjusted(0, -1000, 0, 1000);  // preload 1000px

    for (int i = 0; i < m_pageCount; ++i) {
        bool shouldBeLoaded = pageRect(i).intersects(preload);
        m_pageItems[i]->setLoaded(shouldBeLoaded);
    }
}
```

### 3. Explicit Scene Rect

Set `scene->setSceneRect()` explicitly after layout rather than letting Qt
auto-calculate. This avoids costly itemsBoundingRect() recalculation.

### 4. Device Coordinate Caching

For pages that haven't changed, enable `setCacheMode(DeviceCoordinateCache)`.
Invalidate the cache (via `update()`) when the document content or style
changes.

### 5. Background Rendering

For very large documents, render pages to QPixmap in a background thread.
The PageItem displays the cached pixmap instead of rendering live. When
content changes, the cache is invalidated and re-rendered asynchronously.

---

## Scene Background

The area outside pages is painted with a neutral gray:

```cpp
DocumentView::DocumentView(QWidget *parent) : QGraphicsView(parent) {
    setBackgroundBrush(QBrush(QColor(0x3c, 0x3c, 0x3c)));
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
}
```
