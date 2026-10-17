#ifndef PENELOPE_STYLERUNINDEX_H
#define PENELOPE_STYLERUNINDEX_H

#include "textshaper.h"
#include <QList>

int styleRunIndexAt(int charPos, const QList<StyleRun> &runs);

#endif
