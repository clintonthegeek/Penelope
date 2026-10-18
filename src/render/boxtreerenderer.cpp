/*
 * boxtreerenderer.cpp — Base class for box tree rendering backends
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "boxtreerenderer.h"
#include "fontmanager.h"
#include "hersheyfont.h"

#include <variant>

BoxTreeRenderer::BoxTreeRenderer(FontManager *fontManager)
    : m_fontManager(fontManager)
{
}

BoxTreeRenderer::~BoxTreeRenderer() = default;

// --- Traversal logic ---

void BoxTreeRenderer::renderElement(const Layout::PageElement &element)
{
    std::visit([&](const auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, Layout::BlockBox>)
            renderBlockBox(e);
        else if constexpr (std::is_same_v<T, Layout::TableBox>)
            renderTableBox(e);
        else if constexpr (std::is_same_v<T, Layout::FootnoteSectionBox>)
            renderFootnoteSectionBox(e);
    }, element);
}

void BoxTreeRenderer::setLineClip(const QRectF &clip) {
    if (clip.isNull()) { m_hasLineClip = false; m_lineClip = QRectF(); }
    else { m_hasLineClip = true; m_lineClip = clip; }
}

void BoxTreeRenderer::renderBlockBox(const Layout::BlockBox &box)
{
    // Background
    if (box.background.isValid()) {
        QRectF bgRect(box.x - box.padding, box.y - box.padding,
                       box.width + box.padding * 2, box.height + box.padding * 2);
        drawRect(bgRect, box.background);

        // Border
        if (box.borderWidth > 0 && box.borderColor.isValid()) {
            drawRect(bgRect, QColor(), box.borderColor, box.borderWidth);
        }
    }

    // Image block
    if (box.type == Layout::BlockBox::ImageBlock) {
        renderImageBlock(box);
        return;
    }

    // Horizontal rule
    if (box.type == Layout::BlockBox::HRuleBlock) {
        qreal ruleY = box.y + box.height / 2;
        drawLine(QPointF(box.x, ruleY), QPointF(box.x + box.width, ruleY),
                 QColor(204, 204, 204), 0.5);
        return;
    }

    // Blockquote left border
    if (box.hasBlockQuoteBorder && box.blockQuoteLevel > 0) {
        qreal borderX = box.blockQuoteIndent - 8.0;
        qreal borderTop = box.y - box.spaceBefore;
        qreal borderBottom = box.y + box.height + box.spaceAfter;
        drawLine(QPointF(borderX, borderTop), QPointF(borderX, borderBottom),
                 QColor(204, 204, 204), 2.0);
    }

    // Lines
    m_lastPaintedLineCount = 0;
    qreal lineY = 0;
    for (int li = 0; li < box.lines.size(); ++li) {
        qreal top = box.y + lineY;
        qreal bottom = top + box.lines[li].height;
        if (m_hasLineClip && (bottom <= m_lineClip.top() || top >= m_lineClip.bottom())) {
            lineY += box.lines[li].height;
            continue;
        }
        qreal lineX = box.x;
        qreal lineAvailWidth = box.width;
        if (li == 0 && box.firstLineIndent != 0) {
            lineX += box.firstLineIndent;
            lineAvailWidth -= box.firstLineIndent;
        }
        renderLineBox(box.lines[li], lineX, box.y + lineY, lineAvailWidth);
        ++m_lastPaintedLineCount;
        lineY += box.lines[li].height;
    }
}

void BoxTreeRenderer::renderTableBox(const Layout::TableBox &box)
{
    qreal tableLeft = box.x;
    qreal tableTop = box.y;

    // === Pass 1: Cell backgrounds ===
    for (const auto &row : box.rows) {
        for (const auto &cell : row.cells) {
            if (cell.background.isValid()) {
                qreal cellX = tableLeft + cell.x;
                qreal cellY = tableTop + cell.y;
                drawRect(QRectF(cellX, cellY, cell.width, cell.height),
                         cell.background);
            }
        }
    }

    // === Pass 2: Cell content ===
    for (const auto &row : box.rows) {
        for (const auto &cell : row.cells) {
            qreal cellX = tableLeft + cell.x;
            qreal cellY = tableTop + cell.y;
            qreal innerX = cellX + box.cellPadding;
            qreal innerY = cellY + box.cellPadding;
            qreal lineY = 0;
            for (const auto &line : cell.lines) {
                renderLineBox(line, innerX, innerY + lineY,
                              cell.width - box.cellPadding * 2);
                lineY += line.height;
            }
        }
    }

    // === Pass 3: Grid borders ===

    // Inner horizontal lines (between rows)
    if (box.innerBorderWidth > 0 && box.innerBorderColor.isValid()) {
        qreal rowY = 0;
        for (int ri = 0; ri < box.rows.size() - 1; ++ri) {
            rowY += box.rows[ri].height;
            // Skip header-bottom line (drawn separately with heavier weight)
            if (ri == box.headerRowCount - 1)
                continue;
            qreal lineY = tableTop + rowY;
            drawLine(QPointF(tableLeft, lineY),
                     QPointF(tableLeft + box.width, lineY),
                     box.innerBorderColor, box.innerBorderWidth);
        }
    }

    // Inner vertical lines (between columns)
    if (box.innerBorderWidth > 0 && box.innerBorderColor.isValid()
        && box.columnPositions.size() > 2) {
        qreal tableBottom = tableTop + box.height;
        for (int ci = 1; ci < box.columnPositions.size() - 1; ++ci) {
            qreal lineX = tableLeft + box.columnPositions[ci];
            drawLine(QPointF(lineX, tableTop),
                     QPointF(lineX, tableBottom),
                     box.innerBorderColor, box.innerBorderWidth);
        }
    }

    // Header bottom border (heavier line under header row)
    if (box.headerRowCount > 0
        && box.headerBottomBorderWidth > 0
        && box.headerBottomBorderColor.isValid()) {
        qreal headerHeight = 0;
        for (int ri = 0; ri < box.headerRowCount && ri < box.rows.size(); ++ri)
            headerHeight += box.rows[ri].height;
        qreal hbY = tableTop + headerHeight;
        drawLine(QPointF(tableLeft, hbY),
                 QPointF(tableLeft + box.width, hbY),
                 box.headerBottomBorderColor, box.headerBottomBorderWidth);
    }

    // Outer border (on top of everything)
    if (box.borderWidth > 0 && box.borderColor.isValid()) {
        drawRect(QRectF(tableLeft, tableTop, box.width, box.height),
                 QColor(), box.borderColor, box.borderWidth);
    }
}

void BoxTreeRenderer::renderFootnoteSectionBox(const Layout::FootnoteSectionBox &box)
{
    qreal sectionY = box.y;

    // Separator line
    if (box.showSeparator) {
        qreal sepWidth = qMin(box.width, box.separatorLength);
        drawLine(QPointF(box.x, sectionY), QPointF(box.x + sepWidth, sectionY),
                 QColor(179, 179, 179), box.separatorWidth);
    }

    for (const auto &fn : box.footnotes) {
        const qreal fnY = sectionY + fn.y;
        // Notes carry laid-out blocks; place each at the note's origin.
        for (const auto &element : fn.elements) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Layout::BlockBox>) {
                    Layout::BlockBox placed = e;
                    placed.x += box.x;
                    placed.y += fnY;
                    renderBlockBox(placed);
                } else if constexpr (std::is_same_v<T, Layout::TableBox>) {
                    Layout::TableBox placed = e;
                    placed.x += box.x;
                    placed.y += fnY;
                    renderTableBox(placed);
                }
            }, element);
        }
    }
}

void BoxTreeRenderer::renderLineImage(const Layout::ImageBox &image, qreal x, qreal y)
{
    if (image.image.isNull() || image.width <= 0 || image.height <= 0)
        return;
    const QRectF rect(x, y, image.width, image.height);
    if (!image.title.isEmpty())
        collectImageHint(rect, image.title);
    drawImage(rect, image.image);
}

void BoxTreeRenderer::collectImageHint(const QRectF &, const QString &)
{
}

void BoxTreeRenderer::renderLineBox(const Layout::LineBox &line,
                                     qreal originX, qreal originY, qreal availWidth)
{
    for (const auto &image : line.images)
        renderLineImage(image, originX, originY);

    qreal baselineY = originY + line.baseline;

    // Justification
    JustifyParams jp = computeJustification(line, availWidth);

    qreal x;

    if (jp.doJustify) {
        x = originX;
        for (int i = 0; i < line.glyphs.size(); ++i) {
            renderGlyphBox(line.glyphs[i], x, baselineY);
            x += line.glyphs[i].width;
            if (i < line.glyphs.size() - 1)
                x += jp.extraPerChar * line.glyphs[i].glyphs.size();
            if (i < line.glyphs.size() - 1) {
                if (!Layout::shouldSkipJustifyGap(line.glyphs[i], line.glyphs[i + 1]))
                    x += jp.extraPerGap;
            }
        }
    } else {
        qreal xOffset = 0;
        if (line.alignment == Qt::AlignCenter)
            xOffset = (availWidth - line.width) / 2;
        else if (line.alignment == Qt::AlignRight)
            xOffset = availWidth - line.width;

        x = originX + xOffset;
        for (const auto &gbox : line.glyphs) {
            renderGlyphBox(gbox, x, baselineY);
            x += gbox.width;
        }
    }

    // Trailing soft-hyphen
    if (line.showTrailingHyphen && !line.glyphs.isEmpty()) {
        const auto &lastGbox = line.glyphs.last();
        if (lastGbox.font && !lastGbox.font->isHershey) {
            if (lastGbox.font->ftFace) {
                quint32 hyphenGid = FT_Get_Char_Index(lastGbox.font->ftFace, '-');
                if (hyphenGid > 0) {
                    GlyphRenderInfo info;
                    info.glyphIds = {hyphenGid};
                    info.positions = {QPointF(0, 0)};
                    drawGlyphs(lastGbox.font, lastGbox.fontSize, info,
                               lastGbox.style.foreground, x, baselineY);
                }
            }
        } else if (lastGbox.font && lastGbox.font->isHershey && lastGbox.font->hersheyFont) {
            HersheyFont *hFont = lastGbox.font->hersheyFont;
            const HersheyGlyph *hGlyph = hFont->glyph(U'-');
            if (hGlyph) {
                qreal scale = lastGbox.fontSize / hFont->unitsPerEm();
                qreal strokeWidth = HersheyConstants::kStrokeWidthFactor * lastGbox.fontSize
                                    * (lastGbox.font->hersheyBold ? HersheyConstants::kBoldStrokeMultiplier : 1.0);
                QTransform t;
                if (lastGbox.font->hersheyItalic)
                    t = QTransform(scale, 0, -scale * HersheyConstants::kItalicSkew, scale, x, baselineY);
                else
                    t = QTransform(scale, 0, 0, scale, x, baselineY);

                // Prepare strokes with leftBound offset and Y flip
                QVector<QVector<QPointF>> strokes;
                for (const auto &stroke : hGlyph->strokes) {
                    if (stroke.size() < 2) continue;
                    QVector<QPointF> poly;
                    for (const auto &pt : stroke)
                        poly << QPointF(pt.x() - hGlyph->leftBound, -pt.y());
                    strokes << poly;
                }
                drawHersheyStrokes(strokes, t, lastGbox.style.foreground, strokeWidth);
            }
        }
    }
}

void BoxTreeRenderer::renderGlyphBox(const Layout::GlyphBox &gbox,
                                      qreal x, qreal baselineY)
{
    if (gbox.font && gbox.font->isHershey) {
        renderHersheyGlyphBox(gbox, x, baselineY);
        return;
    }

    if (gbox.checkboxState != Layout::GlyphBox::NoCheckbox) {
        renderCheckbox(gbox, x, baselineY);
        return;
    }

    if (gbox.glyphs.isEmpty() || !gbox.font)
        return;

    drawInlineBackground(gbox, x, baselineY);

    // Build glyph positions
    GlyphRenderInfo info;
    info.glyphIds.reserve(gbox.glyphs.size());
    info.positions.reserve(gbox.glyphs.size());

    qreal curX = 0;
    for (const auto &g : gbox.glyphs) {
        info.glyphIds.append(g.glyphId);
        qreal gx = curX + g.xOffset;
        qreal gy = -g.yOffset; // layout Y is top-down
        if (gbox.style.superscript)
            gy -= gbox.fontSize * RenderConstants::kSuperscriptRise;
        else if (gbox.style.subscript)
            gy += gbox.fontSize * RenderConstants::kSubscriptDrop;
        info.positions.append(QPointF(gx, gy));
        curX += g.xAdvance;
    }

    drawGlyphs(gbox.font, gbox.fontSize, info, gbox.style.foreground, x, baselineY);

    renderGlyphDecorations(gbox, x, baselineY, x + gbox.width);
}

void BoxTreeRenderer::renderHersheyGlyphBox(const Layout::GlyphBox &gbox,
                                              qreal x, qreal baselineY)
{
    if (gbox.glyphs.isEmpty() || !gbox.font || !gbox.font->hersheyFont)
        return;

    HersheyFont *hFont = gbox.font->hersheyFont;
    qreal fontSize = gbox.fontSize;
    qreal scale = fontSize / hFont->unitsPerEm();

    drawInlineBackground(gbox, x, baselineY);

    qreal strokeWidth = HersheyConstants::kStrokeWidthFactor * fontSize;
    if (gbox.font->hersheyBold)
        strokeWidth *= HersheyConstants::kBoldStrokeMultiplier;

    qreal curX = x;
    for (const auto &g : gbox.glyphs) {
        const HersheyGlyph *hGlyph = hFont->glyph(static_cast<char32_t>(g.glyphId));
        if (!hGlyph) {
            curX += g.xAdvance;
            continue;
        }

        qreal gx = curX + g.xOffset;
        qreal gy = baselineY - g.yOffset;
        if (gbox.style.superscript)
            gy -= fontSize * RenderConstants::kSuperscriptRise;
        else if (gbox.style.subscript)
            gy += fontSize * RenderConstants::kSubscriptDrop;

        QTransform t;
        if (gbox.font->hersheyItalic)
            t = QTransform(scale, 0, -scale * HersheyConstants::kItalicSkew, scale, gx, gy);
        else
            t = QTransform(scale, 0, 0, scale, gx, gy);

        // Prepare strokes with leftBound offset and Y flip
        QVector<QVector<QPointF>> strokes;
        for (const auto &stroke : hGlyph->strokes) {
            if (stroke.size() < 2) continue;
            QVector<QPointF> poly;
            for (const auto &pt : stroke)
                poly << QPointF(pt.x() - hGlyph->leftBound, -pt.y());
            strokes << poly;
        }

        drawHersheyStrokes(strokes, t, gbox.style.foreground, strokeWidth);

        curX += g.xAdvance;
    }

    renderGlyphDecorations(gbox, x, baselineY, curX);
}

void BoxTreeRenderer::renderGlyphDecorations(const Layout::GlyphBox &gbox,
                                              qreal x, qreal baselineY, qreal endX)
{
    if (gbox.style.underline) {
        qreal uy = baselineY + gbox.descent * 0.3;
        drawLine(QPointF(x, uy), QPointF(endX, uy),
                 gbox.style.foreground, 0.5);
    }

    if (gbox.style.strikethrough) {
        qreal sy = baselineY - gbox.ascent * 0.3;
        drawLine(QPointF(x, sy), QPointF(endX, sy),
                 gbox.style.foreground, 0.5);
    }

    if (!gbox.style.linkHref.isEmpty()) {
        collectLink(QRectF(x, baselineY - gbox.ascent,
                            endX - x, gbox.ascent + gbox.descent),
                    gbox.style.linkHref, gbox.style.linkTitle);
    }
}

void BoxTreeRenderer::renderCheckbox(const Layout::GlyphBox &gbox,
                                      qreal x, qreal baselineY)
{
    qreal size = gbox.fontSize * 0.7;
    qreal r = size * 0.12;
    qreal lw = size * 0.07;
    qreal cx = x + 1.0;
    qreal cy = baselineY - size * 0.75;

    QRectF boxRect(cx, cy, size, size);
    QColor strokeColor = gbox.style.foreground.isValid()
                             ? gbox.style.foreground : QColor(0x33, 0x33, 0x33);

    if (gbox.checkboxState == Layout::GlyphBox::Checked) {
        drawRoundedRect(boxRect, r, r, QColor(235, 242, 255), strokeColor, lw);

        QPolygonF check;
        check << QPointF(cx + size * 0.20, cy + size * 0.50)
              << QPointF(cx + size * 0.42, cy + size * 0.75)
              << QPointF(cx + size * 0.82, cy + size * 0.22);
        drawCheckmark(check, strokeColor, lw * 1.5);
    } else {
        drawRoundedRect(boxRect, r, r, QColor(), strokeColor, lw);
    }
}

void BoxTreeRenderer::renderImageBlock(const Layout::BlockBox &box)
{
    if (!box.imageTitle.isEmpty() && box.imageWidth > 0 && box.imageHeight > 0)
        collectImageHint(QRectF(box.x, box.y, box.imageWidth, box.imageHeight), box.imageTitle);

    if (box.image.isNull())
        return;

    QRectF imgRect(box.x, box.y, box.imageWidth, box.imageHeight);
    drawImage(imgRect, box.image);
}

// --- Inline background helper ---

void BoxTreeRenderer::drawInlineBackground(const Layout::GlyphBox &gbox,
                                            qreal x, qreal baselineY)
{
    if (gbox.style.background.isValid()) {
        drawRect(QRectF(x - 1, baselineY - gbox.ascent - 1,
                         gbox.width + 2, gbox.ascent + gbox.descent + 2),
                 gbox.style.background);
    }
}

// --- Shared justification helpers ---

JustifyParams BoxTreeRenderer::computeJustification(const Layout::LineBox &line,
                                                    qreal availWidth,
                                                    qreal maxJustifyGap) const
{
    JustifyParams result;

    if (line.alignment != Qt::AlignJustify || line.isLastLine
        || line.glyphs.size() <= 1 || line.width >= availWidth)
        return result;

    if (line.justify.wordGapCount > 0) {
        // New path: use pre-computed JustifyInfo from the layout engine
        result.doJustify = true;
        result.extraPerGap = line.justify.extraWordSpacing;
        result.extraPerChar = line.justify.extraLetterSpacing;
        return result;
    }

    // Legacy fallback: compute gaps inline
    int gapCount = 0;
    for (int i = 1; i < line.glyphs.size(); ++i) {
        if (!Layout::shouldSkipJustifyGap(line.glyphs[i - 1], line.glyphs[i]))
            gapCount++;
    }

    if (gapCount > 0) {
        qreal extraSpace = availWidth - line.width;
        qreal epg = extraSpace / gapCount;
        if (epg <= maxJustifyGap) {
            result.doJustify = true;
            result.extraPerGap = epg;
        }
    }

    return result;
}

QList<qreal> BoxTreeRenderer::computeGlyphXPositions(const Layout::LineBox &line,
                                                     qreal originX,
                                                     qreal availWidth) const
{
    JustifyParams jp = computeJustification(line, availWidth);
    QList<qreal> positions;
    positions.reserve(line.glyphs.size());

    if (jp.doJustify) {
        qreal cx = originX;
        for (int i = 0; i < line.glyphs.size(); ++i) {
            positions.append(cx);
            cx += line.glyphs[i].width;
            if (i < line.glyphs.size() - 1)
                cx += jp.extraPerChar * line.glyphs[i].glyphs.size();
            if (i < line.glyphs.size() - 1) {
                if (!Layout::shouldSkipJustifyGap(line.glyphs[i], line.glyphs[i + 1]))
                    cx += jp.extraPerGap;
            }
        }
    } else {
        qreal xOffset = 0;
        if (line.alignment == Qt::AlignCenter)
            xOffset = (availWidth - line.width) / 2;
        else if (line.alignment == Qt::AlignRight)
            xOffset = availWidth - line.width;
        qreal cx = originX + xOffset;
        for (int i = 0; i < line.glyphs.size(); ++i) {
            positions.append(cx);
            cx += line.glyphs[i].width;
        }
    }

    return positions;
}
