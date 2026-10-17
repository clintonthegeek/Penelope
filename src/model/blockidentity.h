#ifndef PENELOPE_BLOCKIDENTITY_H
#define PENELOPE_BLOCKIDENTITY_H

#include "contentmodel.h"
#include <QList>

void restabilizeBlockIds(const QList<Content::BlockNode> &previous,
                         QList<Content::BlockNode> &current);

#endif
