/*
 * pdfboxrenderer.h --- PDF content stream backend for BoxTreeRenderer
 *
 * Subclasses BoxTreeRenderer to write PDF operators to a QByteArray.
 * Handles the Y-axis flip (layout top-down -> PDF bottom-up) and
 * overrides renderLineBox() / renderBlockBox() for ActualText markdown
 * copy mode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFBOXRENDERER_H
#define PENELOPE_PDFBOXRENDERER_H

#include "boxtreerenderer.h"
#include "pdfexportoptions.h"

#include <QByteArray>
#include <QList>
#include <QRectF>
#include <QString>

#include <functional>

class HersheyFont;

// Forward-declare types from PdfGenerator that callbacks need
// (avoids circular header dependency).
struct GlyphFormEntry {
    uint32_t objId = 0;
    QByteArray pdfName;    // "HG0", "HG1", ...
    qreal advanceWidth = 0; // in glyph units
};

struct EmbeddedFont {
    uint32_t fontObjId = 0;
    QByteArray pdfName; // e.g. "F0", "F1"
    FontFace *face = nullptr;
};

/// Link annotation collected during PDF rendering (PDF coordinates).
struct PdfLinkAnnotation {
    QRectF rect;      // PDF coordinates (bottom-up)
    QString href;
};

class PdfBoxRenderer : public BoxTreeRenderer
{
public:
    explicit PdfBoxRenderer(FontManager *fontManager);

    // --- Configuration setters ---

    /// Set the output byte array where PDF operators are written.
    void setStream(QByteArray *stream) { m_stream = stream; }

    /// Set the content area origin in PDF coordinates.
    /// @param originX  left margin in PDF coordinates (same as layout X)
    /// @param contentTopY  Y coordinate of the content area top in PDF
    ///                     (pageHeight - marginTop - header, already flipped)
    void setContentOrigin(qreal originX, qreal contentTopY);

    /// Set the maximum per-gap justify expansion.
    void setMaxJustifyGap(qreal gap) { m_maxJustifyGap = gap; }

    /// Set export options (markdown copy, xobject glyphs, etc.)
    void setExportOptions(const PdfExportOptions &opts) { m_exportOptions = opts; }

    /// Set whether the document contains Hershey font glyphs.
    void setHasHersheyGlyphs(bool has) { m_hasHersheyGlyphs = has; }

    // --- Callback setters (wired by PdfGenerator) ---

    /// Get the PDF resource name for a FontFace (e.g. "F0").
    void setPdfFontNameCallback(std::function<QByteArray(FontFace *)> cb) {
        m_pdfFontNameCb = std::move(cb);
    }

    /// Ensure a glyph form XObject exists; returns entry with pdfName/objId.
    void setGlyphFormCallback(
        std::function<GlyphFormEntry(const HersheyFont *, FontFace *, uint, bool)> cb) {
        m_glyphFormCb = std::move(cb);
    }

    /// Mark a glyph as used for font subsetting.
    void setMarkGlyphUsedCallback(std::function<void(FontFace *, uint)> cb) {
        m_markGlyphUsedCb = std::move(cb);
    }

    /// Reference to the embedded font list (for ActualText invisible text).
    void setEmbeddedFontsRef(const QList<EmbeddedFont> *fonts) {
        m_embeddedFonts = fonts;
    }

    /// Get the PDF resource name for an image by its imageId.
    void setImageNameCallback(std::function<QByteArray(const QString &)> cb) {
        m_imageNameCb = std::move(cb);
    }

    /// Access collected link annotations after rendering.
    const QList<PdfLinkAnnotation> &linkAnnotations() const { return m_linkAnnotations; }

    /// Clear link annotations (call before each page).
    void clearLinkAnnotations() { m_linkAnnotations.clear(); }

    // --- Drawing primitives (BoxTreeRenderer overrides) ---

    void drawRect(const QRectF &rect, const QColor &fill,
                  const QColor &stroke = QColor(),
                  qreal strokeWidth = 0) override;

    void drawRoundedRect(const QRectF &rect, qreal xRadius, qreal yRadius,
                         const QColor &fill, const QColor &stroke = QColor(),
                         qreal strokeWidth = 0) override;

    void drawLine(const QPointF &p1, const QPointF &p2,
                  const QColor &color, qreal width = 0.5) override;

    void drawPolyline(const QPolygonF &poly, const QColor &color,
                      qreal width, Qt::PenCapStyle cap = Qt::FlatCap,
                      Qt::PenJoinStyle join = Qt::MiterJoin) override;

    void drawCheckmark(const QPolygonF &poly, const QColor &color,
                       qreal width) override;

    void drawGlyphs(FontFace *face, qreal fontSize,
                    const GlyphRenderInfo &info,
                    const QColor &foreground,
                    qreal x, qreal baselineY) override;

    void drawHersheyStrokes(const QVector<QVector<QPointF>> &strokes,
                            const QTransform &transform,
                            const QColor &foreground,
                            qreal strokeWidth) override;

    void drawImage(const QRectF &destRect, const QImage &image) override;

    void pushState() override;
    void popState() override;

    void collectLink(const QRectF &rect, const QString &href) override;

    // --- Traversal overrides ---

    void renderBlockBox(const Layout::BlockBox &box) override;
    void renderLineBox(const Layout::LineBox &line,
                       qreal originX, qreal originY, qreal availWidth) override;
    void renderHersheyGlyphBox(const Layout::GlyphBox &gbox,
                               qreal x, qreal baselineY) override;
    void renderImageBlock(const Layout::BlockBox &box) override;

private:
    // --- PDF coordinate helpers ---

    /// Convert layout Y (top-down) to PDF Y (bottom-up).
    qreal pdfY(qreal layoutY) const { return m_contentTopY - layoutY; }

    static QByteArray pdfCoord(qreal v);
    static QByteArray colorOperator(const QColor &color, bool fill = true);
    static QByteArray toUtf16BeHex(const QString &text);

    // --- Glyph rendering dispatch ---

    void drawGlyphsCIDFont(FontFace *face, qreal fontSize,
                           const GlyphRenderInfo &info,
                           const QColor &foreground,
                           qreal x, qreal baselineY);
    void drawGlyphsAsPath(FontFace *face, qreal fontSize,
                          const GlyphRenderInfo &info,
                          const QColor &foreground,
                          qreal x, qreal baselineY);
    void drawGlyphsAsXObject(FontFace *face, qreal fontSize,
                             const GlyphRenderInfo &info,
                             const QColor &foreground,
                             qreal x, qreal baselineY);

    // --- Helpers ---

    /// Write an invisible text anchor at (x, pdfY) for ActualText spans.
    void writeInvisibleAnchor(qreal x, qreal pdfBaseY);

    void renderTrailingHyphen(const Layout::GlyphBox &lastGbox, qreal x,
                              qreal baselineY);

    // --- State ---

    QByteArray *m_stream = nullptr;
    qreal m_originX = 0;
    qreal m_contentTopY = 0;
    qreal m_maxJustifyGap = 14.0;
    PdfExportOptions m_exportOptions;
    bool m_hasHersheyGlyphs = false;
    bool m_codeBlockLines = false;

    // Callbacks
    std::function<QByteArray(FontFace *)> m_pdfFontNameCb;
    std::function<GlyphFormEntry(const HersheyFont *, FontFace *, uint, bool)> m_glyphFormCb;
    std::function<void(FontFace *, uint)> m_markGlyphUsedCb;
    std::function<QByteArray(const QString &)> m_imageNameCb;
    const QList<EmbeddedFont> *m_embeddedFonts = nullptr;

    // Link annotations collected during rendering
    QList<PdfLinkAnnotation> m_linkAnnotations;
};

#endif // PENELOPE_PDFBOXRENDERER_H
