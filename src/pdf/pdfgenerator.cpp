/*
 * pdfgenerator.cpp — Box tree → PDF content streams + font embedding
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pdfgenerator.h"
#include "pdfboxrenderer.h"
#include "fontmanager.h"
#include "sfnt.h"
#include "headerfooterrenderer.h"

#include <QColor>
#include <QDateTime>
#include <zlib.h>
#include FT_OUTLINE_H

PdfGenerator::PdfGenerator(FontManager *fontManager)
    : m_fontManager(fontManager)
{
}

void PdfGenerator::setDocumentInfo(const QString &filename, const QString &title)
{
    m_filename = filename;
    m_title = title;
}

// --- PDF coordinate helpers ---

QByteArray PdfGenerator::pdfCoord(qreal v)
{
    return QByteArray::number(v, 'f', 2);
}

QByteArray PdfGenerator::colorOperator(const QColor &color, bool fill)
{
    if (!color.isValid())
        return {};
    qreal r = color.redF();
    qreal g = color.greenF();
    qreal b = color.blueF();
    QByteArray op = fill ? " rg\n" : " RG\n";
    return pdfCoord(r) + " " + pdfCoord(g) + " " + pdfCoord(b) + op;
}

// --- Font registration ---

QByteArray PdfGenerator::pdfFontName(FontFace *face)
{
    int idx = ensureFontRegistered(face);
    return "F" + QByteArray::number(idx);
}

int PdfGenerator::ensureFontRegistered(FontFace *face)
{
    if (m_fontIndex.contains(face))
        return m_fontIndex[face];

    int idx = m_embeddedFonts.size();
    EmbeddedFont ef;
    ef.pdfName = "F" + QByteArray::number(idx);
    ef.face = face;
    m_embeddedFonts.append(ef);
    m_fontIndex[face] = idx;
    return idx;
}

// --- Main generate ---

QByteArray PdfGenerator::generate(const Layout::LayoutResult &layout,
                                   const PageLayout &pageLayout,
                                   const QString &title)
{
    m_embeddedFonts.clear();
    m_fontIndex.clear();
    m_embeddedImages.clear();
    m_imageIndex.clear();
    m_pageAnnotations.clear();
    m_glyphForms.clear();
    m_nextGlyphFormIdx = 0;
    if (m_title.isEmpty())
        m_title = title;

    QByteArray output;
    Pdf::Writer writer;
    if (!writer.openBuffer(&output))
        return {};

    writer.writeHeader();
    m_writer = &writer;

    int totalPages = layout.pages.size();

    // Detect Hershey mode
    m_hasHersheyGlyphs = false;
    for (const auto &page : layout.pages) {
        for (const auto &elem : page.elements) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Layout::BlockBox>) {
                    for (const auto &line : e.lines)
                        for (const auto &gbox : line.glyphs)
                            if (gbox.font && gbox.font->isHershey)
                                m_hasHersheyGlyphs = true;
                } else if constexpr (std::is_same_v<T, Layout::TableBox>) {
                    for (const auto &row : e.rows)
                        for (const auto &cell : row.cells)
                            for (const auto &line : cell.lines)
                                for (const auto &gbox : line.glyphs)
                                    if (gbox.font && gbox.font->isHershey)
                                        m_hasHersheyGlyphs = true;
                } else if constexpr (std::is_same_v<T, Layout::FootnoteSectionBox>) {
                    for (const auto &fn : e.footnotes)
                        for (const auto &line : fn.lines)
                            for (const auto &gbox : line.glyphs)
                                if (gbox.font && gbox.font->isHershey)
                                    m_hasHersheyGlyphs = true;
                }
            }, elem);
            if (m_hasHersheyGlyphs) break;
        }
        if (m_hasHersheyGlyphs) break;
    }

    // Force Hershey mode if requested by export options
    if (m_exportOptions.useHersheyFonts)
        m_hasHersheyGlyphs = true;

    // First pass: register fonts by scanning all glyph boxes
    for (const auto &page : layout.pages) {
        for (const auto &elem : page.elements) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Layout::BlockBox>) {
                    for (const auto &line : e.lines)
                        for (const auto &gbox : line.glyphs)
                            if (gbox.font && !gbox.font->isHershey)
                                ensureFontRegistered(gbox.font);
                } else if constexpr (std::is_same_v<T, Layout::TableBox>) {
                    for (const auto &row : e.rows)
                        for (const auto &cell : row.cells)
                            for (const auto &line : cell.lines)
                                for (const auto &gbox : line.glyphs)
                                    if (gbox.font && !gbox.font->isHershey)
                                        ensureFontRegistered(gbox.font);
                } else if constexpr (std::is_same_v<T, Layout::FootnoteSectionBox>) {
                    for (const auto &fn : e.footnotes)
                        for (const auto &line : fn.lines)
                            for (const auto &gbox : line.glyphs)
                                if (gbox.font && !gbox.font->isHershey)
                                    ensureFontRegistered(gbox.font);
                }
            }, elem);
        }
    }

    // Also scan for images
    for (const auto &page : layout.pages) {
        for (const auto &elem : page.elements) {
            if (auto *bb = std::get_if<Layout::BlockBox>(&elem)) {
                if (bb->type == Layout::BlockBox::ImageBlock && !bb->image.isNull())
                    ensureImageRegistered(bb->imageId, bb->image);
            }
        }
    }

    // Also register header/footer font and pre-mark glyphs for subsetting
    FontFace *hfFont = m_fontManager->loadFont(QStringLiteral("Noto Sans"), 400, false);
    if (hfFont) {
        ensureFontRegistered(hfFont);

        // Pre-scan all pages' header/footer text so glyphs are included
        // in the font subset (embedding happens before page rendering).
        auto markGlyphs = [&](const QString &text, int pageNum) {
            QString resolved = HeaderFooterRenderer::resolveField(
                text, {pageNum, totalPages, m_filename, m_title});
            for (QChar ch : resolved) {
                FT_UInt gid = FT_Get_Char_Index(hfFont->ftFace, ch.unicode());
                m_fontManager->markGlyphUsed(hfFont, gid);
            }
        };

        for (int pi = 0; pi < layout.pages.size(); ++pi) {
            PageLayout resolved = pageLayout.resolvedForPage(pi);
            if (resolved.headerEnabled) {
                markGlyphs(resolved.headerLeft, pi);
                markGlyphs(resolved.headerCenter, pi);
                markGlyphs(resolved.headerRight, pi);
            }
            if (resolved.footerEnabled) {
                markGlyphs(resolved.footerLeft, pi);
                markGlyphs(resolved.footerCenter, pi);
                markGlyphs(resolved.footerRight, pi);
            }
        }
    }

    // Embed fonts
    if (!m_exportOptions.xobjectGlyphs)
        embedFonts(writer);

    // Embed images
    embedImages(writer);

    // Build resource dictionary
    Pdf::ResourceDict resources;
    if (!m_exportOptions.xobjectGlyphs) {
        for (auto &ef : m_embeddedFonts) {
            if (ef.fontObjId)
                resources.fonts[ef.pdfName] = ef.fontObjId;
        }
    }
    for (auto &ei : m_embeddedImages)
        resources.xObjects[ei.pdfName] = ei.objId;

    // Base 14 Helvetica for markdown invisible text in Hershey mode
    if ((m_hasHersheyGlyphs || m_exportOptions.xobjectGlyphs) && m_exportOptions.markdownCopy) {
        Pdf::ObjId hvInvObj = writer.startObj();
        writer.write("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        writer.endObj(hvInvObj);
        resources.fonts["HvInv"] = hvInvObj;
    }
    m_resources = &resources;

    // Initialize per-page annotation lists
    m_pageAnnotations.resize(layout.pages.size());

    // Write pages
    QList<Pdf::ObjId> pageObjIds;
    for (int pi = 0; pi < layout.pages.size(); ++pi) {
        m_currentPageIndex = pi;
        QByteArray contentStream = renderPage(layout.pages[pi], pageLayout, resources);

        // Content stream object
        Pdf::ObjId contentObj = writer.startObj();
        writer.write("<<\n");
        writer.endObjectWithStream(contentObj, contentStream);

        // Write link annotation objects for this page
        QList<Pdf::ObjId> annotObjIds;
        for (const auto &annot : m_pageAnnotations[pi]) {
            Pdf::ObjId annotObj = writer.startObj();
            writer.write("<<\n/Type /Annot\n/Subtype /Link\n");
            writer.write("/Rect ["
                         + pdfCoord(annot.rect.left()) + " "
                         + pdfCoord(annot.rect.bottom()) + " "
                         + pdfCoord(annot.rect.right()) + " "
                         + pdfCoord(annot.rect.top()) + "]\n");
            writer.write("/Border [0 0 0]\n"); // no visible border
            writer.write("/A <</Type /Action /S /URI /URI "
                         + Pdf::toLiteralString(annot.href.toUtf8()) + ">>\n");
            writer.write(">>");
            writer.endObj(annotObj);
            annotObjIds.append(annotObj);
        }

        // Page object
        Pdf::ObjId pageObj = writer.startObj();
        writer.write("<<\n");
        writer.write("/Type /Page\n");
        writer.write("/Parent " + Pdf::toObjRef(writer.pagesObj()) + "\n");
        writer.write("/MediaBox [0 0 "
                     + Pdf::toPdf(layout.pageSize.width()) + " "
                     + Pdf::toPdf(layout.pageSize.height()) + "]\n");
        writer.write("/Contents " + Pdf::toObjRef(contentObj) + "\n");
        writer.write("/Resources ");
        writer.writeResourceDict(resources);
        if (!annotObjIds.isEmpty()) {
            writer.write("/Annots [");
            for (auto id : annotObjIds)
                writer.write(Pdf::toObjRef(id) + " ");
            writer.write("]\n");
        }
        writer.write(">>");
        writer.endObj(pageObj);
        pageObjIds.append(pageObj);
    }

    // Pages object
    writer.startObj(writer.pagesObj());
    writer.write("<<\n/Type /Pages\n/Kids [");
    for (auto id : pageObjIds)
        writer.write(Pdf::toObjRef(id) + " ");
    writer.write("]\n/Count " + Pdf::toPdf(pageObjIds.size()) + "\n>>");
    writer.endObj(writer.pagesObj());

    // PDF Bookmarks / Outline tree
    Pdf::ObjId outlineObj = writeOutlines(writer, pageObjIds, layout, pageLayout);

    // Info object
    writer.startObj(writer.infoObj());
    writer.write("<<\n");
    writer.write("/Producer " + Pdf::toLiteralString(QStringLiteral("Penelope")) + "\n");
    QString infoTitle = m_exportOptions.title.isEmpty() ? m_title : m_exportOptions.title;
    if (!infoTitle.isEmpty())
        writer.write("/Title " + Pdf::toLiteralString(Pdf::toUTF16(infoTitle)) + "\n");
    if (!m_exportOptions.author.isEmpty())
        writer.write("/Author " + Pdf::toLiteralString(Pdf::toUTF16(m_exportOptions.author)) + "\n");
    if (!m_exportOptions.subject.isEmpty())
        writer.write("/Subject " + Pdf::toLiteralString(Pdf::toUTF16(m_exportOptions.subject)) + "\n");
    if (!m_exportOptions.keywords.isEmpty())
        writer.write("/Keywords " + Pdf::toLiteralString(Pdf::toUTF16(m_exportOptions.keywords)) + "\n");
    writer.write("/CreationDate " + Pdf::toDateString(QDateTime::currentDateTime()) + "\n");
    writer.write(">>");
    writer.endObj(writer.infoObj());

    // Catalog object
    writer.startObj(writer.catalogObj());
    writer.write("<<\n/Type /Catalog\n/Pages " + Pdf::toObjRef(writer.pagesObj()) + "\n");
    if (outlineObj && m_exportOptions.includeBookmarks)
        writer.write("/Outlines " + Pdf::toObjRef(outlineObj) + "\n");
    // PageMode
    switch (m_exportOptions.initialView) {
    case PdfExportOptions::ShowBookmarks:
        if (outlineObj && m_exportOptions.includeBookmarks)
            writer.write("/PageMode /UseOutlines\n");
        break;
    case PdfExportOptions::ShowThumbnails:
        writer.write("/PageMode /UseThumbs\n");
        break;
    case PdfExportOptions::ViewerDefault:
    default:
        break;
    }
    // PageLayout
    switch (m_exportOptions.pageLayout) {
    case PdfExportOptions::SinglePage:
        writer.write("/PageLayout /SinglePage\n");
        break;
    case PdfExportOptions::Continuous:
        writer.write("/PageLayout /OneColumn\n");
        break;
    case PdfExportOptions::FacingPages:
        writer.write("/PageLayout /TwoColumnLeft\n");
        break;
    case PdfExportOptions::FacingPagesFirstAlone:
        writer.write("/PageLayout /TwoColumnRight\n");
        break;
    }
    writer.write(">>");
    writer.endObj(writer.catalogObj());

    // XRef and trailer
    writer.writeXrefAndTrailer();
    writer.close();

    m_writer = nullptr;
    m_resources = nullptr;
    return output;
}

bool PdfGenerator::generateToFile(const Layout::LayoutResult &layout,
                                   const PageLayout &pageLayout,
                                   const QString &title,
                                   const QString &filePath)
{
    QByteArray data = generate(layout, pageLayout, title);
    if (data.isEmpty())
        return false;
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return true;
}


// --- Page rendering ---

QByteArray PdfGenerator::renderPage(const Layout::Page &page,
                                     const PageLayout &pageLayout,
                                     const Pdf::ResourceDict &resources)
{
    Q_UNUSED(resources);
    QByteArray stream;

    QSizeF pageSize = QPageSize(pageLayout.pageSizeId).sizePoints();
    qreal pageWidth = pageSize.width();
    qreal pageHeight = pageSize.height();

    // Page background fill (theme color)
    if (pageLayout.pageBackground.isValid()
        && pageLayout.pageBackground != Qt::white) {
        stream += colorOperator(pageLayout.pageBackground, true);
        stream += "0 0 " + pdfCoord(pageWidth) + " " + pdfCoord(pageHeight) + " re f\n";
    }

    // Calculate content area
    qreal marginLeft = pageLayout.margins.left() * 72.0 / 25.4;
    qreal marginTop = pageLayout.margins.top() * 72.0 / 25.4;

    qreal contentTopY = pageHeight - marginTop;
    if (pageLayout.headerEnabled)
        contentTopY -= (PageLayout::kHeaderHeight + PageLayout::kSeparatorGap);

    qreal originX = marginLeft;

    // Render header/footer
    renderHeaderFooter(stream, pageLayout, page.pageNumber,
                       0 /* filled later */, pageWidth, pageHeight);

    // Set up PdfBoxRenderer to render page elements
    PdfBoxRenderer renderer(m_fontManager);
    renderer.setStream(&stream);
    renderer.setContentOrigin(originX, contentTopY);
    renderer.setMaxJustifyGap(m_maxJustifyGap);
    renderer.setExportOptions(m_exportOptions);
    renderer.setHasHersheyGlyphs(m_hasHersheyGlyphs);

    // Wire up callbacks for font/glyph management
    renderer.setPdfFontNameCallback([this](FontFace *f) {
        return pdfFontName(f);
    });
    renderer.setGlyphFormCallback([this](const HersheyFont *hf, FontFace *face, uint gid, bool bold) {
        auto entry = ensureGlyphForm(hf, face, gid, bold);
        return ::GlyphFormEntry{entry.objId, entry.pdfName, entry.advanceWidth};
    });
    renderer.setMarkGlyphUsedCallback([this](FontFace *f, uint g) {
        m_fontManager->markGlyphUsed(f, g);
    });
    renderer.setImageNameCallback([this](const QString &imageId) -> QByteArray {
        auto it = m_imageIndex.find(imageId);
        if (it != m_imageIndex.end())
            return m_embeddedImages[it.value()].pdfName;
        return QByteArray();
    });

    // Bridge embedded fonts list for ActualText invisible text.
    // PdfBoxRenderer's EmbeddedFont and PdfGenerator's EmbeddedFont are
    // structurally identical (binary-compatible), so reinterpret_cast is safe.
    static_assert(sizeof(EmbeddedFont) == sizeof(::EmbeddedFont),
                  "EmbeddedFont layout mismatch");
    renderer.setEmbeddedFontsRef(
        reinterpret_cast<const QList<::EmbeddedFont> *>(&m_embeddedFonts));

    // Translate to content origin: the layout engine produces x-coordinates
    // relative to the content area (starting at 0). The CTM translation
    // offsets all rendering by the left margin. Y-flip is handled per-primitive
    // by PdfBoxRenderer::pdfY().
    stream += "q\n1 0 0 1 " + pdfCoord(originX) + " 0 cm\n";

    // Render all page elements through PdfBoxRenderer
    for (const auto &elem : page.elements)
        renderer.renderElement(elem);

    stream += "Q\n";

    // Collect link annotations from renderer
    for (const auto &link : renderer.linkAnnotations())
        m_pageAnnotations[m_currentPageIndex].append(link);

    return stream;
}

// --- Glyph Form XObjects ---

// FreeType outline decomposition callbacks needed by ensureGlyphForm().
// (Duplicated in pdfboxrenderer.cpp for per-glyph rendering; kept here
//  because ensureGlyphForm writes Form XObject streams directly.)

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

PdfGenerator::GlyphFormEntry PdfGenerator::ensureGlyphForm(
    const HersheyFont *hersheyFont, FontFace *ttfFace,
    uint glyphId, bool bold)
{
    // Preconditions: must be called during generate() with valid writer/resources
    if ((!hersheyFont && !ttfFace) || !m_writer || !m_resources)
        return {};

    GlyphFormKey key{hersheyFont, ttfFace, glyphId, bold};
    auto it = m_glyphForms.find(key);
    if (it != m_glyphForms.end())
        return it.value();

    QByteArray formStream;
    qreal advW = 0;
    qreal bboxBottom = 0;
    qreal bboxTop = 0;

    if (hersheyFont) {
        const HersheyGlyph *hGlyph = hersheyFont->glyph(static_cast<char32_t>(glyphId));
        if (!hGlyph) {
            GlyphFormEntry dummy;
            return dummy;
        }

        // Build the Form stream in glyph-local coordinates.
        // Origin: left baseline (x=0 at leftBound, y=0 at baseline).
        // Coordinates are in Hershey font units (scaled at call site via cm).
        formStream += "1 J 1 j\n"; // round cap & join

        // Stroke width in glyph units. Call site scales by fontSize/unitsPerEm,
        // so: strokeWidth_glyphUnits * (fontSize/upm) = 0.02 * fontSize
        // Therefore: strokeWidth_glyphUnits = 0.02 * upm
        qreal strokeWidth = HersheyConstants::kStrokeWidthFactor * hersheyFont->unitsPerEm();
        if (bold)
            strokeWidth *= HersheyConstants::kBoldStrokeMultiplier;
        formStream += pdfCoord(strokeWidth) + " w\n";

        for (const auto &stroke : hGlyph->strokes) {
            if (stroke.size() < 2)
                continue;
            qreal px = stroke[0].x() - hGlyph->leftBound;
            qreal py = stroke[0].y();
            formStream += pdfCoord(px) + " " + pdfCoord(py) + " m\n";
            for (int si = 1; si < stroke.size(); ++si) {
                px = stroke[si].x() - hGlyph->leftBound;
                py = stroke[si].y();
                formStream += pdfCoord(px) + " " + pdfCoord(py) + " l\n";
            }
            formStream += "S\n";
        }

        advW = hGlyph->rightBound - hGlyph->leftBound;
        bboxBottom = -hersheyFont->descent();
        bboxTop = hersheyFont->ascent();

    } else if (ttfFace && ttfFace->ftFace) {
        FT_Face face = ttfFace->ftFace;
        if (FT_Load_Glyph(face, glyphId, FT_LOAD_NO_SCALE) != 0
            || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            return {};
        }

        FT_Outline_Funcs funcs = {};
        funcs.move_to = outlineMoveTo;
        funcs.line_to = outlineLineTo;
        funcs.conic_to = outlineConicTo;
        funcs.cubic_to = outlineCubicTo;

        OutlineCtx ctx;
        ctx.stream = &formStream;
        ctx.scale = 1.0;  // font units, scaling at call site via cm
        ctx.tx = 0;
        ctx.ty = 0;
        ctx.last = {0, 0};
        FT_Outline_Decompose(&face->glyph->outline, &funcs, &ctx);
        formStream += "f\n";

        advW = face->glyph->metrics.horiAdvance;
        bboxBottom = face->glyph->metrics.horiBearingY - face->glyph->metrics.height;
        bboxTop = face->glyph->metrics.horiBearingY;

    } else {
        return {};
    }

    // Write the Form XObject to PDF
    Pdf::ObjId objId = m_writer->startObj();
    m_writer->write("<<\n/Type /XObject\n/Subtype /Form\n");
    m_writer->write("/BBox [0 " + pdfCoord(bboxBottom) + " "
                    + pdfCoord(advW) + " " + pdfCoord(bboxTop) + "]\n");
    m_writer->endObjectWithStream(objId, formStream);

    // Register in resource dict and cache
    GlyphFormEntry entry;
    entry.objId = objId;
    entry.pdfName = "HG" + QByteArray::number(m_nextGlyphFormIdx++);
    entry.advanceWidth = advW;
    m_glyphForms.insert(key, entry);

    m_resources->xObjects[entry.pdfName] = objId;

    return entry;
}

// --- Header/Footer rendering ---

void PdfGenerator::renderHeaderFooter(QByteArray &stream, const PageLayout &pageLayout,
                                       int pageNumber, int totalPages,
                                       qreal pageWidth, qreal pageHeight)
{
    PageLayout resolved = pageLayout.resolvedForPage(pageNumber);

    auto resolveField = [&](const QString &field) -> QString {
        return HeaderFooterRenderer::resolveField(field, {pageNumber, totalPages,
                                                          m_filename, m_title});
    };

    auto drawFields = [&](const QString &left, const QString &center, const QString &right,
                          qreal rectY, qreal rectHeight) {
        FontFace *font = m_fontManager->loadFont(QStringLiteral("Noto Sans"), 400, false);
        if (!font || !font->ftFace) return;

        QByteArray fname = pdfFontName(font);
        qreal fontSize = 9.0;
        qreal marginLeft = pageLayout.margins.left() * 72.0 / 25.4;
        qreal marginRight = pageLayout.margins.right() * 72.0 / 25.4;
        qreal textY = rectY;

        stream += "BT\n";
        stream += "/" + fname + " " + pdfCoord(fontSize) + " Tf\n";
        stream += "0.53 0.53 0.53 rg\n"; // #888888

        // Helper: measure text width and collect glyph IDs
        auto measureText = [&](const QString &text) -> qreal {
            qreal w = 0;
            for (QChar ch : text) {
                FT_UInt gid = FT_Get_Char_Index(font->ftFace, ch.unicode());
                w += m_fontManager->glyphWidth(font, gid, fontSize);
            }
            return w;
        };

        // Helper: emit glyph string (marks glyphs used for subsetting)
        auto emitText = [&](const QString &text) {
            for (QChar ch : text) {
                FT_UInt gid = FT_Get_Char_Index(font->ftFace, ch.unicode());
                m_fontManager->markGlyphUsed(font, gid);
                stream += Pdf::toHexString16(static_cast<quint16>(gid)) + " Tj\n";
            }
        };

        // Left field
        QString leftText = resolveField(left);
        if (!leftText.isEmpty()) {
            stream += "1 0 0 1 " + pdfCoord(marginLeft) + " " + pdfCoord(textY) + " Tm\n";
            emitText(leftText);
        }

        // Center field
        QString centerText = resolveField(center);
        if (!centerText.isEmpty()) {
            qreal textWidth = measureText(centerText);
            qreal centerX = (pageWidth - textWidth) / 2;
            stream += "1 0 0 1 " + pdfCoord(centerX) + " " + pdfCoord(textY) + " Tm\n";
            emitText(centerText);
        }

        // Right field
        QString rightText = resolveField(right);
        if (!rightText.isEmpty()) {
            qreal textWidth = measureText(rightText);
            qreal rightX = pageWidth - marginRight - textWidth;
            stream += "1 0 0 1 " + pdfCoord(rightX) + " " + pdfCoord(textY) + " Tm\n";
            emitText(rightText);
        }

        stream += "ET\n";
    };

    qreal mTop = pageLayout.margins.top() * 72.0 / 25.4;
    qreal mBottom = pageLayout.margins.bottom() * 72.0 / 25.4;

    // Header
    if (resolved.headerEnabled) {
        qreal headerY = pageHeight - mTop + PageLayout::kSeparatorGap;
        drawFields(resolved.headerLeft, resolved.headerCenter, resolved.headerRight,
                   headerY, PageLayout::kHeaderHeight);

        // Separator line
        qreal sepY = pageHeight - mTop;
        qreal mLeft = pageLayout.margins.left() * 72.0 / 25.4;
        qreal mRight = pageLayout.margins.right() * 72.0 / 25.4;
        stream += "q\n0.80 0.80 0.80 RG\n0.5 w\n";
        stream += pdfCoord(mLeft) + " " + pdfCoord(sepY) + " m "
                + pdfCoord(pageWidth - mRight) + " " + pdfCoord(sepY) + " l S\n";
        stream += "Q\n";
    }

    // Footer
    if (resolved.footerEnabled) {
        qreal footerY = mBottom - PageLayout::kSeparatorGap;
        drawFields(resolved.footerLeft, resolved.footerCenter, resolved.footerRight,
                   footerY, PageLayout::kFooterHeight);

        // Separator line
        qreal sepY = mBottom + PageLayout::kFooterHeight;
        qreal mLeft = pageLayout.margins.left() * 72.0 / 25.4;
        qreal mRight = pageLayout.margins.right() * 72.0 / 25.4;
        stream += "q\n0.80 0.80 0.80 RG\n0.5 w\n";
        stream += pdfCoord(mLeft) + " " + pdfCoord(sepY) + " m "
                + pdfCoord(pageWidth - mRight) + " " + pdfCoord(sepY) + " l S\n";
        stream += "Q\n";
    }
}

// --- Image registration and embedding ---

int PdfGenerator::ensureImageRegistered(const QString &imageId, const QImage &image)
{
    if (m_imageIndex.contains(imageId))
        return m_imageIndex[imageId];

    int idx = m_embeddedImages.size();
    EmbeddedImage ei;
    ei.pdfName = "Im" + QByteArray::number(idx);
    ei.image = image;
    ei.width = image.width();
    ei.height = image.height();
    m_embeddedImages.append(ei);
    m_imageIndex[imageId] = idx;
    return idx;
}

void PdfGenerator::embedImages(Pdf::Writer &writer)
{
    for (auto &ei : m_embeddedImages) {
        // Convert image to raw RGB bytes
        QImage rgb = ei.image.convertToFormat(QImage::Format_RGB888);
        QByteArray rawData;
        rawData.reserve(rgb.width() * rgb.height() * 3);
        for (int y = 0; y < rgb.height(); ++y) {
            const uchar *line = rgb.constScanLine(y);
            rawData.append(reinterpret_cast<const char *>(line), rgb.width() * 3);
        }

        Pdf::ObjId imgObj = writer.startObj();
        writer.write("<<\n/Type /XObject\n/Subtype /Image\n");
        writer.write("/Width " + Pdf::toPdf(ei.width) + "\n");
        writer.write("/Height " + Pdf::toPdf(ei.height) + "\n");
        writer.write("/ColorSpace /DeviceRGB\n");
        writer.write("/BitsPerComponent 8\n");
        writer.endObjectWithStream(imgObj, rawData);
        ei.objId = imgObj;
    }
}

// --- PDF Outline / Bookmarks ---

Pdf::ObjId PdfGenerator::writeOutlines(Pdf::Writer &writer,
                                        const QList<Pdf::ObjId> &pageObjIds,
                                        const Layout::LayoutResult &layout,
                                        const PageLayout &pageLayout)
{
    if (!m_exportOptions.includeBookmarks)
        return 0;

    // 1. Collect headings from layout pages
    QList<OutlineEntry> entries;

    QSizeF pageSize = QPageSize(pageLayout.pageSizeId).sizePoints();
    qreal pageHeight = pageSize.height();
    qreal marginTop = pageLayout.margins.top() * 72.0 / 25.4;
    qreal contentTop = pageHeight - marginTop;
    if (pageLayout.headerEnabled)
        contentTop -= (PageLayout::kHeaderHeight + PageLayout::kSeparatorGap);

    for (int pi = 0; pi < layout.pages.size(); ++pi) {
        for (const auto &elem : layout.pages[pi].elements) {
            if (auto *bb = std::get_if<Layout::BlockBox>(&elem)) {
                if (bb->headingLevel > 0 && bb->headingLevel <= m_exportOptions.bookmarkMaxDepth
                    && !bb->headingText.isEmpty()) {
                    OutlineEntry entry;
                    entry.title = bb->headingText;
                    entry.level = bb->headingLevel;
                    entry.pageIndex = pi;
                    // PDF y: position at top of heading with some breathing room
                    entry.destY = contentTop - bb->y + bb->spaceBefore;
                    entries.append(entry);
                }
            }
        }
    }

    if (entries.isEmpty())
        return 0;

    // 2. Build tree structure using stack-based parent finding
    //    (same algorithm as TocWidget)
    // parents[level] = index of last entry at that heading level
    int parents[7] = {-1, -1, -1, -1, -1, -1, -1};
    // parentOf[i] = index of parent entry (-1 = top-level, parented to root)
    QList<int> parentOf(entries.size(), -1);

    for (int i = 0; i < entries.size(); ++i) {
        int level = entries[i].level;

        // Find nearest ancestor with lower level
        int parentIdx = -1;
        for (int l = level - 1; l >= 1; --l) {
            if (parents[l] >= 0) {
                parentIdx = parents[l];
                break;
            }
        }
        parentOf[i] = parentIdx;

        if (parentIdx >= 0) {
            entries[parentIdx].childIndices.append(i);
        }

        parents[level] = i;
        // Clear deeper levels
        for (int l = level + 1; l <= 6; ++l)
            parents[l] = -1;
    }

    // 3. Reserve object IDs: one for root + one per entry
    Pdf::ObjId rootObjId = writer.newObject();
    for (auto &entry : entries)
        entry.objId = writer.newObject();

    // 4. Collect top-level entries (parentOf == -1)
    QList<int> topLevel;
    for (int i = 0; i < entries.size(); ++i) {
        if (parentOf[i] < 0)
            topLevel.append(i);
    }

    // 5. Helper: count all descendants recursively
    std::function<int(int)> countDescendants = [&](int idx) -> int {
        int count = 0;
        for (int childIdx : entries[idx].childIndices) {
            count += 1 + countDescendants(childIdx);
        }
        return count;
    };

    // 6. Write each outline entry
    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        Pdf::ObjId parentObj = (parentOf[i] >= 0) ? entries[parentOf[i]].objId : rootObjId;

        // Find prev/next siblings within same parent
        const QList<int> &siblings = (parentOf[i] >= 0)
            ? entries[parentOf[i]].childIndices
            : topLevel;

        int siblingPos = siblings.indexOf(i);
        int prevIdx = (siblingPos > 0) ? siblings[siblingPos - 1] : -1;
        int nextIdx = (siblingPos < siblings.size() - 1) ? siblings[siblingPos + 1] : -1;

        writer.startObj(entry.objId);
        writer.write("<<\n");
        writer.write("/Title " + Pdf::toLiteralString(Pdf::toUTF16(entry.title)) + "\n");
        writer.write("/Parent " + Pdf::toObjRef(parentObj) + "\n");

        // Destination: page + XYZ position
        if (entry.pageIndex >= 0 && entry.pageIndex < pageObjIds.size()) {
            writer.write("/Dest [" + Pdf::toObjRef(pageObjIds[entry.pageIndex])
                         + " /XYZ 0 " + pdfCoord(entry.destY) + " null]\n");
        }

        if (prevIdx >= 0)
            writer.write("/Prev " + Pdf::toObjRef(entries[prevIdx].objId) + "\n");
        if (nextIdx >= 0)
            writer.write("/Next " + Pdf::toObjRef(entries[nextIdx].objId) + "\n");

        if (!entry.childIndices.isEmpty()) {
            writer.write("/First " + Pdf::toObjRef(entries[entry.childIndices.first()].objId) + "\n");
            writer.write("/Last " + Pdf::toObjRef(entries[entry.childIndices.last()].objId) + "\n");
            writer.write("/Count " + Pdf::toPdf(countDescendants(i)) + "\n");
        }

        writer.write(">>");
        writer.endObj(entry.objId);
    }

    // 7. Write root outline object

    int totalCount = 0;
    for (int idx : topLevel)
        totalCount += 1 + countDescendants(idx);

    writer.startObj(rootObjId);
    writer.write("<<\n/Type /Outlines\n");
    writer.write("/First " + Pdf::toObjRef(entries[topLevel.first()].objId) + "\n");
    writer.write("/Last " + Pdf::toObjRef(entries[topLevel.last()].objId) + "\n");
    writer.write("/Count " + Pdf::toPdf(totalCount) + "\n");
    writer.write(">>");
    writer.endObj(rootObjId);

    return rootObjId;
}

// --- Font embedding ---

void PdfGenerator::embedFonts(Pdf::Writer &writer)
{
    for (auto &ef : m_embeddedFonts) {
        if (m_hasHersheyGlyphs)
            continue;
        ef.fontObjId = writeCidFont(writer, ef.face, ef.pdfName);
    }
}

Pdf::ObjId PdfGenerator::writeCidFont(Pdf::Writer &writer, FontFace *face,
                                       const QByteArray &pdfName)
{
    // 1. Subset the font
    sfnt::SubsetResult subset = m_fontManager->subsetFont(face);
    QByteArray fontData = subset.success ? subset.fontData : face->rawData;

    // 2. Embed font stream
    Pdf::ObjId fontStreamObj = writer.startObj();
    writer.write("<<\n");
    writer.endObjectWithStream(fontStreamObj, fontData);

    // 3. Font descriptor
    QString psName = m_fontManager->postScriptName(face);
    QByteArray psNameBytes = psName.toLatin1();
    if (subset.success)
        psNameBytes = "AAAAAA+" + psNameBytes;

    Pdf::ObjId fontDescObj = writer.startObj();
    writer.write("<<\n/Type /FontDescriptor\n");
    writer.write("/FontName " + Pdf::toName(psNameBytes) + "\n");

    QList<int> bbox = m_fontManager->fontBBox(face);
    writer.write("/FontBBox [" + Pdf::toPdf(bbox[0]) + " " + Pdf::toPdf(bbox[1])
                 + " " + Pdf::toPdf(bbox[2]) + " " + Pdf::toPdf(bbox[3]) + "]\n");
    writer.write("/Flags " + Pdf::toPdf(m_fontManager->fontFlags(face)) + "\n");
    writer.write("/Ascent " + Pdf::toPdf(static_cast<int>(
        m_fontManager->ascent(face, 1000.0 / m_fontManager->unitsPerEm(face) * 1000))) + "\n");
    writer.write("/Descent " + Pdf::toPdf(static_cast<int>(
        -m_fontManager->descent(face, 1000.0 / m_fontManager->unitsPerEm(face) * 1000))) + "\n");
    writer.write("/CapHeight " + Pdf::toPdf(static_cast<int>(
        m_fontManager->capHeight(face, 1000.0 / m_fontManager->unitsPerEm(face) * 1000))) + "\n");
    writer.write("/ItalicAngle " + Pdf::toPdf(m_fontManager->italicAngle(face)) + "\n");
    writer.write("/StemV 80\n");
    writer.write("/FontFile2 " + Pdf::toObjRef(fontStreamObj) + "\n");
    writer.write(">>");
    writer.endObj(fontDescObj);

    // 4. Glyph widths
    Pdf::ObjId widthsObj = writer.startObj();
    writer.write("[");
    qreal upem = m_fontManager->unitsPerEm(face);
    for (uint gid : face->usedGlyphs) {
        qreal w = m_fontManager->glyphWidth(face, gid, upem);
        int pdfWidth = static_cast<int>(w * 1000.0 / upem);
        writer.write(Pdf::toPdf(gid) + " [" + Pdf::toPdf(pdfWidth) + "] ");
    }
    writer.write("]");
    writer.endObj(widthsObj);

    // 5. ToUnicode CMap
    QByteArray cmapData = buildToUnicodeCMap(face);
    Pdf::ObjId cmapObj = writer.startObj();
    writer.write("<<\n");
    writer.endObjectWithStream(cmapObj, cmapData);

    // 6. Type0 font with CIDFont descendant
    Pdf::ObjId fontObj = writer.startObj();
    writer.write("<<\n/Type /Font\n/Subtype /Type0\n");
    writer.write("/Name " + Pdf::toName(pdfName) + "\n");
    writer.write("/BaseFont " + Pdf::toName(psNameBytes) + "\n");
    writer.write("/Encoding /Identity-H\n");
    writer.write("/ToUnicode " + Pdf::toObjRef(cmapObj) + "\n");
    writer.write("/DescendantFonts [");
    writer.write("<<\n/Type /Font\n/Subtype /CIDFontType2\n");
    writer.write("/BaseFont " + Pdf::toName(psNameBytes) + "\n");
    writer.write("/FontDescriptor " + Pdf::toObjRef(fontDescObj) + "\n");
    writer.write("/CIDSystemInfo <</Ordering(Identity)/Registry(Adobe)/Supplement 0>>\n");
    writer.write("/DW 1000\n");
    writer.write("/W " + Pdf::toObjRef(widthsObj) + "\n");
    writer.write("/CIDToGIDMap /Identity\n");
    writer.write(">>]\n");
    writer.write(">>");
    writer.endObj(fontObj);

    return fontObj;
}

QByteArray PdfGenerator::buildToUnicodeCMap(FontFace *face)
{
    QByteArray cmap;
    cmap += "/CIDInit /ProcSet findresource begin\n";
    cmap += "12 dict begin\n";
    cmap += "begincmap\n";
    cmap += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
    cmap += "/CMapName /Adobe-Identity-UCS def\n";
    cmap += "/CMapType 2 def\n";
    cmap += "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";

    // Build glyph-to-unicode mapping using FreeType's charmap
    QList<QPair<uint, uint>> mappings; // glyph ID → unicode
    if (face->ftFace) {
        FT_ULong charcode;
        FT_UInt gid;
        charcode = FT_Get_First_Char(face->ftFace, &gid);
        while (gid != 0) {
            if (face->usedGlyphs.contains(gid))
                mappings.append({gid, static_cast<uint>(charcode)});
            charcode = FT_Get_Next_Char(face->ftFace, charcode, &gid);
        }
    }

    // Write in batches of 100
    int pos = 0;
    while (pos < mappings.size()) {
        int batchSize = qMin(100, mappings.size() - pos);
        cmap += Pdf::toPdf(batchSize) + " beginbfchar\n";
        for (int i = 0; i < batchSize; ++i) {
            auto [gid, unicode] = mappings[pos + i];
            cmap += "<" + QByteArray::number(gid, 16).toUpper().rightJustified(4, '0') + "> "
                  + "<" + QByteArray::number(unicode, 16).toUpper().rightJustified(4, '0') + ">\n";
        }
        cmap += "endbfchar\n";
        pos += batchSize;
    }

    cmap += "endcmap\n";
    cmap += "CMapName currentdict /CMap defineresource pop\n";
    cmap += "end\nend\n";
    return cmap;
}
