#ifndef PENELOPE_FONTFEATURES_H
#define PENELOPE_FONTFEATURES_H

#include <QFlags>
#include <QFont>
#include <QString>
#include <QTextCharFormat>

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

// Apply font features to a QTextCharFormat using native APIs.
// This is the correct way to set features on styled text in QTextDocument,
// because QTextCharFormat decomposes fonts into individual properties.
//
// Features with native QTextCharFormat support (work reliably):
//   - SmallCaps  → setFontCapitalization(QFont::SmallCaps)
//   - Kerning    → setFontKerning()
//
// Features without native support (applied via QFont::setFeature, may
// not survive format round-trips in all Qt versions):
//   - Ligatures  → liga (enabled by default in HarfBuzz)
//   - OldStyleNums → onum
//   - LiningNums → lnum
//   - ContextAlts → calt (enabled by default in HarfBuzz)
inline void applyToCharFormat(QTextCharFormat &cf, Features features)
{
    // Small caps - native QTextCharFormat property, always works
    cf.setFontCapitalization(features.testFlag(SmallCaps)
                             ? QFont::SmallCaps : QFont::MixedCase);

    // Kerning - native QTextCharFormat property, always works
    cf.setFontKerning(features.testFlag(Kerning));

    // For features without native QTextCharFormat support, apply via
    // QFont::setFeature(). These are stored on the QFont object and
    // may propagate through the layout engine in newer Qt versions.
    QFont f = cf.font();
    f.setFeature("liga", features.testFlag(Ligatures) ? 1 : 0);
    if (features.testFlag(OldStyleNums)) {
        f.setFeature("onum", 1);
        f.setFeature("lnum", 0);
    } else if (features.testFlag(LiningNums)) {
        f.setFeature("lnum", 1);
        f.setFeature("onum", 0);
    }
    f.setFeature("calt", features.testFlag(ContextAlts) ? 1 : 0);
    cf.setFont(f, QTextCharFormat::FontPropertiesSpecifiedOnly);
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
