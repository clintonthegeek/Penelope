#ifndef PENELOPE_PRINTCONTROLLER_H
#define PENELOPE_PRINTCONTROLLER_H

#include <QObject>

#include "pagelayout.h"

class QTextDocument;
class QPrinter;
class QPainter;

namespace Poppler { class Document; }

class PrintController : public QObject
{
    Q_OBJECT

public:
    explicit PrintController(QTextDocument *document,
                             QObject *parent = nullptr);

    void print(QWidget *parentWidget);
    void exportPdf(const QString &filePath, QWidget *parentWidget);

    void setPageLayout(const PageLayout &layout);

    // PDF pipeline: set pre-generated PDF data for printing
    void setPdfData(const QByteArray &pdf);

    // Document metadata
    void setFileName(const QString &name);
    void setDocumentTitle(const QString &title);

private:
    void configurePrinter(QPrinter *printer);
    void renderDocument(QPrinter *printer);
    void renderDocumentFromPdf(QPrinter *printer);
    void renderPage(QPainter *painter, int pageNumber, int totalPages,
                    const QRectF &headerRect, const QRectF &bodyRect,
                    const QRectF &footerRect, qreal dpiScale);

    QTextDocument *m_document;
    PageLayout m_pageLayout;

    // PDF pipeline
    QByteArray m_pdfData;

    // Metadata
    QString m_fileName;
    QString m_documentTitle;
};

#endif // PENELOPE_PRINTCONTROLLER_H
