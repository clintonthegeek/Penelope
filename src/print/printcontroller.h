#ifndef PENELOPE_PRINTCONTROLLER_H
#define PENELOPE_PRINTCONTROLLER_H

#include <QObject>
#include "pagelayout.h"

class QPrinter;
class QWidget;

class PrintController : public QObject
{
    Q_OBJECT
public:
    explicit PrintController(QObject *parent = nullptr);
    void print(QWidget *parentWidget);
    void setPageLayout(const PageLayout &layout);
    void setPdfData(const QByteArray &pdf);

private:
    void renderDocumentFromPdf(QPrinter *printer);
    PageLayout m_pageLayout;
    QByteArray m_pdfData;
};
#endif
