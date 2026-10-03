/*
 * pdfboxrenderer.cpp --- PDF content stream backend for BoxTreeRenderer
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pdfboxrenderer.h"

#include <cstdio>

#include "fontmanager.h"
#include "hersheyfont.h"
#include "pdfwriter.h"

#include <QColor>
#include <ft2build.h>
#include FT_OUTLINE_H

// --- FreeType outline decomposition callbacks (for path rendering) ---

namespace {

QByteArray coord(qreal v) { return QByteArray::number(v, 'f', 2); }

struct OutlineCtx {
    QByteArray *stream;
    qreal scale;
    qreal tx, ty;
    FT_Vector last;
};

int outlineMoveTo(const FT_Vector *to, void *user) {
    auto *c = static_cast<OutlineCtx *>(user);
    *c->stream += coord(to->x * c->scale + c->tx) + " "
                + coord(to->y * c->scale + c->ty) + " m\n";
    c->last = *to;
    return 0;
}

int outlineLineTo(const FT_Vector *to, void *user) {
    auto *c = static_cast<OutlineCtx *>(user);
    *c->stream += coord(to->x * c->scale + c->tx) + " "
                + coord(to->y * c->scale + c->ty) + " l\n";
    c->last = *to;
    return 0;
}

int outlineConicTo(const FT_Vector *ctrl, const FT_Vector *to, void *user) {
    auto *c = static_cast<OutlineCtx *>(user);
    qreal cp1x = (c->last.x + 2.0 * ctrl->x) / 3.0;
    qreal cp1y = (c->last.y + 2.0 * ctrl->y) / 3.0;
    qreal cp2x = (to->x + 2.0 * ctrl->x) / 3.0;
    qreal cp2y = (to->y + 2.0 * ctrl->y) / 3.0;
    *c->stream += coord(cp1x * c->scale + c->tx) + " "
                + coord(cp1y * c->scale + c->ty) + " "
                + coord(cp2x * c->scale + c->tx) + " "
                + coord(cp2y * c->scale + c->ty) + " "
                + coord(to->x * c->scale + c->tx) + " "
                + coord(to->y * c->scale + c->ty) + " c\n";
    c->last = *to;
    return 0;
}

int outlineCubicTo(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user) {
    auto *c = static_cast<OutlineCtx *>(user);
    *c->stream += coord(c1->x * c->scale + c->tx) + " "
                + coord(c1->y * c->scale + c->ty) + " "
                + coord(c2->x * c->scale + c->tx) + " "
                + coord(c2->y * c->scale + c->ty) + " "
                + coord(to->x * c->scale + c->tx) + " "
                + coord(to->y * c->scale + c->ty) + " c\n";
    c->last = *to;
    return 0;
}

} // anonymous namespace

// --- Constructor and setters ---

PdfBoxRenderer::PdfBoxRenderer(FontManager *fontManager)
    : BoxTreeRenderer(fontManager)
{
}

void PdfBoxRenderer::setContentOrigin(qreal originX, qreal contentTopY)
{
    m_originX = originX;
    m_contentTopY = contentTopY;
}

// --- PDF coordinate helpers ---

QByteArray PdfBoxRenderer::pdfCoord(qreal v)
{
    QByteArray out;
    out.reserve(16);
    appendCoord(out, v);
    return out;
}

// Composing operators out of returned QByteArrays allocated once per number and
// again for every concatenation, which dominated content-stream emission. These
// append straight into the destination instead.
//
// The number formatting deliberately still goes through QByteArray::number:
// Qt rounds a decimal halfway case away from zero where printf rounds half to
// even, so 88.125 prints as "88.13" via Qt and "88.12" via snprintf. Font
// metrics produce exact quarter/eighth values constantly, so substituting
// snprintf here silently shifted glyph positions. Output is byte-for-byte
// identical to the previous returning-value forms.
void PdfBoxRenderer::appendCoord(QByteArray &out, qreal v)
{
    out.append(QByteArray::number(v, 'f', 2));
}

void PdfBoxRenderer::appendHex16(QByteArray &out, quint16 v)
{
    static const char kHex[] = "0123456789ABCDEF";
    out.append('<');
    out.append(kHex[(v >> 12) & 0xf]);
    out.append(kHex[(v >> 8) & 0xf]);
    out.append(kHex[(v >> 4) & 0xf]);
    out.append(kHex[v & 0xf]);
    out.append('>');
}

void PdfBoxRenderer::appendRect(QByteArray &out, qreal x, qreal y, qreal w, qreal h)
{
    appendCoord(out, x);
    out.append(' ');
    appendCoord(out, y);
    out.append(' ');
    appendCoord(out, w);
    out.append(' ');
    appendCoord(out, h);
    out.append(" re");
}

void PdfBoxRenderer::appendColorOperator(QByteArray &out, const QColor &color, bool fill)
{
    if (!color.isValid())
        return;
    appendCoord(out, color.redF());
    out.append(' ');
    appendCoord(out, color.greenF());
    out.append(' ');
    appendCoord(out, color.blueF());
    out.append(fill ? " rg\n" : " RG\n");
}

QByteArray PdfBoxRenderer::colorOperator(const QColor &color, bool fill)
{
    QByteArray out;
    out.reserve(32);
    appendColorOperator(out, color, fill);
    return out;
}

QByteArray PdfBoxRenderer::toUtf16BeHex(const QString &text)
{
    QByteArray hex;
    hex += "FEFF";
    for (QChar ch : text)
        hex += QByteArray::number(ch.unicode(), 16).toUpper().rightJustified(4, '0');
    return hex;
}

// --- Drawing primitives ---

void PdfBoxRenderer::drawRect(const QRectF &rect, const QColor &fill,
                               const QColor &stroke, qreal strokeWidth)
{
    if (!m_stream) return;

    // Convert from layout coordinates (top-down) to PDF coordinates (bottom-up)
    qreal pdfLeft = rect.x();
    qreal pdfBottom = pdfY(rect.y()) - rect.height();

    if (fill.isValid()) {
        *m_stream += "q\n";
        appendColorOperator(*m_stream, fill, true);
        appendRect(*m_stream, pdfLeft, pdfBottom, rect.width(), rect.height());
        *m_stream += " f\n";
        *m_stream += "Q\n";
    }
    if (stroke.isValid()) {
        *m_stream += "q\n";
        appendColorOperator(*m_stream, stroke, false);
        appendCoord(*m_stream, strokeWidth);
        *m_stream += " w\n";
        appendRect(*m_stream, pdfLeft, pdfBottom, rect.width(), rect.height());
        *m_stream += " S\n";
        *m_stream += "Q\n";
    }
}

void PdfBoxRenderer::drawRoundedRect(const QRectF &rect, qreal xRadius, qreal yRadius,
                                      const QColor &fill, const QColor &stroke,
                                      qreal strokeWidth)
{
    if (!m_stream) return;

    // Convert to PDF coordinates
    qreal cx = rect.x();
    qreal cy = pdfY(rect.y()) - rect.height(); // bottom-left in PDF
    qreal w = rect.width();
    qreal h = rect.height();
    qreal r = qMin(xRadius, yRadius);

    *m_stream += "q\n";

    if (stroke.isValid()) {
        *m_stream += colorOperator(stroke, false);
        *m_stream += pdfCoord(strokeWidth) + " w\n";
    }

    // Rounded rectangle path (clockwise from bottom-left)
    // Bottom edge
    *m_stream += pdfCoord(cx + r) + " " + pdfCoord(cy) + " m\n";
    *m_stream += pdfCoord(cx + w - r) + " " + pdfCoord(cy) + " l\n";
    // Bottom-right corner
    *m_stream += pdfCoord(cx + w) + " " + pdfCoord(cy) + " "
            + pdfCoord(cx + w) + " " + pdfCoord(cy + r) + " v\n";
    // Right edge
    *m_stream += pdfCoord(cx + w) + " " + pdfCoord(cy + h - r) + " l\n";
    // Top-right corner
    *m_stream += pdfCoord(cx + w) + " " + pdfCoord(cy + h) + " "
            + pdfCoord(cx + w - r) + " " + pdfCoord(cy + h) + " v\n";
    // Top edge
    *m_stream += pdfCoord(cx + r) + " " + pdfCoord(cy + h) + " l\n";
    // Top-left corner
    *m_stream += pdfCoord(cx) + " " + pdfCoord(cy + h) + " "
            + pdfCoord(cx) + " " + pdfCoord(cy + h - r) + " v\n";
    // Left edge
    *m_stream += pdfCoord(cx) + " " + pdfCoord(cy + r) + " l\n";
    // Bottom-left corner
    *m_stream += pdfCoord(cx) + " " + pdfCoord(cy) + " "
            + pdfCoord(cx + r) + " " + pdfCoord(cy) + " v\n";

    if (fill.isValid() && stroke.isValid()) {
        *m_stream += colorOperator(fill, true);
        *m_stream += "B\n"; // fill + stroke
    } else if (fill.isValid()) {
        *m_stream += colorOperator(fill, true);
        *m_stream += "f\n";
    } else {
        *m_stream += "S\n"; // stroke only
    }

    *m_stream += "Q\n";
}

void PdfBoxRenderer::drawLine(const QPointF &p1, const QPointF &p2,
                               const QColor &color, qreal width)
{
    if (!m_stream) return;

    *m_stream += "q\n" + colorOperator(color, false);
    appendCoord(*m_stream, width);
    *m_stream += " w\n";
    appendCoord(*m_stream, p1.x());
    m_stream->append(' ');
    appendCoord(*m_stream, pdfY(p1.y()));
    *m_stream += " m ";
    appendCoord(*m_stream, p2.x());
    m_stream->append(' ');
    appendCoord(*m_stream, pdfY(p2.y()));
    *m_stream += " l S\n";
    *m_stream += "Q\n";
}

void PdfBoxRenderer::drawPolyline(const QPolygonF &poly, const QColor &color,
                                   qreal width, Qt::PenCapStyle cap,
                                   Qt::PenJoinStyle join)
{
    if (!m_stream || poly.size() < 2) return;

    *m_stream += "q\n";
    *m_stream += colorOperator(color, false);
    *m_stream += pdfCoord(width) + " w\n";

    // Map Qt cap/join to PDF
    int pdfCap = 0; // butt
    if (cap == Qt::RoundCap) pdfCap = 1;
    else if (cap == Qt::SquareCap) pdfCap = 2;
    *m_stream += QByteArray::number(pdfCap) + " J\n";

    int pdfJoin = 0; // miter
    if (join == Qt::RoundJoin) pdfJoin = 1;
    else if (join == Qt::BevelJoin) pdfJoin = 2;
    *m_stream += QByteArray::number(pdfJoin) + " j\n";

    *m_stream += pdfCoord(poly[0].x()) + " " + pdfCoord(pdfY(poly[0].y())) + " m\n";
    for (int i = 1; i < poly.size(); ++i)
        *m_stream += pdfCoord(poly[i].x()) + " " + pdfCoord(pdfY(poly[i].y())) + " l\n";
    *m_stream += "S\n";
    *m_stream += "Q\n";
}

void PdfBoxRenderer::drawCheckmark(const QPolygonF &poly, const QColor &color,
                                    qreal width)
{
    // Checkmark uses round caps and joins
    drawPolyline(poly, color, width, Qt::RoundCap, Qt::RoundJoin);
}

void PdfBoxRenderer::drawGlyphs(FontFace *face, qreal fontSize,
                                  const GlyphRenderInfo &info,
                                  const QColor &foreground,
                                  qreal x, qreal baselineY)
{
    if (!m_stream || !face || info.glyphIds.isEmpty()) return;

    // Dispatch based on export options
    if (m_exportOptions.xobjectGlyphs)
        drawGlyphsAsXObject(face, fontSize, info, foreground, x, baselineY);
    else if (m_exportOptions.markdownCopy || m_hasHersheyGlyphs)
        drawGlyphsAsPath(face, fontSize, info, foreground, x, baselineY);
    else
        drawGlyphsCIDFont(face, fontSize, info, foreground, x, baselineY);
}

void PdfBoxRenderer::drawGlyphsCIDFont(FontFace *face, qreal fontSize,
                                         const GlyphRenderInfo &info,
                                         const QColor &foreground,
                                         qreal x, qreal baselineY)
{
    if (!m_pdfFontNameCb) return;

    QByteArray fontName = m_pdfFontNameCb(face);

    // Convert baselineY from layout to PDF
    qreal pdfBaseY = pdfY(baselineY);

    *m_stream += "BT\n";
    m_stream->append('/');
    m_stream->append(fontName);
    m_stream->append(' ');
    appendCoord(*m_stream, fontSize);
    m_stream->append(" Tf\n");
    appendColorOperator(*m_stream, foreground, true);

    const int count = info.glyphIds.size();

    // Prefer one TJ array for the whole run. The viewer advances by the /W
    // width after each glyph, so an adjustment restores the position the
    // layout asked for. Adjustments are computed against the very /W values
    // the viewer uses, which is why HarfBuzz and FreeType disagreeing on
    // advances does not matter: the quantisation cancels.
    bool useRun = m_runText && m_glyphWidthCb && count > 1 && fontSize > 0;
    QList<int> widths(count, -1);
    if (useRun) {
        for (int i = 0; i < count; ++i) {
            widths[i] = m_glyphWidthCb(face, info.glyphIds[i]);
            if (widths[i] < 0) {
                // No /W entry: the viewer would substitute /DW and the advance
                // would be unpredictable, so fall back for the whole run.
                useRun = false;
                break;
            }
        }
    }

    if (useRun) {
        const qreal runX = x + info.positions[0].x();
        const qreal runY = pdfBaseY - info.positions[0].y();
        m_stream->append("1 0 0 1 ");
        appendCoord(*m_stream, runX);
        m_stream->append(' ');
        appendCoord(*m_stream, runY);
        m_stream->append(" Tm[");

        for (int i = 0; i < count; ++i) {
            if (i > 0) {
                // Natural pen after glyph i-1 versus where glyph i belongs.
                const qreal natural = info.positions[i - 1].x()
                                    + widths[i - 1] * fontSize / 1000.0;
                const qreal shift = info.positions[i].x() - natural;
                if (qAbs(shift) > 1e-9) {
                    char buf[32];
                    const int n = std::snprintf(buf, sizeof(buf), "%.2f",
                                                -1000.0 * shift / fontSize);
                    if (n > 0)
                        m_stream->append(buf, n);
                }
            }
            appendHex16(*m_stream, static_cast<quint16>(info.glyphIds[i]));
        }
        m_stream->append("] TJ\n");

        if (m_markGlyphUsedCb)
            for (int i = 0; i < count; ++i)
                m_markGlyphUsedCb(face, info.glyphIds[i]);
        *m_stream += "ET\n";
        return;
    }

    for (int i = 0; i < count; ++i) {
        qreal gx = x + info.positions[i].x();
        qreal gy = pdfBaseY - info.positions[i].y(); // layout yOffset is top-down

        m_stream->append("1 0 0 1 ");
        appendCoord(*m_stream, gx);
        m_stream->append(' ');
        appendCoord(*m_stream, gy);
        m_stream->append(" Tm\n");
        appendHex16(*m_stream, static_cast<quint16>(info.glyphIds[i]));
        m_stream->append(" Tj\n");

        // Mark glyph used for subsetting
        if (m_markGlyphUsedCb)
            m_markGlyphUsedCb(face, info.glyphIds[i]);
    }

    *m_stream += "ET\n";
}

void PdfBoxRenderer::drawGlyphsAsPath(FontFace *face, qreal fontSize,
                                        const GlyphRenderInfo &info,
                                        const QColor &foreground,
                                        qreal x, qreal baselineY)
{
    if (!face->ftFace) return;

    FT_Face ftFace = face->ftFace;
    qreal scale = fontSize / ftFace->units_per_EM;
    qreal pdfBaseY = pdfY(baselineY);

    *m_stream += "q\n";
    *m_stream += colorOperator(foreground, true);

    FT_Outline_Funcs funcs = {};
    funcs.move_to = outlineMoveTo;
    funcs.line_to = outlineLineTo;
    funcs.conic_to = outlineConicTo;
    funcs.cubic_to = outlineCubicTo;

    for (int i = 0; i < info.glyphIds.size(); ++i) {
        qreal gx = x + info.positions[i].x();
        qreal gy = pdfBaseY - info.positions[i].y();

        if (FT_Load_Glyph(ftFace, info.glyphIds[i], FT_LOAD_NO_SCALE) == 0
            && ftFace->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
            OutlineCtx ctx;
            ctx.stream = m_stream;
            ctx.scale = scale;
            ctx.tx = gx;
            ctx.ty = gy;
            ctx.last = {0, 0};
            FT_Outline_Decompose(&ftFace->glyph->outline, &funcs, &ctx);
        }
    }

    *m_stream += "f\n";
    *m_stream += "Q\n";
}

void PdfBoxRenderer::drawGlyphsAsXObject(FontFace *face, qreal fontSize,
                                           const GlyphRenderInfo &info,
                                           const QColor &foreground,
                                           qreal x, qreal baselineY)
{
    if (!face->ftFace || !m_glyphFormCb) return;

    FT_Face ftFace = face->ftFace;
    qreal scale = fontSize / ftFace->units_per_EM;
    qreal pdfBaseY = pdfY(baselineY);

    for (int i = 0; i < info.glyphIds.size(); ++i) {
        auto entry = m_glyphFormCb(nullptr, face, info.glyphIds[i], false);
        if (entry.objId == 0) continue;

        qreal gx = x + info.positions[i].x();
        qreal gy = pdfBaseY - info.positions[i].y();

        *m_stream += "q\n";
        *m_stream += colorOperator(foreground, true);
        *m_stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                + " " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
        *m_stream += "/" + entry.pdfName + " Do\n";
        *m_stream += "Q\n";
    }
}

void PdfBoxRenderer::drawHersheyStrokes(const QVector<QVector<QPointF>> &strokes,
                                          const QTransform &transform,
                                          const QColor &foreground,
                                          qreal strokeWidth)
{
    // Fallback: draw inline stroke operators for Hershey glyphs.
    // The primary Hershey path uses renderHersheyGlyphBox() override with
    // XObject references. This fallback handles edge cases like trailing hyphens.
    if (!m_stream || strokes.isEmpty()) return;

    *m_stream += "q\n";
    *m_stream += colorOperator(foreground, false);
    *m_stream += pdfCoord(strokeWidth) + " w\n";
    *m_stream += "1 J 1 j\n"; // round cap & join

    for (const auto &stroke : strokes) {
        if (stroke.size() < 2) continue;
        // Apply the transform to each point.
        // The transform includes scale + skew + translate and already maps
        // to layout coordinates (with Y increasing downward). We still need
        // to flip Y for PDF.
        QPointF p0 = transform.map(stroke[0]);
        *m_stream += pdfCoord(p0.x()) + " " + pdfCoord(pdfY(p0.y())) + " m\n";
        for (int i = 1; i < stroke.size(); ++i) {
            QPointF pt = transform.map(stroke[i]);
            *m_stream += pdfCoord(pt.x()) + " " + pdfCoord(pdfY(pt.y())) + " l\n";
        }
        *m_stream += "S\n";
    }

    *m_stream += "Q\n";
}

void PdfBoxRenderer::drawImage(const QRectF &destRect, const QImage &image)
{
    // No-op: PDF images require resource names, not raw QImage data.
    // Image rendering is handled by renderImageBlock() override which
    // uses the imageNameCallback to get the PDF resource name.
    Q_UNUSED(destRect);
    Q_UNUSED(image);
}

void PdfBoxRenderer::pushState()
{
    if (m_stream)
        *m_stream += "q\n";
}

void PdfBoxRenderer::popState()
{
    if (m_stream)
        *m_stream += "Q\n";
}

void PdfBoxRenderer::collectLink(const QRectF &rect, const QString &href)
{
    if (href.isEmpty()) return;

    // Convert from layout coordinates to PDF coordinates.
    // rect is in layout space: (x, baselineY - ascent, width, ascent + descent)
    // Link annotations use absolute page coordinates (not affected by CTM),
    // so we must add m_originX manually.
    PdfLinkAnnotation annot;
    annot.rect = QRectF(m_originX + rect.x(),
                         pdfY(rect.y() + rect.height()), // bottom in PDF
                         rect.width(),
                         rect.height());
    annot.href = href;
    m_linkAnnotations.append(annot);
}

// --- Traversal overrides ---

void PdfBoxRenderer::renderBlockBox(const Layout::BlockBox &box)
{
    if (!m_stream) return;

    // Markdown copy: code block opening fence
    bool isCodeBlock = (box.type == Layout::BlockBox::CodeBlockType);
    if (m_exportOptions.markdownCopy && isCodeBlock)
        m_codeBlockLines = true;

    if (m_exportOptions.markdownCopy && isCodeBlock && box.isFragmentStart) {
        QString fence = QStringLiteral("```");
        if (!box.codeLanguage.isEmpty())
            fence += box.codeLanguage;
        fence += QLatin1Char('\n');
        *m_stream += "/Span <</ActualText <" + toUtf16BeHex(fence) + ">>> BDC\n";
        // Invisible text anchor at block top
        writeInvisibleAnchor(box.x, pdfY(box.y));
        *m_stream += "EMC\n";
    }

    // Markdown copy: HRule gets "---\n\n" ActualText before visual rendering
    if (m_exportOptions.markdownCopy && box.type == Layout::BlockBox::HRuleBlock) {
        *m_stream += "/Span <</ActualText <" + toUtf16BeHex(QStringLiteral("---\n\n")) + ">>> BDC\n";
        writeInvisibleAnchor(box.x, pdfY(box.y + box.height / 2));
        *m_stream += "EMC\n";
    }

    // Delegate to base class for the actual visual rendering
    // (background, border, hrule line, blockquote border, lines)
    BoxTreeRenderer::renderBlockBox(box);

    m_codeBlockLines = false;

    // Markdown copy: emit block separator
    if (m_exportOptions.markdownCopy && box.isFragmentEnd) {
        QString sep;
        if (isCodeBlock)
            sep = QStringLiteral("```\n\n");
        else if (box.isListItem)
            sep = QStringLiteral("\n");
        else
            sep = QStringLiteral("\n\n");
        *m_stream += "/Span <</ActualText <" + toUtf16BeHex(sep) + ">>> BDC\n";
        if (isCodeBlock)
            writeInvisibleAnchor(box.x, pdfY(box.y + box.height));
        *m_stream += "EMC\n";
    }
}

void PdfBoxRenderer::renderLineBox(const Layout::LineBox &line,
                                    qreal originX, qreal originY, qreal availWidth)
{
    if (!m_stream) return;

    bool markdownMode = m_exportOptions.markdownCopy;
    bool xobjectGlyphs = m_exportOptions.xobjectGlyphs;

    if (!markdownMode) {
        // Non-markdown: delegate to base class
        BoxTreeRenderer::renderLineBox(line, originX, originY, availWidth);
        return;
    }

    // --- Markdown copy mode ---

    qreal baselineY = originY + line.baseline;
    qreal pdfBaseY = pdfY(baselineY);

    // Phase 1: compute x-positions
    QList<qreal> glyphXPositions = computeGlyphXPositions(line, originX, availWidth);

    if (line.glyphs.isEmpty())
        return;

    // Phase 2: build line ActualText
    QString lineText;
    for (int i = 0; i < line.glyphs.size(); ++i) {
        const auto &gbox = line.glyphs[i];
        QString word = gbox.text.trimmed();
        if (gbox.isListMarker) {
            word.replace(QChar(0x2022), QLatin1Char('-'));
            lineText += word;
        } else {
            lineText += gbox.mdPrefix + word + gbox.mdSuffix;
        }
        if (i < line.glyphs.size() - 1) {
            bool trailingSpace = !gbox.text.isEmpty()
                               && gbox.text.at(gbox.text.size() - 1).isSpace();
            if (trailingSpace)
                lineText += QLatin1Char(' ');
        }
    }
    // Trailing separator
    if (m_codeBlockLines) {
        // no trailing separator
    } else if (!line.isLastLine) {
        if (!line.glyphs.isEmpty() && line.glyphs.last().trailingSoftHyphen) {
            // Soft-hyphen break: word continues on next line, no separator
        } else {
            lineText += QLatin1Char(' ');
        }
    } else {
        lineText += QLatin1Char('\n');
    }

    // Phase 3: emit BDC ActualText span
    *m_stream += "/Span <</ActualText <" + toUtf16BeHex(lineText) + ">>> BDC\n";

    // Phase 4: invisible text overlay (per-glyph Tm matching visual positions)
    *m_stream += "BT\n";
    *m_stream += "3 Tr\n";
    for (int i = 0; i < line.glyphs.size(); ++i) {
        const auto &gbox = line.glyphs[i];
        if (gbox.glyphs.isEmpty() || !gbox.font)
            continue;
        QByteArray fontName;
        if (gbox.font->isHershey || xobjectGlyphs)
            fontName = "HvInv";
        else if (m_pdfFontNameCb)
            fontName = m_pdfFontNameCb(gbox.font);
        else
            continue;
        *m_stream += "/" + fontName + " " + pdfCoord(gbox.fontSize) + " Tf\n";
        qreal curX = glyphXPositions[i];
        for (const auto &g : gbox.glyphs) {
            qreal px = curX + g.xOffset;
            qreal py = pdfBaseY + g.yOffset; // PDF: yOffset goes up
            if (gbox.style.superscript)
                py += gbox.fontSize * RenderConstants::kSuperscriptRise;
            else if (gbox.style.subscript)
                py -= gbox.fontSize * RenderConstants::kSubscriptDrop;
            *m_stream += "1 0 0 1 " + pdfCoord(px) + " " + pdfCoord(py) + " Tm\n";
            *m_stream += Pdf::toHexString16(static_cast<quint16>(g.glyphId)) + " Tj\n";
            curX += g.xAdvance;
        }
    }
    *m_stream += "0 Tr\n";
    *m_stream += "ET\n";
    *m_stream += "EMC\n";

    // Phase 5: render visible glyphs at computed positions
    for (int i = 0; i < line.glyphs.size(); ++i)
        renderGlyphBox(line.glyphs[i], glyphXPositions[i], baselineY);

    // Phase 6: trailing soft-hyphen
    qreal x = glyphXPositions.isEmpty() ? originX
        : glyphXPositions.last() + line.glyphs.last().width;

    if (line.showTrailingHyphen && !line.glyphs.isEmpty()) {
        renderTrailingHyphen(line.glyphs.last(), x, baselineY);
    }
}

void PdfBoxRenderer::renderImageBlock(const Layout::BlockBox &box)
{
    if (!m_stream || box.image.isNull() || box.imageId.isEmpty() || !m_imageNameCb)
        return;

    QByteArray imgName = m_imageNameCb(box.imageId);
    if (imgName.isEmpty())
        return;

    // PDF image rendering: translate + scale with cm, then paint with Do
    qreal imgX = box.x;
    qreal imgY = pdfY(box.y) - box.imageHeight;

    *m_stream += "q\n";
    *m_stream += pdfCoord(box.imageWidth) + " 0 0 "
            + pdfCoord(box.imageHeight) + " "
            + pdfCoord(imgX) + " " + pdfCoord(imgY) + " cm\n";
    *m_stream += "/" + imgName + " Do\n";
    *m_stream += "Q\n";
}

void PdfBoxRenderer::renderHersheyGlyphBox(const Layout::GlyphBox &gbox,
                                             qreal x, qreal baselineY)
{
    if (gbox.glyphs.isEmpty() || !gbox.font || !gbox.font->hersheyFont)
        return;
    if (!m_stream || !m_glyphFormCb)
        return;

    HersheyFont *hFont = gbox.font->hersheyFont;
    qreal fontSize = gbox.fontSize;
    qreal scale = fontSize / hFont->unitsPerEm();

    drawInlineBackground(gbox, x, baselineY);

    qreal pdfBaseY = pdfY(baselineY);

    qreal curX = x;
    for (const auto &g : gbox.glyphs) {
        auto entry = m_glyphFormCb(hFont, nullptr, g.glyphId, gbox.font->hersheyBold);
        if (entry.objId == 0) {
            curX += g.xAdvance;
            continue;
        }

        qreal gx = curX + g.xOffset;
        qreal gy = pdfBaseY + g.yOffset; // PDF: yOffset goes up

        // Superscript/subscript adjustment (in PDF, up = positive)
        if (gbox.style.superscript)
            gy += fontSize * RenderConstants::kSuperscriptRise;
        else if (gbox.style.subscript)
            gy -= fontSize * RenderConstants::kSubscriptDrop;

        *m_stream += "q\n";
        *m_stream += colorOperator(gbox.style.foreground, false); // stroke color

        if (gbox.font->hersheyItalic) {
            // Scale + italic skew + translate
            *m_stream += pdfCoord(scale) + " 0 "
                    + pdfCoord(scale * HersheyConstants::kItalicSkew) + " " + pdfCoord(scale)
                    + " " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
        } else {
            // Scale + translate only
            *m_stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                    + " " + pdfCoord(gx) + " " + pdfCoord(gy) + " cm\n";
        }

        *m_stream += "/" + entry.pdfName + " Do\n";
        *m_stream += "Q\n";

        curX += g.xAdvance;
    }

    renderGlyphDecorations(gbox, x, baselineY, curX);
}

// --- Helpers ---

void PdfBoxRenderer::writeInvisibleAnchor(qreal x, qreal pdfBaseY)
{
    if (!m_stream || !m_embeddedFonts || m_embeddedFonts->isEmpty())
        return;
    *m_stream += "BT\n3 Tr\n";
    *m_stream += "/" + m_embeddedFonts->first().pdfName + " 1 Tf\n";
    *m_stream += "1 0 0 1 " + pdfCoord(x) + " " + pdfCoord(pdfBaseY) + " Tm\n";
    *m_stream += "<0000> Tj\n";
    *m_stream += "0 Tr\nET\n";
}

// --- Trailing hyphen helper ---

void PdfBoxRenderer::renderTrailingHyphen(const Layout::GlyphBox &lastGbox, qreal x,
                                           qreal baselineY)
{
    if (!lastGbox.font || !m_stream) return;

    qreal pdfBaseY = pdfY(baselineY);

    if (lastGbox.font->isHershey && lastGbox.font->hersheyFont) {
        // Hershey hyphen via XObject
        if (!m_glyphFormCb) return;
        HersheyFont *hFont = lastGbox.font->hersheyFont;
        auto entry = m_glyphFormCb(hFont, nullptr, uint(U'-'), lastGbox.font->hersheyBold);
        if (entry.objId != 0) {
            qreal scale = lastGbox.fontSize / hFont->unitsPerEm();
            *m_stream += "q\n";
            *m_stream += colorOperator(lastGbox.style.foreground, false);
            if (lastGbox.font->hersheyItalic) {
                *m_stream += pdfCoord(scale) + " 0 "
                        + pdfCoord(scale * HersheyConstants::kItalicSkew) + " " + pdfCoord(scale)
                        + " " + pdfCoord(x) + " " + pdfCoord(pdfBaseY) + " cm\n";
            } else {
                *m_stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                        + " " + pdfCoord(x) + " " + pdfCoord(pdfBaseY) + " cm\n";
            }
            *m_stream += "/" + entry.pdfName + " Do\n";
            *m_stream += "Q\n";
        }
    } else if (lastGbox.font->ftFace) {
        FT_UInt hyphenGid = FT_Get_Char_Index(lastGbox.font->ftFace, '-');

        if (m_exportOptions.xobjectGlyphs) {
            // XObject mode
            if (m_glyphFormCb) {
                auto entry = m_glyphFormCb(nullptr, lastGbox.font, hyphenGid, false);
                if (entry.objId != 0) {
                    qreal scale = lastGbox.fontSize / lastGbox.font->ftFace->units_per_EM;
                    *m_stream += "q\n";
                    *m_stream += colorOperator(lastGbox.style.foreground, true);
                    *m_stream += pdfCoord(scale) + " 0 0 " + pdfCoord(scale)
                            + " " + pdfCoord(x) + " " + pdfCoord(pdfBaseY) + " cm\n";
                    *m_stream += "/" + entry.pdfName + " Do\n";
                    *m_stream += "Q\n";
                }
            }
        } else if (m_exportOptions.markdownCopy || m_hasHersheyGlyphs) {
            // Path rendering
            FT_Face face = lastGbox.font->ftFace;
            if (FT_Load_Glyph(face, hyphenGid, FT_LOAD_NO_SCALE) == 0
                && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
                qreal scale = lastGbox.fontSize / face->units_per_EM;
                FT_Outline_Funcs funcs = {};
                funcs.move_to = outlineMoveTo;
                funcs.line_to = outlineLineTo;
                funcs.conic_to = outlineConicTo;
                funcs.cubic_to = outlineCubicTo;
                OutlineCtx ctx;
                ctx.stream = m_stream;
                ctx.scale = scale;
                ctx.tx = x;
                ctx.ty = pdfBaseY;
                ctx.last = {0, 0};
                *m_stream += "q\n";
                *m_stream += colorOperator(lastGbox.style.foreground, true);
                FT_Outline_Decompose(&face->glyph->outline, &funcs, &ctx);
                *m_stream += "f\nQ\n";
            }
        } else {
            // Standard CIDFont mode
            if (m_markGlyphUsedCb)
                m_markGlyphUsedCb(lastGbox.font, hyphenGid);
            QByteArray fontName;
            if (m_pdfFontNameCb)
                fontName = m_pdfFontNameCb(lastGbox.font);
            else
                return;
            *m_stream += "BT\n";
            *m_stream += "/" + fontName + " " + pdfCoord(lastGbox.fontSize) + " Tf\n";
            *m_stream += colorOperator(lastGbox.style.foreground, true);
            *m_stream += "1 0 0 1 " + pdfCoord(x) + " " + pdfCoord(pdfBaseY) + " Tm\n";
            *m_stream += Pdf::toHexString16(static_cast<quint16>(hyphenGid)) + " Tj\n";
            *m_stream += "ET\n";
        }
    }
}
