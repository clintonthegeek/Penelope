#ifndef PENELOPE_DOCUMENTVIEW_H
#define PENELOPE_DOCUMENTVIEW_H

#include <QGraphicsView>
#include <memory>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "layoutengine.h"
#include "pagelayout.h"
#include "rtffilteroptions.h"

class BoxPageItem;
class FontManager;
class PdfPageItem;
class RenderCache;
class WebViewItem;

namespace Poppler { class Document; }

// A7: Cached link data per page
struct PageLinkInfo {
    QRectF rect;      // in page-local points
    QString url;
};

struct ViewState
{
    int zoomPercent = 100;
    int currentPage = 0;
    qreal scrollFraction = 0.0;
    bool valid = false;
};

struct HeadingPosition {
    int page = 0;
    qreal yOffset = 0; // page-local, points from top of content area
    int sourceLine = 0; // stable identifier from Content::SourceRange::startLine
};

// Content anchor for restoring the reading position after a rebuild. The
// source line survives re-layout; the fraction keeps the offset inside a block
// whose rendered height may change when text above it is edited. The context
// is the block's leading source text, used to find the block again when lines
// were inserted or removed above the viewport.
struct ScrollAnchor
{
    int sourceLine = -1; // 1-based line in the processed markdown
    qreal fraction = 0.0; // 0..1 inside the mapped source-map block
    QString context; // trimmed leading source line of that block
    bool isValid() const { return sourceLine > 0; }
};

class DocumentView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit DocumentView(QWidget *parent = nullptr);
    ~DocumentView() override;

    // Paginated PDF reader
    void setPdfData(const QByteArray &pdf);

    void setPageLayout(const PageLayout &layout);

    ViewState saveViewState() const;
    // The anchor takes precedence over the scroll fraction once the anchored
    // block exists in the installed layout.
    void restoreViewState(const ViewState &state, const ScrollAnchor &anchor = {});
    // Top-of-viewport block as a content anchor, valid while a source map exists.
    ScrollAnchor viewportAnchor() const;
    // Restore an anchor, deferring until layout reaches it. User scrolling and
    // explicit navigation cancel the pending anchor.
    void scrollToAnchor(const ScrollAnchor &anchor);

    // Zoom
    void setZoomPercent(int percent);
    void zoomIn();
    void zoomOut();
    void fitWidth();
    void fitPage();
    int zoomPercent() const { return m_currentZoom; }

    // Navigation
    void goToPage(int page);
    void scrollToPosition(int page, qreal yOffset);
    // Scroll to a heading that may still be below the laid-out column.
    void followHeading(int sourceLine);
    void setHeadingPositions(const QList<HeadingPosition> &positions);
    const QList<HeadingPosition> &headingPositions() const { return m_headingPositions; }
    void previousPage();
    void nextPage();
    int currentPage() const { return m_currentPage; }
    int pageCount() const { return m_pageCount; }

    // B1: Cursor mode
    enum CursorMode { HandTool, SelectionTool };
    Q_ENUM(CursorMode)

    void setCursorMode(CursorMode mode);
    CursorMode cursorMode() const { return m_cursorMode; }

    // B2: Selection
    void copySelection();
    void copySelectionAsRtf();
    void copySelectionAsMarkdown();
    void copySelectionAsComplexRtf();
    void copySelectionWithFilter(const RtfFilterOptions &filter);
    // The selection as styled text taken from the shaped boxes: only the runs
    // the selection actually covers, so a partial paragraph copies partially.
    QList<Layout::GlyphBox> selectedGlyphBoxes() const;
    // That selection as a document, for the styled copy.
    QList<Content::BlockNode> selectedRunsDocument() const;
    // The blocks a copy should carry: the shaped runs the selection covers when
    // the display list is available, whole blocks only as a fallback (the PDF
    // reader, which has no display list to read).
    QList<Content::BlockNode> selectionCopyBlocks() const;
    // The selection's rendered text, cut at character boundaries.
    QString selectedDisplayListText() const;
    void collectSelectedRuns(QList<Layout::GlyphBox> *runs, QStringList *lineText) const;
    void clearSelection();
    // Source breadcrumbs for markdown-faithful copy + styled RTF export
    void setSourceData(const QString &processedMarkdown,
                       const QList<Layout::SourceMapEntry> &sourceMap,
                       const Content::Document &contentDoc,
                       const QList<Layout::CodeBlockRegion> &codeBlockRegions = {});

    // Code block language overrides (per-session, persisted via MetadataStore)
    void setCodeBlockLanguageOverrides(const QHash<QString, QString> &overrides);
    QHash<QString, QString> codeBlockLanguageOverrides() const;
    void applyLanguageOverrides(Content::Document &doc) const;

    // View modes (Okular pattern)
    enum ViewMode { Continuous, SinglePage, FacingPages, FacingPagesFirstAlone, ContinuousFacing, ContinuousFacingFirstAlone };
    Q_ENUM(ViewMode)

    // Render modes: paginated PDF vs continuous web-style
    enum RenderMode { PrintMode, WebMode };
    Q_ENUM(RenderMode)

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }

    bool isContinuous() const { return m_viewMode == Continuous || m_viewMode == ContinuousFacing || m_viewMode == ContinuousFacingFirstAlone; }

    // Web view mode
    void setRenderMode(RenderMode mode);
    RenderMode renderMode() const { return m_renderMode; }
    void setWebContent(Layout::ContinuousLayoutResult &&result);
    void appendWebContent(Layout::ContinuousLayoutResult &&extra);
    void layoutExtended();
    void setBoxPages(const QList<std::shared_ptr<const Layout::Page>> &pages,
                     std::shared_ptr<FontManager> fonts, const PageLayout &layout,
                     bool append, const QString &fileName = {}, const QString &title = {});
    void scheduleMoreLayout();
    void scheduleWebRelayout();
    const Layout::LayoutCursor &layoutCursor() const { return m_layoutCursor; }
    void setLayoutCursor(const Layout::LayoutCursor &cursor) { m_layoutCursor = cursor; }
    void requestScrollToBlock(int blockIndex);
    void integrateCursor(const Layout::LayoutCursor &cursor, int blockIndex = -1, qreal blockY = 0);
    int scrollToBlock() const { return m_scrollToBlock; }
    qreal scrollTargetY() const { return m_scrollTargetY; }
    WebViewItem *webViewItem() const { return m_webViewItem; }
    bool hasBoxPages() const { return !m_boxPageItems.isEmpty(); }
    void setWebFontManager(FontManager *fm) { m_webFontManager = fm; m_webFontOwner.reset(); }
    void setWebFontOwner(std::shared_ptr<FontManager> owner) {
        m_webFontManager = owner.get(); m_webFontOwner = std::move(owner);
    }

    // Internal reading links: a citation points at its note, a note's marker at
    // the page that cites it. Both are collected as link rectangles, so the
    // reader resolves them instead of the desktop.
    bool activateInternalLink(const QString &href);
    // What the status hint should say for a link, internal or not.
    QString linkHint(const QString &href) const;

    bool isPdfMode() const { return m_pdfMode; }
    bool hasPageItems() const { return m_pdfMode || !m_boxPageItems.isEmpty(); }
    bool isPrintMode() const { return m_renderMode == PrintMode && hasPageItems(); }
    QByteArray pdfData() const { return m_pdfData; }
    const QList<Layout::SourceMapEntry> &sourceMap() const { return m_sourceMap; }
    // What assistive technology reads: the semantic document, not the pages.
    const Content::Document &contentDocument() const { return m_contentDoc; }

Q_SIGNALS:
    void zoomChanged(int percent);
    void statusHintChanged(const QString &hint);  // A7: hover hints
    void codeBlockLanguageChanged();
    void currentHeadingChanged(int index);
    void webRelayoutRequested();
    void moreLayoutRequested();
    void languageOverrideRequested(const QString &codeKey, const QString &currentLang);
    void rtfCopyOptionsRequested();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private Q_SLOTS:
    void onPixmapReady(int pageNumber);

private:
    void applyRenderChrome();
    void layoutPages();
    void layoutPagesContinuous();
    void layoutPagesSingle();
    void layoutPagesFacing();
    void layoutPagesContinuousFacing();
    void layoutPagesFacingFirstAlone();
    void layoutPagesContinuousFacingFirstAlone();
    void updateCurrentPage();
    void noteUserScroll();
    void maintainHeadingScroll();
    void applyScrollChase();
    void requestMoreLayout();
    bool headingSceneY(const HeadingPosition &hp, qreal *sceneY) const;
    bool sourceMapEntrySceneRect(const Layout::SourceMapEntry &entry, QRectF *rect) const;
    bool scrollToHrefRect(const QString &href);
    QString noteText(int index) const;
    // Maps an anchor line onto the installed revision's line numbering.
    int resolveAnchorLine(const ScrollAnchor &anchor) const;
    QString anchorContext(const Layout::SourceMapEntry &entry) const;
    // Aligns the pending anchor block at the viewport top. Returns false while
    // the block is not laid out yet.
    bool applyPendingAnchor();
    void clearPdfPages();
    void clearBoxPages();
    void layoutBoxPages();

    // B2: Text selection helpers
    void updateTextSelection();
    QPair<int, int> selectedSourceLines() const;
    QString extractSelectedText() const;
    QString selectedPlainText() const;
    QString extractSelectedTextFromPdf() const; // Poppler fallback
    QList<Content::BlockNode> extractSelectedBlocks(int minLine, int maxLine) const;
    // Code block hit-test
    const Content::CodeBlock *codeBlockAtScenePos(const QPointF &scenePos) const;

    // A7: Link hover helpers
    void checkLinkHover(const QPointF &scenePos);
    void ensureLinkCacheForPage(int pageNum);

    QGraphicsScene *m_scene = nullptr;
    QSizeF m_pageSize;
    PageLayout m_pageLayout;
    int m_pageCount = 0;
    int m_currentPage = 0;
    int m_currentZoom = 100;
    ViewMode m_viewMode = Continuous;
    bool m_skipAutoFit = false;

    // PDF rendering
    bool m_pdfMode = false;
    QByteArray m_pdfData;
    Poppler::Document *m_popplerDoc = nullptr;
    RenderCache *m_renderCache = nullptr;
    QList<PdfPageItem *> m_pdfPageItems;
    QList<BoxPageItem *> m_boxPageItems;
    int m_pendingPage = -1;
    int m_scrollToBlock = -1;
    qreal m_scrollTargetY = 0;
    QHash<int, qreal> m_blockY;

    // Web view rendering
    RenderMode m_renderMode = PrintMode;
    Layout::LayoutCursor m_layoutCursor;
    bool m_moreRequested = false;
    WebViewItem *m_webViewItem = nullptr;
    bool m_applyingWebContent = false;
    QTimer m_relayoutTimer;
    std::shared_ptr<FontManager> m_webFontOwner;
    FontManager *m_webFontManager = nullptr;

    // Middle-mouse smooth zoom (Okular pattern)
    bool m_middleZooming = false;
    QPoint m_middleZoomOrigin;
    QPointF m_middleZoomSceneAnchor;
    int m_middleZoomStartPercent = 100;

    // B1: Cursor mode
    CursorMode m_cursorMode = HandTool;

    // B2: Text selection state (Okular-informed)
    bool m_textSelecting = false;
    QPointF m_selectPressPos;        // scene coords
    QPointF m_selectCurrentPos;      // scene coords
    QSet<int> m_pagesWithSelection;
    static constexpr int kSelectionThreshold = 5;  // 5px before selecting

    // A7: Link hover cache
    QHash<int, QList<PageLinkInfo>> m_linkCache;
    QString m_currentHoverLink;

    // Source breadcrumbs
    QString m_processedMarkdown;
    QList<Layout::SourceMapEntry> m_sourceMap;
    Content::Document m_contentDoc;
    QList<Layout::CodeBlockRegion> m_codeBlockRegions;
    QHash<QString, QString> m_codeBlockLanguageOverrides; // trimmed code -> language
    bool m_wordSelection = false; // set by double-click, cleared by mouse press
    QList<HeadingPosition> m_headingPositions;
    int m_currentHeadingLine = -1; // source line of current heading (stable ID)
    int m_pendingHeadingLine = -1;
    bool m_applyingScroll = false;
    ScrollAnchor m_pendingAnchor;
    bool m_scrollChase = false;
    int m_scrollChasePage = 0;
    qreal m_scrollChaseY = 0;

    static constexpr qreal kPageGap = 12.0;

public:
    static constexpr qreal kSceneMargin = 40.0;
};

#endif // PENELOPE_DOCUMENTVIEW_H
