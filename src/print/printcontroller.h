#ifndef PENELOPE_PRINTCONTROLLER_H
#define PENELOPE_PRINTCONTROLLER_H

#include <functional>

#include <QObject>
#include <QSizeF>
#include <QString>

#include "pagelayout.h"

class QWidget;

class PrintController : public QObject
{
    Q_OBJECT
public:
    explicit PrintController(QObject *parent = nullptr);
    void setPageLayout(const PageLayout &layout);

    /// Ask for a printer, then send PostScript produced from the PDF bytes
    /// @p makePdf returns. The factory is called again when the dialog's
    /// paper size differs from @p layout, so the PDF matches the printer.
    bool print(QWidget *parentWidget,
               const std::function<QByteArray(const PageLayout &)> &makePdf,
               QString *errorMessage);

    /// Poppler's PS converter writes the PDF page content as PostScript.
    /// @p fromPage and @p toPage are 1-based; values below 1 mean every page.
    static QByteArray pdfToPostScript(const QByteArray &pdf, const QSizeF &pagePoints,
                                      int fromPage, int toPage, QString *errorMessage);

private:
    PageLayout m_pageLayout;
};
#endif
