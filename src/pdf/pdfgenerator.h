/*
 * pdfgenerator.h — Box tree → PDF content streams + font embedding
 *
 * Converts Layout::LayoutResult into a complete PDF document.
 * Uses CIDFont Type 2 + Identity-H encoding for text,
 * DCTDecode/FlateDecode for images.
 *
 * Page content rendering is delegated to PdfBoxRenderer; this class
 * handles the overall PDF document structure, font/image embedding,
 * and resource management.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFGENERATOR_H
#define PENELOPE_PDFGENERATOR_H

#include <QByteArray>
#include <QString>

#include "hersheyfont.h"
#include "layoutengine.h"
#include "pagelayout.h"
#include "pdfboxrenderer.h"
#include "pdfwriter.h"
#include "pdfexportoptions.h"

class FontManager;
struct FontFace;

class PdfGenerator {
public:
    explicit PdfGenerator(FontManager *fontManager);

    QByteArray generate(const Layout::LayoutResult &layout,
                        const PageLayout &pageLayout,
                        const QString &title);
    bool generateToFile(const Layout::LayoutResult &layout,
                        const PageLayout &pageLayout,
                        const QString &title,
                        const QString &filePath);

    void setDocumentInfo(const QString &filename, const QString &title);
    void setMaxJustifyGap(qreal gap) { m_maxJustifyGap = gap; }
    void setExportOptions(const PdfExportOptions &opts) { m_exportOptions = opts; }

private:
    // Page content rendering (delegates to PdfBoxRenderer)
    QByteArray renderPage(const Layout::Page &page,
                          const PageLayout &pageLayout,
                          const Pdf::ResourceDict &resources);

    // Link annotations: collected per-page during rendering
    QList<QList<PdfLinkAnnotation>> m_pageAnnotations; // indexed by page number
    int m_currentPageIndex = 0;

    // Font embedding
    struct EmbeddedFont {
        Pdf::ObjId fontObjId = 0;
        QByteArray pdfName; // e.g. "F0", "F1"
        FontFace *face = nullptr;
    };
    QList<EmbeddedFont> m_embeddedFonts;
    QHash<FontFace *, int> m_fontIndex; // face -> index in m_embeddedFonts

    QByteArray pdfFontName(FontFace *face);
    int ensureFontRegistered(FontFace *face);
    void embedFonts(Pdf::Writer &writer);
    Pdf::ObjId writeCidFont(Pdf::Writer &writer, FontFace *face,
                            const QByteArray &pdfName);
    QByteArray buildToUnicodeCMap(FontFace *face);

    // Header/footer rendering
    void renderHeaderFooter(QByteArray &stream, const PageLayout &pageLayout,
                            int pageNumber, int totalPages,
                            qreal pageWidth, qreal pageHeight);

    // Image embedding
    struct EmbeddedImage {
        Pdf::ObjId objId = 0;
        QByteArray pdfName; // e.g. "Im0", "Im1"
        QImage image;
        int width = 0;
        int height = 0;
    };
    // Glyph Form XObjects (reusable vector glyph drawings)
    struct GlyphFormKey {
        const HersheyFont *hersheyFont = nullptr;
        FontFace *ttfFace = nullptr;
        uint glyphId = 0;
        bool bold = false;

        bool operator==(const GlyphFormKey &o) const {
            return hersheyFont == o.hersheyFont && ttfFace == o.ttfFace
                   && glyphId == o.glyphId && bold == o.bold;
        }
    };
    friend size_t qHash(const PdfGenerator::GlyphFormKey &k, size_t seed = 0) {
        return qHashMulti(seed, quintptr(k.hersheyFont), quintptr(k.ttfFace),
                          k.glyphId, k.bold);
    }

    struct GlyphFormEntry {
        Pdf::ObjId objId = 0;
        QByteArray pdfName;    // "HG0", "HG1", ...
        qreal advanceWidth = 0; // in glyph units
    };

    QHash<GlyphFormKey, GlyphFormEntry> m_glyphForms;
    int m_nextGlyphFormIdx = 0;
    GlyphFormEntry ensureGlyphForm(const HersheyFont *hersheyFont, FontFace *ttfFace,
                                   uint glyphId, bool bold);

    QList<EmbeddedImage> m_embeddedImages;
    QHash<QString, int> m_imageIndex; // imageId -> index in m_embeddedImages
    int ensureImageRegistered(const QString &imageId, const QImage &image);
    void embedImages(Pdf::Writer &writer);

    // PDF Outline / Bookmarks
    struct OutlineEntry {
        QString title;
        int level = 0;        // heading level 1-6
        int pageIndex = 0;    // index into pageObjIds
        qreal destY = 0;      // PDF y-coordinate for /Dest
        Pdf::ObjId objId = 0;
        QList<int> childIndices; // indices into flat list
    };

    Pdf::ObjId writeOutlines(Pdf::Writer &writer,
                              const QList<Pdf::ObjId> &pageObjIds,
                              const Layout::LayoutResult &layout,
                              const PageLayout &pageLayout);

    // Utility
    static QByteArray colorOperator(const QColor &color, bool fill = true);
    static QByteArray pdfCoord(qreal v);

    FontManager *m_fontManager;
    QString m_filename;
    QString m_title;
    qreal m_maxJustifyGap = 14.0;
    PdfExportOptions m_exportOptions;
    bool m_hasHersheyGlyphs = false;
    Pdf::Writer *m_writer = nullptr;           // set during generate(), null otherwise
    Pdf::ResourceDict *m_resources = nullptr;  // set during generate(), null otherwise
};

#endif // PENELOPE_PDFGENERATOR_H
