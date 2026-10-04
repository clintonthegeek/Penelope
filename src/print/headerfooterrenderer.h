#ifndef PENELOPE_HEADERFOOTERRENDERER_H
#define PENELOPE_HEADERFOOTERRENDERER_H

#include <QString>

struct PageMetadata {
    int pageNumber = 0;   // 0-based
    int totalPages = 1;
    QString fileName;
    QString title;
};

namespace HeaderFooterRenderer {

QString resolveField(const QString &text, const PageMetadata &meta);

} // namespace HeaderFooterRenderer

#endif // PENELOPE_HEADERFOOTERRENDERER_H
