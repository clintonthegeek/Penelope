#ifndef PENELOPE_FONTFEATURES_H
#define PENELOPE_FONTFEATURES_H

#include <QFlags>
#include <QStringList>

namespace FontFeatures {

// OpenType feature tags that Penelope supports.
// These map to 4-byte OT feature tags.
enum Feature : quint32 {
    None          = 0,
    Ligatures     = (1 << 0),  // liga - Standard ligatures (fi, fl, ff, ffi, ffl)
    SmallCaps     = (1 << 1),  // smcp - Small capitals
    OldStyleNums  = (1 << 2),  // onum - Old-style (lowercase) numerals
    LiningNums    = (1 << 3),  // lnum - Lining (uppercase) numerals
    Kerning       = (1 << 4),  // kern - Kerning
    ContextAlts   = (1 << 5),  // calt - Contextual alternates
};
Q_DECLARE_FLAGS(Features, Feature)
Q_DECLARE_OPERATORS_FOR_FLAGS(Features)

// Default features enabled in a new style
inline Features defaultFeatures()
{
    return Ligatures | Kerning | ContextAlts | OldStyleNums;
}

// Serialize features to a string list for JSON storage
inline QStringList toStringList(Features features)
{
    QStringList list;
    if (features.testFlag(Ligatures))    list << QStringLiteral("liga");
    if (features.testFlag(SmallCaps))    list << QStringLiteral("smcp");
    if (features.testFlag(OldStyleNums)) list << QStringLiteral("onum");
    if (features.testFlag(LiningNums))   list << QStringLiteral("lnum");
    if (features.testFlag(Kerning))      list << QStringLiteral("kern");
    if (features.testFlag(ContextAlts))  list << QStringLiteral("calt");
    return list;
}

// Deserialize features from a string list
inline Features fromStringList(const QStringList &list)
{
    Features features;
    for (const QString &tag : list) {
        if (tag == QLatin1String("liga"))      features |= Ligatures;
        else if (tag == QLatin1String("smcp")) features |= SmallCaps;
        else if (tag == QLatin1String("onum")) features |= OldStyleNums;
        else if (tag == QLatin1String("lnum")) features |= LiningNums;
        else if (tag == QLatin1String("kern")) features |= Kerning;
        else if (tag == QLatin1String("calt")) features |= ContextAlts;
    }
    return features;
}

} // namespace FontFeatures

#endif // PENELOPE_FONTFEATURES_H
