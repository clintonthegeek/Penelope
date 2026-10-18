#ifndef PENELOPE_ENTITYTABLE_H
#define PENELOPE_ENTITYTABLE_H

#include <QString>

// The named character references MD4C accepts (`&name;`). Source data:
// https://html.spec.whatwg.org/entities.json, semicolon-terminated spellings.
// Returns a null string when @p name is not a known entity.
QString decodeNamedEntity(const QString &name);

#endif // PENELOPE_ENTITYTABLE_H
