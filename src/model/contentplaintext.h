#ifndef PENELOPE_CONTENTPLAINTEXT_H
#define PENELOPE_CONTENTPLAINTEXT_H

#include "contentmodel.h"

namespace Content {
// Clipboard text: visible content, without Markdown syntax or layout typography.
QString toPlainText(const Document &document);
}

#endif
