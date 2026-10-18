#include "printcontroller.h"

#include <QBuffer>
#include <QDir>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QTemporaryFile>

// poppler-qt6.h declares the converters. Splitting them into
// poppler-converter.h only happened in poppler 25.05; the extra include
// breaks 24.08 (Ubuntu 24.04 / KDE Neon), and including it here is not
// needed because the umbrella header includes it where it exists.
#include <poppler-qt6.h>

PrintController::PrintController(QObject *parent) : QObject(parent) {}

void PrintController::setPageLayout(const PageLayout &layout) { m_pageLayout = layout; }

QByteArray PrintController::pdfToPostScript(const QByteArray &pdf, const QSizeF &pagePoints,
                                            int fromPage, int toPage, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return QByteArray();
    };
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::loadFromData(pdf));
    if (!doc || doc->numPages() < 1)
        return fail(tr("The print PDF has no pages."));

    const int total = doc->numPages();
    if (fromPage < 1)
        fromPage = 1;
    if (toPage < 1)
        toPage = total;
    fromPage = qBound(1, fromPage, total);
    toPage = qBound(fromPage, toPage, total);

    QList<int> pages;
    pages.reserve(toPage - fromPage + 1);
    for (int page = fromPage; page <= toPage; ++page)
        pages.append(page);

    std::unique_ptr<Poppler::PSConverter> converter(doc->psConverter());
    if (!converter)
        return fail(tr("PostScript conversion is not available."));

    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly))
        return fail(tr("Could not store the PostScript."));
    converter->setOutputDevice(&buffer);
    converter->setPageList(pages);
    converter->setPaperWidth(qMax(1, qRound(pagePoints.width())));
    converter->setPaperHeight(qMax(1, qRound(pagePoints.height())));
    converter->setLeftMargin(0);
    converter->setRightMargin(0);
    converter->setTopMargin(0);
    converter->setBottomMargin(0);
    converter->setStrictMargins(true);
    converter->setPSOptions(Poppler::PSConverter::Printing);
    if (!converter->convert())
        return fail(tr("Could not convert the document to PostScript."));

    const QByteArray ps = buffer.data();
    if (!ps.startsWith("%!PS"))
        return fail(tr("The printer data is not PostScript."));
    return ps;
}

namespace {
bool sendPostScript(const QByteArray &postScript, const QPrinter &printer, QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("penelope-print-XXXXXX.ps")));
    file.setAutoRemove(true);
    if (!file.open() || file.write(postScript) != postScript.size())
        return fail(PrintController::tr("Could not store the PostScript."));
    file.flush();

    QStringList args;
    const QString queue = printer.printerName();
    if (!queue.isEmpty())
        args << QStringLiteral("-d") << queue;
    args << QStringLiteral("-n") << QString::number(qMax(1, printer.copyCount()));
    args << QStringLiteral("-o") << QStringLiteral("document-format=application/postscript");
    args << QStringLiteral("-o") << (printer.colorMode() == QPrinter::GrayScale
             ? QStringLiteral("print-color-mode=monochrome")
             : QStringLiteral("print-color-mode=color"));
    if (printer.copyCount() > 1)
        args << QStringLiteral("-o")
             << (printer.collateCopies() ? QStringLiteral("collate=true")
                                         : QStringLiteral("collate=false"));
    switch (printer.duplex()) {
    case QPrinter::DuplexLongSide:
        args << QStringLiteral("-o") << QStringLiteral("sides=two-sided-long-edge");
        break;
    case QPrinter::DuplexShortSide:
        args << QStringLiteral("-o") << QStringLiteral("sides=two-sided-short-edge");
        break;
    case QPrinter::DuplexNone:
        args << QStringLiteral("-o") << QStringLiteral("sides=one-sided");
        break;
    case QPrinter::DuplexAuto:
        break;
    }
    const QString title = printer.docName();
    if (!title.isEmpty())
        args << QStringLiteral("-t") << title;
    args << file.fileName();

    QProcess lp;
    lp.start(QStringLiteral("lp"), args);
    if (!lp.waitForStarted(5000))
        return fail(PrintController::tr("Could not start lp to reach the printer."));
    if (!lp.waitForFinished(120000) || lp.exitCode() != 0) {
        const QString detail = QString::fromLocal8Bit(lp.readAllStandardError()).trimmed();
        return fail(detail.isEmpty()
                        ? PrintController::tr("The printer rejected the PostScript.")
                        : detail);
    }
    return true;
}
}

bool PrintController::print(QWidget *parentWidget,
                            const std::function<QByteArray(const PageLayout &)> &makePdf,
                            QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    if (!makePdf)
        return fail(tr("Nothing to print."));

    QByteArray pdf = makePdf(m_pageLayout);
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::loadFromData(pdf));
    if (!doc || doc->numPages() < 1)
        return fail(tr("Nothing to print."));

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(tr("Penelope"));
    printer.setPageSize(QPageSize(m_pageLayout.pageSizeId));
    printer.setPageOrientation(m_pageLayout.orientation);
    QPrintDialog dialog(&printer, parentWidget);
    dialog.setWindowTitle(tr("Print Document"));
    dialog.setMinMax(1, doc->numPages());
    if (dialog.exec() != QDialog::Accepted)
        return false;

    const QPageLayout chosen = printer.pageLayout();
    const QPageSize::PageSizeId chosenId = chosen.pageSize().id();
    if (chosenId != QPageSize::Custom
        && (chosenId != m_pageLayout.pageSizeId || chosen.orientation() != m_pageLayout.orientation)) {
        m_pageLayout.pageSizeId = chosenId;
        m_pageLayout.orientation = chosen.orientation();
        pdf = makePdf(m_pageLayout);
        if (pdf.isEmpty())
            return fail(tr("Nothing to print."));
    }

    QString conversionError;
    const QByteArray postScript = pdfToPostScript(pdf, m_pageLayout.pageSizePoints(),
                                                   printer.fromPage(), printer.toPage(),
                                                   &conversionError);
    if (postScript.isEmpty())
        return fail(conversionError);
    return sendPostScript(postScript, printer, errorMessage);
}
