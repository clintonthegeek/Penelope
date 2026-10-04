#include "printcontroller.h"

#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <poppler-qt6.h>

PrintController::PrintController(QObject *parent) : QObject(parent) {}

void PrintController::setPageLayout(const PageLayout &layout) { m_pageLayout = layout; }
void PrintController::setPdfData(const QByteArray &pdf) { m_pdfData = pdf; }

void PrintController::print(QWidget *parentWidget)
{
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::loadFromData(m_pdfData));
    if (!doc || doc->numPages() == 0) return;

    QPrinter printer(QPrinter::HighResolution);
    // Margins are already included in the PDF.
    printer.setPageSize(QPageSize(m_pageLayout.pageSizeId));
    printer.setPageOrientation(m_pageLayout.orientation);
    QPrintDialog dialog(&printer, parentWidget);
    dialog.setWindowTitle(tr("Print Document"));
    dialog.setMinMax(1, doc->numPages());
    if (dialog.exec() == QDialog::Accepted) renderDocumentFromPdf(&printer);
}

void PrintController::renderDocumentFromPdf(QPrinter *printer)
{
    std::unique_ptr<Poppler::Document> doc(
        Poppler::Document::loadFromData(m_pdfData));
    if (!doc)
        return;

    // renderToPainter() requires the QPainter backend (not the default Splash).
    doc->setRenderBackend(Poppler::Document::QPainterBackend);

    int totalPages = doc->numPages();

    // Respect page range from print dialog (1-based; 0 means all pages)
    int fromPage = printer->fromPage();
    int toPage = printer->toPage();
    if (fromPage < 1) fromPage = 1;
    if (toPage < 1) toPage = totalPages;
    fromPage = qBound(1, fromPage, totalPages);
    toPage = qBound(fromPage, toPage, totalPages);

    // Full-page mode: QPainter origin covers the entire physical page,
    // matching PDF coordinate space where margins are baked into content.
    printer->setFullPage(true);

    QPainter painter(printer);
    if (!painter.isActive())
        return;

    // Pass printer resolution so PDF points map 1:1 to device pixels.
    const qreal dpi = printer->resolution();

    bool firstPage = true;
    for (int i = fromPage - 1; i < toPage; ++i) {
        if (!firstPage)
            printer->newPage();
        firstPage = false;

        std::unique_ptr<Poppler::Page> page(doc->page(i));
        if (!page)
            continue;

        page->renderToPainter(&painter, dpi, dpi);
    }

    painter.end();
}
