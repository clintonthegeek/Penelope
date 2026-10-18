#include "thememanager.h"
#include "stylemanager.h"
#include "paragraphstyle.h"
#include "characterstyle.h"
#include "tablestyle.h"
#include "footnotestyle.h"
#include "fontfeatures.h"

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QMarginsF>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Helper parsers
// ---------------------------------------------------------------------------

static QFont::Weight parseWeight(const QString &w)
{
    if (w == QLatin1String("bold"))
        return QFont::Bold;
    bool ok;
    int num = w.toInt(&ok);
    if (ok)
        return static_cast<QFont::Weight>(num);
    return QFont::Normal;
}

static Qt::Alignment parseAlignment(const QString &a)
{
    if (a == QLatin1String("center"))  return Qt::AlignCenter;
    if (a == QLatin1String("right"))   return Qt::AlignRight;
    if (a == QLatin1String("justify")) return Qt::AlignJustify;
    return Qt::AlignLeft;
}

// ---------------------------------------------------------------------------
// applyStyleOverrides — parse a JSON root into a StyleManager
// ---------------------------------------------------------------------------

void ThemeManager::applyStyleOverrides(const QJsonObject &root, StyleManager *sm)
{
    // Paragraph styles
    QJsonObject paraStyles = root.value(QLatin1String("paragraphStyles")).toObject();
    for (auto it = paraStyles.begin(); it != paraStyles.end(); ++it) {
        QJsonObject props = it.value().toObject();

        ParagraphStyle *existing = sm->paragraphStyle(it.key());
        ParagraphStyle style = existing ? *existing : ParagraphStyle(it.key());

        if (props.contains(QLatin1String("parent")))
            style.setParentStyleName(props.value(QLatin1String("parent")).toString());
        if (props.contains(QLatin1String("fontFamily")))
            style.setFontFamily(props.value(QLatin1String("fontFamily")).toString());
        if (props.contains(QLatin1String("fontSize")))
            style.setFontSize(props.value(QLatin1String("fontSize")).toDouble());
        if (props.contains(QLatin1String("fontWeight")))
            style.setFontWeight(parseWeight(props.value(QLatin1String("fontWeight")).toString()));
        if (props.contains(QLatin1String("fontItalic")))
            style.setFontItalic(props.value(QLatin1String("fontItalic")).toBool());
        if (props.contains(QLatin1String("foreground")))
            style.setForeground(QColor(props.value(QLatin1String("foreground")).toString()));
        if (props.contains(QLatin1String("background")))
            style.setBackground(QColor(props.value(QLatin1String("background")).toString()));
        if (props.contains(QLatin1String("alignment")))
            style.setAlignment(parseAlignment(props.value(QLatin1String("alignment")).toString()));
        if (props.contains(QLatin1String("spaceBefore")))
            style.setSpaceBefore(props.value(QLatin1String("spaceBefore")).toDouble());
        if (props.contains(QLatin1String("spaceAfter")))
            style.setSpaceAfter(props.value(QLatin1String("spaceAfter")).toDouble());
        if (props.contains(QLatin1String("lineHeightPercent")))
            style.setLineHeightPercent(props.value(QLatin1String("lineHeightPercent")).toInt());
        if (props.contains(QLatin1String("firstLineIndent")))
            style.setFirstLineIndent(props.value(QLatin1String("firstLineIndent")).toDouble());
        if (props.contains(QLatin1String("wordSpacing")))
            style.setWordSpacing(props.value(QLatin1String("wordSpacing")).toDouble());
        if (props.contains(QLatin1String("leftMargin")))
            style.setLeftMargin(props.value(QLatin1String("leftMargin")).toDouble());
        if (props.contains(QLatin1String("rightMargin")))
            style.setRightMargin(props.value(QLatin1String("rightMargin")).toDouble());
        if (props.contains(QLatin1String("fontFeatures"))) {
            QJsonArray features = props.value(QLatin1String("fontFeatures")).toArray();
            QStringList featureList;
            for (const auto &f : features)
                featureList.append(f.toString());
            style.setFontFeatures(FontFeatures::fromStringList(featureList));
        }
        if (props.contains(QLatin1String("baseCharacterStyle")))
            style.setBaseCharacterStyleName(props.value(QLatin1String("baseCharacterStyle")).toString());

        sm->addParagraphStyle(style);
    }

    // Character styles
    QJsonObject charStyles = root.value(QLatin1String("characterStyles")).toObject();
    for (auto it = charStyles.begin(); it != charStyles.end(); ++it) {
        QJsonObject props = it.value().toObject();

        CharacterStyle *existing = sm->characterStyle(it.key());
        CharacterStyle style = existing ? *existing : CharacterStyle(it.key());

        if (props.contains(QLatin1String("parent")))
            style.setParentStyleName(props.value(QLatin1String("parent")).toString());
        if (props.contains(QLatin1String("fontFamily")))
            style.setFontFamily(props.value(QLatin1String("fontFamily")).toString());
        if (props.contains(QLatin1String("fontSize")))
            style.setFontSize(props.value(QLatin1String("fontSize")).toDouble());
        if (props.contains(QLatin1String("fontWeight")))
            style.setFontWeight(parseWeight(props.value(QLatin1String("fontWeight")).toString()));
        if (props.contains(QLatin1String("fontItalic")))
            style.setFontItalic(props.value(QLatin1String("fontItalic")).toBool());
        if (props.contains(QLatin1String("underline")))
            style.setFontUnderline(props.value(QLatin1String("underline")).toBool());
        if (props.contains(QLatin1String("strikeOut")))
            style.setFontStrikeOut(props.value(QLatin1String("strikeOut")).toBool());
        if (props.contains(QLatin1String("foreground")))
            style.setForeground(QColor(props.value(QLatin1String("foreground")).toString()));
        if (props.contains(QLatin1String("background")))
            style.setBackground(QColor(props.value(QLatin1String("background")).toString()));
        if (props.contains(QLatin1String("letterSpacing")))
            style.setLetterSpacing(props.value(QLatin1String("letterSpacing")).toDouble());
        if (props.contains(QLatin1String("fontFeatures"))) {
            QJsonArray features = props.value(QLatin1String("fontFeatures")).toArray();
            QStringList featureList;
            for (const auto &f : features)
                featureList.append(f.toString());
            style.setFontFeatures(FontFeatures::fromStringList(featureList));
        }

        sm->addCharacterStyle(style);
    }

    // Table styles
    if (root.contains(QLatin1String("tableStyles"))) {
        QJsonObject tableStyles = root.value(QLatin1String("tableStyles")).toObject();
        for (auto it = tableStyles.begin(); it != tableStyles.end(); ++it) {
            QJsonObject props = it.value().toObject();

            TableStyle *existing = sm->tableStyle(it.key());
            TableStyle ts = existing ? *existing : TableStyle(it.key());

            if (props.contains(QLatin1String("borderCollapse")))
                ts.setBorderCollapse(props.value(QLatin1String("borderCollapse")).toBool());
            if (props.contains(QLatin1String("cellPadding"))) {
                QJsonObject p = props.value(QLatin1String("cellPadding")).toObject();
                ts.setCellPadding(QMarginsF(
                    p.value(QLatin1String("left")).toDouble(4),
                    p.value(QLatin1String("top")).toDouble(3),
                    p.value(QLatin1String("right")).toDouble(4),
                    p.value(QLatin1String("bottom")).toDouble(3)));
            }
            if (props.contains(QLatin1String("headerBackground")))
                ts.setHeaderBackground(QColor(props.value(QLatin1String("headerBackground")).toString()));
            if (props.contains(QLatin1String("headerForeground")))
                ts.setHeaderForeground(QColor(props.value(QLatin1String("headerForeground")).toString()));
            if (props.contains(QLatin1String("bodyBackground")))
                ts.setBodyBackground(QColor(props.value(QLatin1String("bodyBackground")).toString()));
            if (props.contains(QLatin1String("alternateRowColor")))
                ts.setAlternateRowColor(QColor(props.value(QLatin1String("alternateRowColor")).toString()));
            if (props.contains(QLatin1String("alternateFrequency")))
                ts.setAlternateFrequency(props.value(QLatin1String("alternateFrequency")).toInt(1));

            auto parseBorder = [](const QJsonObject &obj) {
                TableStyle::Border b;
                b.width = obj.value(QLatin1String("width")).toDouble(0.5);
                b.color = QColor(obj.value(QLatin1String("color")).toString(QStringLiteral("#333333")));
                return b;
            };
            if (props.contains(QLatin1String("outerBorder")))
                ts.setOuterBorder(parseBorder(props.value(QLatin1String("outerBorder")).toObject()));
            if (props.contains(QLatin1String("innerBorder")))
                ts.setInnerBorder(parseBorder(props.value(QLatin1String("innerBorder")).toObject()));
            if (props.contains(QLatin1String("headerBottomBorder")))
                ts.setHeaderBottomBorder(parseBorder(props.value(QLatin1String("headerBottomBorder")).toObject()));
            if (props.contains(QLatin1String("headerParagraphStyle")))
                ts.setHeaderParagraphStyle(props.value(QLatin1String("headerParagraphStyle")).toString());
            if (props.contains(QLatin1String("bodyParagraphStyle")))
                ts.setBodyParagraphStyle(props.value(QLatin1String("bodyParagraphStyle")).toString());

            sm->addTableStyle(ts);
        }
    }

    // Footnote style
    if (root.contains(QLatin1String("footnoteStyle"))) {
        FootnoteStyle fs = sm->footnoteStyle();
        fs.applyJson(root.value(QLatin1String("footnoteStyle")).toObject());
        sm->setFootnoteStyle(fs);
    }
}

// ---------------------------------------------------------------------------
// assignDefaultParents — ensure style hierarchy is intact
// ---------------------------------------------------------------------------

void ThemeManager::assignDefaultParents(StyleManager *sm)
{
    // Default paragraph hierarchy:
    //   Default Paragraph Style
    //   +-- Body Text
    //   |   +-- Block Quotation, List Item, Table Cell
    //   +-- Heading
    //   |   +-- Heading 1-6
    //   +-- Code Block
    //   +-- Table Header

    // Ensure abstract parent styles exist
    if (!sm->paragraphStyle(QStringLiteral("Default Paragraph Style"))) {
        ParagraphStyle dps(QStringLiteral("Default Paragraph Style"));
        dps.setFontFamily(QStringLiteral("Noto Serif"));
        dps.setFontSize(11.0);
        dps.setLineHeightPercent(100);
        dps.setForeground(QColor(0x1a, 0x1a, 0x1a));
        sm->addParagraphStyle(dps);
    }

    if (!sm->paragraphStyle(QStringLiteral("Heading"))) {
        ParagraphStyle heading(QStringLiteral("Heading"));
        heading.setParentStyleName(QStringLiteral("Default Paragraph Style"));
        heading.setFontFamily(QStringLiteral("Noto Sans"));
        heading.setFontWeight(QFont::Bold);
        heading.setAlignment(Qt::AlignLeft);
        sm->addParagraphStyle(heading);
    }

    // Assign parents where not explicitly set
    struct DefaultParentMap {
        const char *styleName;
        const char *parentName;
    };

    static const DefaultParentMap paraDefaults[] = {
        {"BodyText",           "Default Paragraph Style"},
        {"BlockQuote",         "BodyText"},
        {"ListItem",           "BodyText"},
        {"OrderedListItem",    "ListItem"},
        {"UnorderedListItem",  "ListItem"},
        {"TaskListItem",       "ListItem"},
        {"TableCell",          "BodyText"},
        {"Heading1",           "Heading"},
        {"Heading2",           "Heading"},
        {"Heading3",           "Heading"},
        {"Heading4",           "Heading"},
        {"Heading5",           "Heading"},
        {"Heading6",           "Heading"},
        {"CodeBlock",          "Default Paragraph Style"},
        {"TableHeader",        "Default Paragraph Style"},
        {"TableBody",          "Default Paragraph Style"},
        {"Heading",            "Default Paragraph Style"},
        {"HorizontalRule",     "Default Paragraph Style"},
        {"MathDisplay",        "Default Paragraph Style"},
    };

    for (const auto &def : paraDefaults) {
        QString styleName = QString::fromLatin1(def.styleName);
        ParagraphStyle *s = sm->paragraphStyle(styleName);
        if (!s) {
            ParagraphStyle stub(styleName);
            stub.setParentStyleName(QString::fromLatin1(def.parentName));
            sm->addParagraphStyle(stub);
        } else if (s->parentStyleName().isEmpty()) {
            s->setParentStyleName(QString::fromLatin1(def.parentName));
        }
    }

    // Set CodeBlock's baseCharacterStyle if not already set
    ParagraphStyle *codeBlock = sm->paragraphStyle(QStringLiteral("CodeBlock"));
    if (codeBlock && !codeBlock->hasBaseCharacterStyle())
        codeBlock->setBaseCharacterStyleName(QStringLiteral("Code"));

    // Default character hierarchy:
    //   Default Character Style
    //   +-- Emphasis, Strong, StrongEmphasis, Strikethrough, Subscript, Superscript
    //   +-- Code
    //   |   +-- InlineCode
    //   +-- Link
    //   +-- Emoji, MathInline

    if (!sm->characterStyle(QStringLiteral("Default Character Style"))) {
        CharacterStyle dcs(QStringLiteral("Default Character Style"));
        CharacterStyle *dt = sm->characterStyle(QStringLiteral("DefaultText"));
        if (dt) {
            if (dt->hasFontFamily()) dcs.setFontFamily(dt->fontFamily());
            if (dt->hasFontSize()) dcs.setFontSize(dt->fontSize());
            if (dt->hasForeground()) dcs.setForeground(dt->foreground());
        } else {
            dcs.setFontFamily(QStringLiteral("Noto Serif"));
            dcs.setFontSize(11.0);
            dcs.setForeground(QColor(0x1a, 0x1a, 0x1a));
        }
        sm->addCharacterStyle(dcs);
    }

    if (!sm->characterStyle(QStringLiteral("Code"))) {
        CharacterStyle code(QStringLiteral("Code"));
        code.setParentStyleName(QStringLiteral("Default Character Style"));
        code.setFontFamily(QStringLiteral("JetBrains Mono"));
        code.setFontSize(10.0);
        sm->addCharacterStyle(code);
    }

    static const DefaultParentMap charDefaults[] = {
        {"DefaultText",     "Default Character Style"},
        {"Emphasis",        "Default Character Style"},
        {"Strong",          "Default Character Style"},
        {"StrongEmphasis",  "Default Character Style"},
        {"InlineCode",      "Code"},
        {"Link",            "Default Character Style"},
        {"Strikethrough",   "Default Character Style"},
        {"Subscript",       "Default Character Style"},
        {"Superscript",     "Default Character Style"},
        {"Emoji",           "Default Character Style"},
        {"MathInline",      "Default Character Style"},
        {"Code",            "Default Character Style"},
    };

    for (const auto &def : charDefaults) {
        QString styleName = QString::fromLatin1(def.styleName);
        CharacterStyle *s = sm->characterStyle(styleName);
        if (!s) {
            CharacterStyle stub(styleName);
            stub.setParentStyleName(QString::fromLatin1(def.parentName));
            if (styleName == QLatin1String("Emphasis"))
                stub.setFontItalic(true);
            else if (styleName == QLatin1String("Strong"))
                stub.setFontWeight(QFont::Bold);
            else if (styleName == QLatin1String("StrongEmphasis")) {
                stub.setFontWeight(QFont::Bold);
                stub.setFontItalic(true);
            } else if (styleName == QLatin1String("Strikethrough"))
                stub.setFontStrikeOut(true);
            sm->addCharacterStyle(stub);
        } else if (s->parentStyleName().isEmpty()) {
            s->setParentStyleName(QString::fromLatin1(def.parentName));
        }
    }
}

// ---------------------------------------------------------------------------
// resolveAllStyles — flatten style hierarchy for rendering
// ---------------------------------------------------------------------------

void ThemeManager::resolveAllStyles(StyleManager *sm)
{
    QStringList paraNames = sm->paragraphStyleNames();
    for (const QString &name : paraNames) {
        ParagraphStyle resolved = sm->resolvedParagraphStyle(name);
        ParagraphStyle *orig = sm->paragraphStyle(name);
        if (orig) {
            resolved.setParentStyleName(orig->parentStyleName());
            if (orig->headingLevel() > 0)
                resolved.setHeadingLevel(orig->headingLevel());
        }
        sm->addParagraphStyle(resolved);
    }

    QStringList charNames = sm->characterStyleNames();
    for (const QString &name : charNames) {
        CharacterStyle resolved = sm->resolvedCharacterStyle(name);
        CharacterStyle *orig = sm->characterStyle(name);
        if (orig) {
            resolved.setParentStyleName(orig->parentStyleName());
        }
        sm->addCharacterStyle(resolved);
    }
}

// ---------------------------------------------------------------------------
// loadDefaults — hardcoded style hierarchy
// ---------------------------------------------------------------------------

void ThemeManager::loadDefaults(StyleManager *sm)
{
    // Abstract parent styles
    ParagraphStyle dps(QStringLiteral("Default Paragraph Style"));
    dps.setFontFamily(QStringLiteral("Noto Serif"));
    dps.setFontSize(11.0);
    dps.setLineHeightPercent(100);
    dps.setForeground(QColor(0x1a, 0x1a, 0x1a));
    sm->addParagraphStyle(dps);

    ParagraphStyle heading(QStringLiteral("Heading"));
    heading.setParentStyleName(QStringLiteral("Default Paragraph Style"));
    heading.setFontFamily(QStringLiteral("Noto Sans"));
    heading.setFontWeight(QFont::Bold);
    heading.setAlignment(Qt::AlignLeft);
    sm->addParagraphStyle(heading);

    ParagraphStyle body(QStringLiteral("BodyText"));
    body.setParentStyleName(QStringLiteral("Default Paragraph Style"));
    body.setSpaceAfter(6.0);
    sm->addParagraphStyle(body);

    auto makeHeading = [&](const QString &name, int level, qreal size,
                           qreal before, qreal after) {
        ParagraphStyle h(name);
        h.setParentStyleName(QStringLiteral("Heading"));
        h.setFontSize(size);
        h.setSpaceBefore(before);
        h.setSpaceAfter(after);
        h.setHeadingLevel(level);
        sm->addParagraphStyle(h);
    };

    makeHeading(QStringLiteral("Heading1"), 1, 28, 24, 12);
    makeHeading(QStringLiteral("Heading2"), 2, 24, 20, 10);
    makeHeading(QStringLiteral("Heading3"), 3, 20, 16,  8);
    makeHeading(QStringLiteral("Heading4"), 4, 16, 12,  6);
    makeHeading(QStringLiteral("Heading5"), 5, 14, 10,  4);
    makeHeading(QStringLiteral("Heading6"), 6, 12,  8,  4);

    ParagraphStyle codeBlk(QStringLiteral("CodeBlock"));
    codeBlk.setParentStyleName(QStringLiteral("Default Paragraph Style"));
    codeBlk.setBaseCharacterStyleName(QStringLiteral("Code"));
    codeBlk.setBackground(QColor(0xf6, 0xf8, 0xfa));
    sm->addParagraphStyle(codeBlk);

    ParagraphStyle bq(QStringLiteral("BlockQuote"));
    bq.setParentStyleName(QStringLiteral("BodyText"));
    bq.setFontItalic(true);
    bq.setForeground(QColor(0x55, 0x55, 0x55));
    sm->addParagraphStyle(bq);

    ParagraphStyle li(QStringLiteral("ListItem"));
    li.setParentStyleName(QStringLiteral("BodyText"));
    sm->addParagraphStyle(li);

    // Character styles
    CharacterStyle dcs(QStringLiteral("Default Character Style"));
    dcs.setFontFamily(QStringLiteral("Noto Serif"));
    dcs.setFontSize(11.0);
    dcs.setForeground(QColor(0x1a, 0x1a, 0x1a));
    sm->addCharacterStyle(dcs);

    CharacterStyle def(QStringLiteral("DefaultText"));
    def.setParentStyleName(QStringLiteral("Default Character Style"));
    sm->addCharacterStyle(def);

    CharacterStyle codeChar(QStringLiteral("Code"));
    codeChar.setParentStyleName(QStringLiteral("Default Character Style"));
    codeChar.setFontFamily(QStringLiteral("JetBrains Mono"));
    codeChar.setFontSize(10.0);
    sm->addCharacterStyle(codeChar);

    CharacterStyle inlineCode(QStringLiteral("InlineCode"));
    inlineCode.setParentStyleName(QStringLiteral("Code"));
    inlineCode.setForeground(QColor(0xc7, 0x25, 0x4e));
    inlineCode.setBackground(QColor(0xf0, 0xf0, 0xf0));
    sm->addCharacterStyle(inlineCode);

    CharacterStyle link(QStringLiteral("Link"));
    link.setParentStyleName(QStringLiteral("Default Character Style"));
    link.setForeground(QColor(0x03, 0x66, 0xd6));
    link.setFontUnderline(true);
    sm->addCharacterStyle(link);

    CharacterStyle emphasis(QStringLiteral("Emphasis"));
    emphasis.setParentStyleName(QStringLiteral("Default Character Style"));
    emphasis.setFontItalic(true);
    sm->addCharacterStyle(emphasis);

    CharacterStyle strong(QStringLiteral("Strong"));
    strong.setParentStyleName(QStringLiteral("Default Character Style"));
    strong.setFontWeight(QFont::Bold);
    sm->addCharacterStyle(strong);

    CharacterStyle strongEmphasis(QStringLiteral("StrongEmphasis"));
    strongEmphasis.setParentStyleName(QStringLiteral("Default Character Style"));
    strongEmphasis.setFontWeight(QFont::Bold);
    strongEmphasis.setFontItalic(true);
    sm->addCharacterStyle(strongEmphasis);

    CharacterStyle strike(QStringLiteral("Strikethrough"));
    strike.setParentStyleName(QStringLiteral("Default Character Style"));
    strike.setFontStrikeOut(true);
    sm->addCharacterStyle(strike);
}
