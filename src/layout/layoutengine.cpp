/*
 * layoutengine.cpp — Content → box tree, line/page breaking
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "layoutengine.h"
#include "codespancollector.h"
#include "fontmanager.h"
#include "linebreaker.h"
#include "penelopesettings.h"
#include "textshaper.h"


#include <QMarginsF>
#include <QtMath>

#include <unicode/brkiter.h>
#include <unicode/unistr.h>

namespace Layout {

Engine::Engine(FontManager *fontManager, TextShaper *textShaper)
    : m_fontManager(fontManager)
    , m_textShaper(textShaper)
{
}

// --- Main entry point ---

LayoutResult Engine::layout(const Content::Document &doc, const PageLayout &pageLayout)
{
    LayoutResult result;
    result.pageSize = QSizeF(
        pageLayout.pageSizeId == QPageSize::Custom
            ? 595.0  // A4 width fallback
            : QPageSize(pageLayout.pageSizeId).sizePoints().width(),
        pageLayout.pageSizeId == QPageSize::Custom
            ? 842.0  // A4 height fallback
            : QPageSize(pageLayout.pageSizeId).sizePoints().height());

    QSizeF contentSize = pageLayout.contentSizePoints();
    qreal availWidth = contentSize.width();

    // Layout all blocks into page elements
    QList<PageElement> elements;

    for (const auto &block : doc.blocks) {
        std::visit([&](const auto &b) {
            using T = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<T, Content::Paragraph>) {
                // Detect image-only paragraphs (single InlineImage inline)
                if (b.inlines.size() == 1
                    && std::holds_alternative<Content::InlineImage>(b.inlines.first())) {
                    const auto &img = std::get<Content::InlineImage>(b.inlines.first());
                    if (!img.resolvedImageData.isEmpty()) {
                        elements.append(layoutImage(img, availWidth));
                    }
                } else {
                    elements.append(layoutParagraph(b, availWidth));
                }
            } else if constexpr (std::is_same_v<T, Content::Heading>) {
                elements.append(layoutHeading(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
                elements.append(layoutCodeBlock(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
                auto bqElements = layoutBlockQuote(b, availWidth);
                elements.append(bqElements);
            } else if constexpr (std::is_same_v<T, Content::List>) {
                auto listElements = layoutList(b, availWidth);
                elements.append(listElements);
            } else if constexpr (std::is_same_v<T, Content::Table>) {
                elements.append(layoutTable(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::HorizontalRule>) {
                elements.append(layoutHorizontalRule(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::FootnoteSection>) {
                elements.append(layoutFootnoteSection(b, availWidth));
            }
        }, block);
    }

    // Assign elements to pages
    assignToPages(elements, pageLayout, result);

    // Build source map from placed elements (maps page rects to markdown source lines)
    QMarginsF margins = pageLayout.marginsPoints();
    qreal headerOffset = pageLayout.headerTotalHeight();
    for (const auto &page : result.pages) {
        for (const auto &element : page.elements) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, BlockBox>) {
                    if (e.source.startLine > 0) {
                        SourceMapEntry entry;
                        entry.pageNumber = page.pageNumber;
                        entry.rect = QRectF(margins.left() + e.x,
                                            margins.top() + headerOffset + e.y,
                                            e.width, e.height);
                        entry.startLine = e.source.startLine;
                        entry.endLine = e.source.endLine;
                        result.sourceMap.append(entry);

                        // Code block hit regions (includes padding for generous hit area)
                        if (e.type == BlockBox::CodeBlockType) {
                            CodeBlockRegion region;
                            region.pageNumber = page.pageNumber;
                            region.rect = QRectF(
                                margins.left() + e.x - e.padding,
                                margins.top() + headerOffset + e.y - e.padding,
                                e.width + e.padding * 2,
                                e.height + e.padding * 2);
                            region.startLine = e.source.startLine;
                            region.endLine = e.source.endLine;
                            result.codeBlockRegions.append(region);
                        }
                    }
                } else if constexpr (std::is_same_v<T, TableBox>) {
                    if (e.source.startLine > 0) {
                        SourceMapEntry entry;
                        entry.pageNumber = page.pageNumber;
                        entry.rect = QRectF(margins.left() + e.x,
                                            margins.top() + headerOffset + e.y,
                                            e.width, e.height);
                        entry.startLine = e.source.startLine;
                        entry.endLine = e.source.endLine;
                        result.sourceMap.append(entry);
                    }
                }
            }, element);
        }
    }

    return result;
}

// --- Continuous (web-view) layout ---

ContinuousLayoutResult Engine::layoutContinuous(const Content::Document &doc, qreal availWidth)
{
    ContinuousLayoutResult result;
    result.contentWidth = availWidth;

    // Phase 1: layout all blocks into page elements (identical to layout())
    QList<PageElement> elements;

    for (const auto &block : doc.blocks) {
        std::visit([&](const auto &b) {
            using T = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<T, Content::Paragraph>) {
                // Detect image-only paragraphs (single InlineImage inline)
                if (b.inlines.size() == 1
                    && std::holds_alternative<Content::InlineImage>(b.inlines.first())) {
                    const auto &img = std::get<Content::InlineImage>(b.inlines.first());
                    if (!img.resolvedImageData.isEmpty()) {
                        elements.append(layoutImage(img, availWidth));
                    }
                } else {
                    elements.append(layoutParagraph(b, availWidth));
                }
            } else if constexpr (std::is_same_v<T, Content::Heading>) {
                elements.append(layoutHeading(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
                elements.append(layoutCodeBlock(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
                auto bqElements = layoutBlockQuote(b, availWidth);
                elements.append(bqElements);
            } else if constexpr (std::is_same_v<T, Content::List>) {
                auto listElements = layoutList(b, availWidth);
                elements.append(listElements);
            } else if constexpr (std::is_same_v<T, Content::Table>) {
                elements.append(layoutTable(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::HorizontalRule>) {
                elements.append(layoutHorizontalRule(b, availWidth));
            } else if constexpr (std::is_same_v<T, Content::FootnoteSection>) {
                elements.append(layoutFootnoteSection(b, availWidth));
            }
        }, block);
    }

    // Phase 2: simple vertical stacking (no page breaks, no splitting)
    qreal y = 0;

    for (auto &element : elements) {
        std::visit([&](auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox>) {
                y += e.spaceBefore;
                e.y = y;
                y += e.height + e.spaceAfter;

                // Build source map entry
                if (e.source.startLine > 0) {
                    SourceMapEntry entry;
                    entry.pageNumber = 0;
                    entry.rect = QRectF(e.x, e.y, e.width, e.height);
                    entry.startLine = e.source.startLine;
                    entry.endLine = e.source.endLine;
                    result.sourceMap.append(entry);

                    // Code block hit regions
                    if (e.type == BlockBox::CodeBlockType) {
                        CodeBlockRegion region;
                        region.pageNumber = 0;
                        region.rect = QRectF(
                            e.x - e.padding,
                            e.y - e.padding,
                            e.width + e.padding * 2,
                            e.height + e.padding * 2);
                        region.startLine = e.source.startLine;
                        region.endLine = e.source.endLine;
                        result.codeBlockRegions.append(region);
                    }
                }
            } else if constexpr (std::is_same_v<T, TableBox>) {
                e.y = y;
                y += e.height;

                // Build source map entry
                if (e.source.startLine > 0) {
                    SourceMapEntry entry;
                    entry.pageNumber = 0;
                    entry.rect = QRectF(e.x, e.y, e.width, e.height);
                    entry.startLine = e.source.startLine;
                    entry.endLine = e.source.endLine;
                    result.sourceMap.append(entry);
                }
            } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                y += 20.0; // spaceBefore for footnote sections
                e.y = y;
                y += e.height;
            }
        }, element);
    }

    result.elements = std::move(elements);
    result.totalHeight = y;

    return result;
}

// --- Inline text collection and shaping ---

namespace {

struct MarkdownRange {
    int textStart;       // position in collected text
    int textEnd;         // position + length
    QString prefix;      // markdown opening syntax
    QString suffix;      // markdown closing syntax
};

// Collect all text and style runs from inline nodes
struct CollectedText {
    QString text;
    QList<StyleRun> styleRuns;
    QList<Content::TextStyle> textStyles; // rendering styles, parallel to styleRuns
    QSet<int> softHyphenPositions;        // cleaned-text positions at soft hyphens
    QList<MarkdownRange> markdownRanges;
};

// Strip U+00AD (soft hyphen) from text, recording their cleaned-text positions
QString stripSoftHyphens(const QString &text, int offset, QSet<int> &positions)
{
    QString clean;
    clean.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QChar(0x00AD))
            positions.insert(offset + clean.size());
        else
            clean.append(text[i]);
    }
    return clean;
}

CollectedText collectInlines(const QList<Content::InlineNode> &inlines,
                              const Content::TextStyle &baseStyle,
                              bool markdownMode = false)
{
    CollectedText result;
    for (const auto &node : inlines) {
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>) {
                int startPos = result.text.size();
                QString clean = stripSoftHyphens(n.text, startPos, result.softHyphenPositions);
                StyleRun sr;
                sr.start = startPos;
                sr.length = clean.size();
                sr.fontFamily = n.style.fontFamily;
                sr.fontWeight = n.style.fontWeight;
                sr.fontItalic = n.style.italic;
                sr.fontSize = n.style.fontSize;
                sr.fontFeatures = n.style.fontFeatures;
                result.styleRuns.append(sr);
                result.textStyles.append(n.style);
                result.text.append(clean);
                if (markdownMode) {
                    QString prefix, suffix;
                    // Only mark bold/italic if it differs from the base style
                    // (headings are inherently bold — no redundant ** needed)
                    bool bold = (n.style.fontWeight >= 700) && !(baseStyle.fontWeight >= 700);
                    bool italic = n.style.italic && !baseStyle.italic;
                    bool strike = n.style.strikethrough;
                    if (bold && italic) { prefix += QStringLiteral("***"); suffix.prepend(QStringLiteral("***")); }
                    else if (bold)      { prefix += QStringLiteral("**");  suffix.prepend(QStringLiteral("**")); }
                    else if (italic)    { prefix += QStringLiteral("*");   suffix.prepend(QStringLiteral("*")); }
                    if (strike)         { prefix += QStringLiteral("~~");  suffix.prepend(QStringLiteral("~~")); }
                    if (!prefix.isEmpty())
                        result.markdownRanges.append({startPos, startPos + static_cast<int>(clean.size()), prefix, suffix});
                }
            } else if constexpr (std::is_same_v<T, Content::InlineCode>) {
                StyleRun sr;
                sr.start = result.text.size();
                sr.length = n.text.size();
                sr.fontFamily = n.style.fontFamily;
                sr.fontWeight = n.style.fontWeight;
                sr.fontItalic = n.style.italic;
                sr.fontSize = n.style.fontSize;
                sr.fontFeatures = n.style.fontFeatures;
                result.styleRuns.append(sr);
                result.textStyles.append(n.style);
                result.text.append(n.text);
                if (markdownMode)
                    result.markdownRanges.append({sr.start, sr.start + sr.length,
                                                   QStringLiteral("`"), QStringLiteral("`")});
            } else if constexpr (std::is_same_v<T, Content::FootnoteRef>) {
                StyleRun sr;
                sr.start = result.text.size();
                sr.length = n.label.size();
                sr.fontFamily = n.style.fontFamily;
                sr.fontWeight = n.style.fontWeight;
                sr.fontItalic = n.style.italic;
                sr.fontSize = n.style.fontSize;
                sr.fontFeatures = n.style.fontFeatures;
                result.styleRuns.append(sr);
                result.textStyles.append(n.style);
                result.text.append(n.label);
            } else if constexpr (std::is_same_v<T, Content::SoftBreak>) {
                // Treat as space
                StyleRun sr;
                sr.start = result.text.size();
                sr.length = 1;
                sr.fontFamily = baseStyle.fontFamily;
                sr.fontWeight = baseStyle.fontWeight;
                sr.fontItalic = baseStyle.italic;
                sr.fontSize = baseStyle.fontSize;
                result.styleRuns.append(sr);
                result.textStyles.append(baseStyle);
                result.text.append(QChar(' '));
            } else if constexpr (std::is_same_v<T, Content::HardBreak>) {
                // Append newline — handled during line breaking
                StyleRun sr;
                sr.start = result.text.size();
                sr.length = 1;
                sr.fontFamily = baseStyle.fontFamily;
                sr.fontWeight = baseStyle.fontWeight;
                sr.fontItalic = baseStyle.italic;
                sr.fontSize = baseStyle.fontSize;
                result.styleRuns.append(sr);
                result.textStyles.append(baseStyle);
                result.text.append(QChar('\n'));
            } else if constexpr (std::is_same_v<T, Content::Link>) {
                int startPos = result.text.size();
                QString clean = stripSoftHyphens(n.text, startPos, result.softHyphenPositions);
                StyleRun sr;
                sr.start = startPos;
                sr.length = clean.size();
                sr.fontFamily = n.style.fontFamily;
                sr.fontWeight = n.style.fontWeight;
                sr.fontItalic = n.style.italic;
                sr.fontSize = n.style.fontSize;
                result.styleRuns.append(sr);
                result.textStyles.append(n.style);
                result.text.append(clean);
                if (markdownMode)
                    result.markdownRanges.append({startPos, startPos + static_cast<int>(clean.size()),
                                                   QStringLiteral("["),
                                                   QStringLiteral("](") + n.href + QStringLiteral(")")});
            } else if constexpr (std::is_same_v<T, Content::InlineImage>) {
                // Placeholder for inline images — represented as object replacement
                // Images are handled separately during rendering
            }
        }, node);
    }
    return result;
}

// Resolve the Content::TextStyle for a character position in collected text
Content::TextStyle resolveStyleAt(int charPos, const CollectedText &collected,
                                   const Content::TextStyle &baseStyle)
{
    for (int i = 0; i < collected.styleRuns.size(); ++i) {
        const auto &sr = collected.styleRuns[i];
        if (sr.start <= charPos && sr.start + sr.length > charPos)
            return collected.textStyles[i];
    }
    return baseStyle;
}

// Measure the trailing space width of a word box by checking its last glyph
static qreal trailingSpaceWidth(const GlyphBox &gbox, const QString &text)
{
    if (gbox.glyphs.isEmpty())
        return 0;
    int cluster = gbox.glyphs.last().cluster;
    if (cluster < text.size() && text[cluster].isSpace())
        return gbox.glyphs.last().xAdvance;
    return 0;
}

// Forward declaration of WordBox (defined inside breakIntoLines)
struct WordBox {
    GlyphBox gbox;
    bool isNewline = false;
};

// Convert word boxes into Knuth-Plass items (boxes, glue, penalties)
static QList<LineBreaking::Item> buildKPItems(
    const QList<WordBox> &words,
    const QString &text,
    const Content::ParagraphFormat &format,
    qreal defaultSpaceWidth)
{
    QList<LineBreaking::Item> items;
    items.reserve(words.size() * 3);

    for (int i = 0; i < words.size(); ++i) {
        const auto &word = words[i];

        if (word.isNewline) {
            // Forced break: penalty of -infinity
            items.append(LineBreaking::Item::makePenalty(0, -1e7));
            continue;
        }

        // Measure trailing space in this word (will become glue)
        qreal spaceW = trailingSpaceWidth(word.gbox, text);
        qreal contentWidth = word.gbox.width - spaceW;

        // Box: the word content (without trailing space)
        items.append(LineBreaking::Item::makeBox(contentWidth, i));

        // After each word (except the last and before newlines):
        if (i + 1 < words.size() && !words[i + 1].isNewline) {
            // If next box is attached (mid-word style change like "Office" + "."),
            // emit no glue — consecutive boxes with no break opportunity.
            if (words[i + 1].gbox.attachedToPrevious)
                continue;

            // Non-breaking space: infinite penalty prevents line break,
            // but the following glue still stretches during justification.
            if (word.gbox.trailingNbsp) {
                items.append(LineBreaking::Item::makePenalty(0, 10000));
            }

            // Soft hyphen break opportunity
            if (word.gbox.trailingSoftHyphen) {
                // Hyphen width estimate: ~60% of space width
                qreal hyphenW = (spaceW > 0 ? spaceW : defaultSpaceWidth) * 0.6;
                items.append(LineBreaking::Item::makePenalty(hyphenW, 50.0, true));
            }

            // Inter-word glue
            qreal glueWidth = (spaceW > 0) ? spaceW : defaultSpaceWidth;
            if (format.alignment == Qt::AlignJustify) {
                items.append(LineBreaking::Item::makeGlue(
                    glueWidth,
                    glueWidth * 0.5,   // 50% stretch
                    glueWidth * 0.33)); // 33% shrink
            } else {
                items.append(LineBreaking::Item::makeGlue(glueWidth, 0, 0));
            }
        }
    }

    // Forced final break
    items.append(LineBreaking::Item::makePenalty(0, -1e7));

    return items;
}

} // anonymous namespace

// --- Line breaking ---

QList<LineBox> Engine::breakIntoLines(const QList<Content::InlineNode> &inlines,
                                       const Content::TextStyle &baseStyle,
                                       const Content::ParagraphFormat &format,
                                       qreal availWidth,
                                       bool markdownRanges)
{
    QList<LineBox> lines;
    // For code blocks: still set glyph text fields (m_markdownDecorations)
    // but suppress markdown range creation (bold/italic from syntax highlighting
    // should not produce ** markers in the ActualText).
    auto collected = collectInlines(inlines, baseStyle,
                                    m_markdownDecorations && markdownRanges);
    if (collected.text.isEmpty())
        return lines;

    // Shape all text
    QList<ShapedRun> shapedRuns = m_textShaper->shape(collected.text, collected.styleRuns);

    // Use ICU BreakIterator to find line break opportunities
    UErrorCode err = U_ZERO_ERROR;
    icu::UnicodeString ustr(reinterpret_cast<const UChar *>(collected.text.utf16()),
                            collected.text.length());
    std::unique_ptr<icu::BreakIterator> lineBreakIter(
        icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err));
    if (U_FAILURE(err))
        lineBreakIter.reset();

    QSet<int> breakPositions;
    if (lineBreakIter) {
        lineBreakIter->setText(ustr);
        for (int32_t pos = lineBreakIter->first();
             pos != icu::BreakIterator::DONE;
             pos = lineBreakIter->next()) {
            breakPositions.insert(pos);
        }
    }

    // Add soft hyphen positions as additional break opportunities.
    // When HyphenateJustifiedText is off, skip them for justified paragraphs
    // so that justify relies only on word boundaries (wider gaps, no mid-word breaks).
    bool useSoftHyphens = !collected.softHyphenPositions.isEmpty();
    if (format.alignment == Qt::AlignJustify && !m_hyphenateJustifiedText)
        useSoftHyphens = false;
    if (useSoftHyphens)
        breakPositions.unite(collected.softHyphenPositions);

    // Add non-breaking space (U+00A0) positions as split points.
    // ICU's line-break iterator doesn't break at nbsp (by design), but we
    // need separate glyph boxes so that justification can stretch the gap.
    // The actual no-break constraint is enforced by an infinite penalty in
    // the Knuth-Plass item list (see buildKPItems).
    QSet<int> nbspSplitPositions;
    for (int ci = 0; ci < collected.text.size(); ++ci) {
        if (collected.text[ci] == QChar(0x00A0) && ci + 1 < collected.text.size())
            nbspSplitPositions.insert(ci + 1);
    }
    breakPositions.unite(nbspSplitPositions);

    // Build word-level glyph boxes by splitting shaped runs at break points.
    // Each shaped run may span multiple words. Using HarfBuzz cluster info
    // (character index per glyph), we split at ICU break positions and newlines.
    QList<WordBox> words;

    for (const auto &run : shapedRuns) {
        // Resolve rendering style for this run
        Content::TextStyle runStyle = resolveStyleAt(run.textStart, collected, baseStyle);

        // Template for new glyph boxes from this run
        auto newGlyphBox = [&]() -> GlyphBox {
            GlyphBox gb;
            gb.font = run.font;
            gb.fontSize = run.fontSize;
            gb.style = runStyle;
            gb.rtl = run.rtl;
            gb.textStart = run.textStart;
            gb.textLength = 0;
            if (run.font) {
                gb.ascent = m_fontManager->ascent(run.font, run.fontSize);
                gb.descent = m_fontManager->descent(run.font, run.fontSize);
            } else {
                gb.ascent = run.fontSize * 0.8;
                gb.descent = run.fontSize * 0.2;
            }
            return gb;
        };

        GlyphBox currentWord = newGlyphBox();

        for (int gi = 0; gi < run.glyphs.size(); ++gi) {
            const auto &sg = run.glyphs[gi];
            int charPos = sg.cluster;

            // Check for newline character
            if (charPos < collected.text.size() && collected.text[charPos] == '\n') {
                // Finalize current word
                if (!currentWord.glyphs.isEmpty()) {
                    words.append({currentWord, false});
                    currentWord = newGlyphBox();
                }
                // Insert newline marker
                words.append({GlyphBox{}, true});
                continue;
            }

            // Check if this glyph starts at a line break opportunity
            if (breakPositions.contains(charPos) && !currentWord.glyphs.isEmpty()) {
                bool isSoftHyphenBreak = collected.softHyphenPositions.contains(charPos);
                bool isNbspBreak = nbspSplitPositions.contains(charPos);
                if (isSoftHyphenBreak)
                    currentWord.trailingSoftHyphen = true;
                if (isNbspBreak)
                    currentWord.trailingNbsp = true;
                // Finalize current word and start a new one
                words.append({currentWord, false});
                currentWord = newGlyphBox();
                currentWord.textStart = charPos;
                if (isSoftHyphenBreak)
                    currentWord.startsAfterSoftHyphen = true;
            }

            // Add glyph to current word
            GlyphInfo info;
            info.glyphId = sg.glyphId;
            info.xAdvance = sg.xAdvance;
            info.yAdvance = sg.yAdvance;
            info.xOffset = sg.xOffset;
            info.yOffset = sg.yOffset;
            info.cluster = sg.cluster;
            currentWord.glyphs.append(info);
            currentWord.width += sg.xAdvance;
            currentWord.textLength = charPos - currentWord.textStart + 1;
        }

        // Finalize last word of run
        if (!currentWord.glyphs.isEmpty())
            words.append({currentWord, false});
    }

    // Detect word boxes that are text-adjacent with no inter-word space
    // (e.g. "Office" in italic + "." in normal from `*Office*.`).
    // These must not have glue or justify gaps between them.
    for (int wi = 1; wi < words.size(); ++wi) {
        if (words[wi].isNewline || words[wi - 1].isNewline)
            continue;
        const auto &prev = words[wi - 1].gbox;
        const auto &cur = words[wi].gbox;
        // Check if text ranges are adjacent (no gap) and previous has no trailing space
        if (prev.textStart + prev.textLength == cur.textStart
            && trailingSpaceWidth(prev, collected.text) == 0) {
            words[wi].gbox.attachedToPrevious = true;
        }
    }

    // Store original text content in each glyph box (for ActualText in PDF)
    if (m_markdownDecorations) {
        for (auto &w : words) {
            if (w.isNewline) continue;
            w.gbox.text = collected.text.mid(w.gbox.textStart, w.gbox.textLength);
        }
    }

    // Apply markdown decorations to glyph boxes
    if (m_markdownDecorations && !collected.markdownRanges.isEmpty()) {
        for (const auto &range : collected.markdownRanges) {
            GlyphBox *first = nullptr;
            GlyphBox *last = nullptr;
            for (auto &w : words) {
                if (w.isNewline) continue;
                auto &gb = w.gbox;
                int gbEnd = gb.textStart + gb.textLength;
                // Check if this glyph box overlaps the markdown range
                if (gbEnd > range.textStart && gb.textStart < range.textEnd) {
                    if (!first) first = &gb;
                    last = &gb;
                }
            }
            if (first) first->mdPrefix += range.prefix;
            if (last) last->mdSuffix.prepend(range.suffix);
        }
    }

    // --- Line breaking ---
    qreal firstLineIndent = format.firstLineIndent;
    qreal firstLineWidth = availWidth - firstLineIndent;

    // Measure natural inter-word space width from shaped text
    qreal naturalSpaceWidth = baseStyle.fontSize * 0.25; // fallback
    for (const auto &run : shapedRuns) {
        for (const auto &g : run.glyphs) {
            if (g.cluster < collected.text.size()
                && collected.text[g.cluster] == QLatin1Char(' ')
                && g.xAdvance > 0) {
                naturalSpaceWidth = g.xAdvance;
                goto foundSpace;
            }
        }
    }
    foundSpace:

    // Build line widths (first line may be narrower due to indent)
    QList<qreal> kpLineWidths;
    kpLineWidths.append(firstLineWidth);
    kpLineWidths.append(availWidth);

    // Convert word boxes to Knuth-Plass items
    auto kpItems = buildKPItems(words, collected.text, format, naturalSpaceWidth);

    // Configure and run Knuth-Plass (only for justified text — other alignments
    // don't need optimal line breaking and KP fails with zero-stretch glue)
    bool useKP = (format.alignment == Qt::AlignJustify);
    LineBreaking::Config kpConfig;
    kpConfig.enableHyphenation = m_hyphenateJustifiedText;
    LineBreaking::BreakResult kpResult;

    if (useKP) {
        kpResult = LineBreaking::findBreaksTiered(kpItems, kpLineWidths, kpConfig);
    }

    if (useKP && kpResult.optimal && !kpResult.breaks.isEmpty()) {
        // Build lines from Knuth-Plass breakpoints
        int wordStart = 0;
        for (int bi = 0; bi < kpResult.breaks.size(); ++bi) {
            const auto &bp = kpResult.breaks[bi];
            LineBox line;
            line.alignment = format.alignment;

            // Find the last word index for this line by scanning items before the break.
            // bp.itemIndex is one past the break item, so scan from bp.itemIndex - 1
            // (the break item itself).  For a glue break, the glue is skipped and we
            // find the last Box before it.  For a penalty break, same logic applies.
            int lastBoxWordIdx = -1;
            for (int ii = qMin(bp.itemIndex - 1, kpItems.size() - 1); ii >= 0; --ii) {
                if (kpItems[ii].type == LineBreaking::Item::BoxType) {
                    lastBoxWordIdx = kpItems[ii].box.wordIndex;
                    break;
                }
            }
            if (lastBoxWordIdx < 0)
                lastBoxWordIdx = wordStart; // safety

            // Collect word boxes for this line
            int charCount = 0;
            int gapCount = 0;
            for (int wi = wordStart; wi < words.size() && wi <= lastBoxWordIdx; ++wi) {
                if (words[wi].isNewline) continue;
                if (!line.glyphs.isEmpty()) {
                    // Count eligible justify gaps
                    if (!shouldSkipJustifyGap(line.glyphs.last(), words[wi].gbox))
                        gapCount++;
                }
                line.glyphs.append(words[wi].gbox);
                charCount += words[wi].gbox.glyphs.size();
            }

            // Check if break is at a flagged penalty (soft-hyphen)
            if (bp.itemIndex < kpItems.size()
                && kpItems[bp.itemIndex].type == LineBreaking::Item::PenaltyType
                && kpItems[bp.itemIndex].penalty.flagged) {
                line.showTrailingHyphen = true;
            }

            // Mark last line
            if (bi == kpResult.breaks.size() - 1)
                line.isLastLine = true;

            // Forced newline breaks also mark last-of-paragraph-segment
            if (bp.itemIndex < kpItems.size()
                && kpItems[bp.itemIndex].type == LineBreaking::Item::PenaltyType
                && kpItems[bp.itemIndex].penalty.penalty <= -1e6)
                line.isLastLine = true;

            // Skip empty lines (can happen if all words in range are newlines)
            if (line.glyphs.isEmpty()) {
                wordStart = lastBoxWordIdx + 1;
                while (wordStart < words.size() && words[wordStart].isNewline)
                    wordStart++;
                continue;
            }

            lines.append(line);
            wordStart = lastBoxWordIdx + 1;

            // Skip past any newline words
            while (wordStart < words.size() && words[wordStart].isNewline)
                wordStart++;
        }
    } else {
        // Greedy fallback (original algorithm)
        qreal lineWidth = availWidth - firstLineIndent;
        qreal currentX = 0;
        LineBox currentLine;
        currentLine.alignment = format.alignment;
        bool isFirstLine = true;

        for (int i = 0; i < words.size(); ++i) {
            const auto &word = words[i];

            if (word.isNewline) {
                currentLine.isLastLine = true;
                lines.append(currentLine);
                currentLine = LineBox{};
                currentLine.alignment = format.alignment;
                currentX = 0;
                if (isFirstLine) {
                    isFirstLine = false;
                    lineWidth = availWidth;
                }
                continue;
            }

            if (currentX + word.gbox.width > lineWidth && !currentLine.glyphs.isEmpty()) {
                if (!currentLine.glyphs.isEmpty() && currentLine.glyphs.last().trailingSoftHyphen)
                    currentLine.showTrailingHyphen = true;
                lines.append(currentLine);
                currentLine = LineBox{};
                currentLine.alignment = format.alignment;
                currentX = 0;
                if (isFirstLine) {
                    isFirstLine = false;
                    lineWidth = availWidth;
                }
            }

            if (word.gbox.width > lineWidth && currentLine.glyphs.isEmpty()) {
                GlyphBox part = word.gbox;
                part.glyphs.clear();
                part.width = 0;
                part.textLength = 0;
                for (const auto &glyph : word.gbox.glyphs) {
                    if (part.width + glyph.xAdvance > lineWidth && !part.glyphs.isEmpty()) {
                        currentLine.glyphs.append(part);
                        lines.append(currentLine);
                        currentLine = LineBox{};
                        currentLine.alignment = format.alignment;
                        part.glyphs.clear();
                        part.width = 0;
                        part.textLength = 0;
                    }
                    part.glyphs.append(glyph);
                    part.width += glyph.xAdvance;
                    part.textLength++;
                }
                if (!part.glyphs.isEmpty()) {
                    currentLine.glyphs.append(part);
                    currentX = part.width;
                }
                continue;
            }

            currentLine.glyphs.append(word.gbox);
            currentX += word.gbox.width;
        }

        if (!currentLine.glyphs.isEmpty())
            lines.append(currentLine);

        if (!lines.isEmpty())
            lines.last().isLastLine = true;
    }

    // Trim trailing whitespace glyphs from non-last lines.
    // ICU break positions place spaces at the end of the preceding word.
    // For justify, this trailing space inflates line.width and prevents the
    // last visible character from reaching the right margin.
    for (int li = 0; li < lines.size(); ++li) {
        if (lines[li].isLastLine || lines[li].glyphs.isEmpty())
            continue;
        auto &lastBox = lines[li].glyphs.last();
        while (!lastBox.glyphs.isEmpty()) {
            int cluster = lastBox.glyphs.last().cluster;
            if (cluster < collected.text.size() && collected.text[cluster].isSpace()) {
                lastBox.width -= lastBox.glyphs.last().xAdvance;
                lastBox.textLength--;
                lastBox.glyphs.removeLast();
            } else {
                break;
            }
        }
        // Remove the glyph box entirely if trimming emptied it,
        // otherwise justify counts it as a gap recipient
        if (lastBox.glyphs.isEmpty())
            lines[li].glyphs.removeLast();
    }

    // Compute line metrics
    for (auto &line : lines) {
        qreal maxAscent = 0;
        qreal maxDescent = 0;
        qreal totalWidth = 0;
        for (const auto &g : line.glyphs) {
            maxAscent = qMax(maxAscent, g.ascent);
            maxDescent = qMax(maxDescent, g.descent);
            totalWidth += g.width;
        }
        line.width = totalWidth;
        // Empty lines (e.g., blank lines in code) get minimum height from base style
        if (maxAscent + maxDescent < 1.0) {
            maxAscent = baseStyle.fontSize * 0.8;
            maxDescent = baseStyle.fontSize * 0.2;
        }
        line.baseline = maxAscent;
        line.height = maxAscent + maxDescent;
    }

    // Compute JustifyInfo from actual shortfall (after trimming, so line.width
    // is accurate).  This decouples spacing from the KP adjustment ratio, which
    // doesn't account for glyph-box trailing-space mismatches.
    if (format.alignment == Qt::AlignJustify) {
        qreal maxLetterFrac = 0.03;
        qreal minLetterFrac = -0.02;
        for (auto &line : lines) {
            if (line.isLastLine || line.glyphs.size() <= 1)
                continue;
            qreal shortfall = availWidth - line.width;
            if (shortfall <= 0)
                continue; // already full or overfull

            // Count eligible gaps and characters
            int gapCount = 0;
            int charCount = 0;
            for (int i = 0; i < line.glyphs.size(); ++i) {
                charCount += line.glyphs[i].glyphs.size();
                if (i > 0) {
                    if (!shouldSkipJustifyGap(line.glyphs[i - 1], line.glyphs[i]))
                        gapCount++;
                }
            }
            if (gapCount <= 0)
                continue;

            // Letter spacing is applied between glyph boxes, so the last
            // box's characters don't benefit (spacing after the last box
            // would go past the right margin).  Exclude them.
            int lastBoxChars = line.glyphs.last().glyphs.size();
            int letterCharCount = charCount - lastBoxChars;

            // Split shortfall: 2/3 word spacing, 1/3 letter spacing
            qreal letterSlack = shortfall * (1.0 / 3.0);
            qreal letterPerChar = (letterCharCount > 0) ? letterSlack / letterCharCount : 0;

            // Clamp letter spacing
            qreal maxLS = maxLetterFrac * baseStyle.fontSize;
            qreal minLS = minLetterFrac * baseStyle.fontSize;
            letterPerChar = qBound(minLS, letterPerChar, maxLS);

            // Whatever letter spacing doesn't absorb goes to word spacing
            qreal actualLetterSlack = letterPerChar * letterCharCount;
            qreal wordSlack = shortfall - actualLetterSlack;

            line.justify.wordGapCount = gapCount;
            line.justify.charCount = charCount;
            line.justify.extraWordSpacing = wordSlack / gapCount;
            line.justify.extraLetterSpacing = letterPerChar;

        }
    }

    // Apply line height multiplier
    qreal lineHeightMult = format.lineHeightPercent / 100.0;
    for (auto &line : lines) {
        line.height *= lineHeightMult;
    }

    return lines;
}

// --- Block layout ---

BlockBox Engine::layoutParagraph(const Content::Paragraph &para, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::ParagraphBlock;
    box.spaceBefore = para.format.spaceBefore;
    box.spaceAfter = para.format.spaceAfter;

    // Resolve base style from format
    Content::TextStyle baseStyle;
    if (!para.inlines.isEmpty()) {
        // Use style from first inline
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>
                          || std::is_same_v<T, Content::InlineCode>
                          || std::is_same_v<T, Content::FootnoteRef>) {
                baseStyle = n.style;
            }
        }, para.inlines.first());
    }

    // For markdown decoration, the base style must be the paragraph's "normal"
    // style (not bold/italic) so that inline bold/italic inlines get ** / *
    // markers even if the paragraph happens to start with formatted text.
    // (Headings handle this differently — layoutHeading passes the heading's
    // inherently bold base style so heading-level bold is correctly suppressed.)
    if (m_markdownDecorations) {
        baseStyle.fontWeight = 400;
        baseStyle.italic = false;
    }

    qreal effectiveWidth = availWidth - para.format.leftMargin - para.format.rightMargin;
    box.lines = breakIntoLines(para.inlines, baseStyle, para.format, effectiveWidth);
    box.width = effectiveWidth;

    // Compute total height
    qreal h = 0;
    for (const auto &line : box.lines)
        h += line.height;
    box.height = h;
    box.x = para.format.leftMargin;
    box.firstLineIndent = para.format.firstLineIndent;
    box.source = para.source;

    return box;
}

BlockBox Engine::layoutHeading(const Content::Heading &heading, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::HeadingBlock;
    box.headingLevel = heading.level;
    box.width = availWidth;
    box.spaceBefore = heading.format.spaceBefore;
    box.spaceAfter = heading.format.spaceAfter;

    Content::TextStyle baseStyle;
    if (!heading.inlines.isEmpty()) {
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>) {
                baseStyle = n.style;
            }
        }, heading.inlines.first());
    }

    box.lines = breakIntoLines(heading.inlines, baseStyle, heading.format, availWidth);

    if (m_markdownDecorations && !box.lines.isEmpty() && !box.lines.first().glyphs.isEmpty()) {
        QString headingPrefix = QString(heading.level, QLatin1Char('#')) + QLatin1Char(' ');
        box.lines.first().glyphs.first().mdPrefix.prepend(headingPrefix);
    }

    box.keepWithNext = true;

    // Extract heading text for PDF bookmarks
    for (const auto &node : heading.inlines) {
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>)
                box.headingText += n.text;
            else if constexpr (std::is_same_v<T, Content::InlineCode>)
                box.headingText += n.text;
            else if constexpr (std::is_same_v<T, Content::Link>)
                box.headingText += n.text;
        }, node);
    }

    qreal h = 0;
    for (const auto &line : box.lines)
        h += line.height;
    box.height = h;
    box.source = heading.source;

    return box;
}

BlockBox Engine::layoutCodeBlock(const Content::CodeBlock &cb, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::CodeBlockType;
    box.width = availWidth - 24.0; // subtract left + right margins (12pt each)
    box.padding = cb.padding;
    box.background = cb.background;
    box.borderColor = QColor(0xe1, 0xe4, 0xe8);
    box.borderWidth = 0.5;
    box.codeLanguage = cb.language;
    box.codeFenced = cb.isFenced;
    box.spaceBefore = 6.0;
    box.spaceAfter = 10.0;

    QList<Content::InlineNode> inlines;

    // Try syntax highlighting if language is known
    if (!cb.language.isEmpty()) {
        CodeSpanCollector collector;
        auto spans = collector.highlight(cb.code, cb.language);

        if (!spans.isEmpty()) {
            // Sort spans by start position
            std::sort(spans.begin(), spans.end(),
                      [](const auto &a, const auto &b) { return a.start < b.start; });

            int lastEnd = 0;
            for (const auto &span : spans) {
                // Gap between spans → default style
                if (span.start > lastEnd) {
                    Content::TextRun run;
                    run.text = cb.code.mid(lastEnd, span.start - lastEnd);
                    run.style = cb.style;
                    inlines.append(run);
                }
                // Highlighted span
                Content::TextRun run;
                run.text = cb.code.mid(span.start, span.length);
                run.style = cb.style; // base monospace style
                if (span.foreground.isValid())
                    run.style.foreground = span.foreground;
                if (span.background.isValid())
                    run.style.background = span.background;
                if (span.bold)
                    run.style.fontWeight = 700;
                if (span.italic)
                    run.style.italic = true;
                inlines.append(run);
                lastEnd = span.start + span.length;
            }
            // Trailing unstyled text
            if (lastEnd < cb.code.size()) {
                Content::TextRun run;
                run.text = cb.code.mid(lastEnd);
                run.style = cb.style;
                inlines.append(run);
            }
        }
    }

    // Fallback: plain monospace text
    if (inlines.isEmpty()) {
        Content::TextRun run;
        run.text = cb.code;
        run.style = cb.style;
        inlines.append(run);
    }

    Content::ParagraphFormat fmt;
    fmt.lineHeightPercent = cb.lineHeightPercent;
    qreal innerWidth = availWidth - cb.padding * 2 - 24.0; // margins
    box.lines = breakIntoLines(inlines, cb.style, fmt, innerWidth,
                               false /* no markdown ranges for code */);


    qreal h = cb.padding * 2;
    for (const auto &line : box.lines)
        h += line.height;
    box.height = h;
    box.x = 12.0; // match DocumentBuilder margins
    box.source = cb.source;

    return box;
}

BlockBox Engine::layoutHorizontalRule(const Content::HorizontalRule &hr, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::HRuleBlock;
    box.width = availWidth;
    box.height = 1.0;
    box.spaceBefore = hr.topMargin;
    box.spaceAfter = hr.bottomMargin;
    box.source = hr.source;
    return box;
}

// --- Table layout ---

TableBox Engine::layoutTable(const Content::Table &table, qreal availWidth)
{
    TableBox tbox;
    tbox.width = availWidth;
    tbox.borderWidth = table.borderWidth;
    tbox.borderColor = table.borderColor;
    tbox.innerBorderWidth = table.innerBorderWidth;
    tbox.innerBorderColor = table.innerBorderColor;
    tbox.headerBottomBorderWidth = table.headerBottomBorderWidth;
    tbox.headerBottomBorderColor = table.headerBottomBorderColor;
    tbox.cellPadding = table.cellPadding;
    tbox.headerRowCount = table.headerRowCount;

    if (table.rows.isEmpty())
        return tbox;

    int numCols = 0;
    for (const auto &row : table.rows)
        numCols = qMax(numCols, row.cells.size());

    // Content-aware column width distribution
    auto metrics = measureColumnMetrics(table);

    QList<qreal> colWidths;
    if (PenelopeSettings::self()->tableLayoutAlgorithm() == PenelopeSettings::EnumTableLayoutAlgorithm::Optimal)
        colWidths = distributeColumnsOptimal(table, metrics, availWidth);
    else
        colWidths = distributeColumnsAuto(metrics, availWidth);

    // Store column positions for grid drawing
    tbox.columnPositions.append(0);
    for (int i = 0; i < numCols; ++i)
        tbox.columnPositions.append(tbox.columnPositions.last() + colWidths[i]);

    qreal y = 0;
    int rowIdx = 0;
    for (const auto &row : table.rows) {
        TableRowBox rbox;
        rbox.y = y;

        qreal maxCellHeight = 0;
        qreal x = 0;

        for (int ci = 0; ci < row.cells.size(); ++ci) {
            const auto &cell = row.cells[ci];
            TableCellBox cbox;
            cbox.x = x;
            cbox.y = y;
            cbox.width = colWidths[ci];
            cbox.alignment = cell.alignment;
            cbox.isHeader = cell.isHeader;

            // Resolve cell background: explicit cell bg > alternating row > body bg
            if (cell.background.isValid()) {
                cbox.background = cell.background;
            } else if (!cell.isHeader) {
                int bodyRowIdx = rowIdx - table.headerRowCount;
                if (bodyRowIdx >= 0 && (bodyRowIdx % 2) == 1 && table.alternateRowColor.isValid())
                    cbox.background = table.alternateRowColor;
                else if (table.bodyBackground.isValid())
                    cbox.background = table.bodyBackground;
            }

            // Layout cell content
            Content::ParagraphFormat cellFmt;
            cellFmt.alignment = cell.alignment;
            Content::TextStyle cellStyle = cell.style;
            if (cellStyle.fontFamily.isEmpty()) {
                cellStyle.fontFamily = QStringLiteral("Noto Serif");
                cellStyle.fontSize = 11.0;
            }
            qreal innerWidth = colWidths[ci] - table.cellPadding * 2;
            cbox.lines = breakIntoLines(cell.inlines, cellStyle, cellFmt, innerWidth);

            qreal h = table.cellPadding * 2;
            for (const auto &line : cbox.lines)
                h += line.height;
            cbox.height = h;
            maxCellHeight = qMax(maxCellHeight, h);

            rbox.cells.append(cbox);
            x += colWidths[ci];
        }

        // Equalize cell heights
        rbox.height = maxCellHeight;
        for (auto &c : rbox.cells)
            c.height = maxCellHeight;

        tbox.rows.append(rbox);
        y += maxCellHeight;
        rowIdx++;
    }

    tbox.height = y;
    tbox.source = table.source;
    return tbox;
}

// --- Image layout ---

BlockBox Engine::layoutImage(const Content::InlineImage &img, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::ImageBlock;
    box.width = availWidth;
    box.spaceBefore = 6.0;
    box.spaceAfter = 6.0;

    QImage qimg;
    qimg.loadFromData(img.resolvedImageData);
    if (qimg.isNull()) {
        box.height = 0;
        return box;
    }

    // Scale image to fit available width, cap height at 500pt
    static constexpr qreal kMaxImageHeight = 500.0;
    qreal imgW = img.width > 0 ? img.width : qimg.width();
    qreal imgH = img.height > 0 ? img.height : qimg.height();

    if (imgW > availWidth) {
        qreal scale = availWidth / imgW;
        imgW = availWidth;
        imgH *= scale;
    }
    if (imgH > kMaxImageHeight) {
        qreal scale = kMaxImageHeight / imgH;
        imgH = kMaxImageHeight;
        imgW *= scale;
    }

    box.image = qimg.convertToFormat(QImage::Format_RGB888);
    box.imageWidth = imgW;
    box.imageHeight = imgH;
    box.height = imgH;
    // Generate unique image ID based on data hash
    box.imageId = QStringLiteral("Img%1").arg(
        QString::number(qHash(img.resolvedImageData), 16));

    return box;
}

// --- Blockquote layout ---

QList<PageElement> Engine::layoutBlockQuote(const Content::BlockQuote &bq, qreal availWidth)
{
    QList<PageElement> elements;
    qreal indent = bq.level * 16.0;
    qreal innerWidth = availWidth - indent;

    for (const auto &child : bq.children) {
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Content::Paragraph>) {
                auto box = layoutParagraph(c, innerWidth);
                box.x += indent;
                box.hasBlockQuoteBorder = true;
                box.blockQuoteLevel = bq.level;
                box.blockQuoteIndent = indent;
                elements.append(box);
            } else if constexpr (std::is_same_v<T, Content::Heading>) {
                auto box = layoutHeading(c, innerWidth);
                box.x += indent;
                box.hasBlockQuoteBorder = true;
                box.blockQuoteLevel = bq.level;
                box.blockQuoteIndent = indent;
                elements.append(box);
            } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
                auto box = layoutCodeBlock(c, innerWidth);
                box.x += indent;
                box.hasBlockQuoteBorder = true;
                box.blockQuoteLevel = bq.level;
                box.blockQuoteIndent = indent;
                elements.append(box);
            } else if constexpr (std::is_same_v<T, Content::List>) {
                auto listElements = layoutList(c, availWidth - indent);
                for (auto &elem : listElements) {
                    if (auto *bb = std::get_if<BlockBox>(&elem)) {
                        bb->x += indent;
                        bb->hasBlockQuoteBorder = true;
                        bb->blockQuoteLevel = bq.level;
                        bb->blockQuoteIndent = indent;
                    }
                }
                elements.append(listElements);
            } else if constexpr (std::is_same_v<T, Content::Table>) {
                auto tbox = layoutTable(c, innerWidth);
                tbox.x += indent;
                elements.append(tbox);
            } else if constexpr (std::is_same_v<T, Content::HorizontalRule>) {
                auto box = layoutHorizontalRule(c, innerWidth);
                box.x += indent;
                box.hasBlockQuoteBorder = true;
                box.blockQuoteLevel = bq.level;
                box.blockQuoteIndent = indent;
                elements.append(box);
            } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
                // Nested blockquote
                auto nested = layoutBlockQuote(c, availWidth);
                elements.append(nested);
            }
        }, child);
    }

    return elements;
}

// --- List layout ---

QList<PageElement> Engine::layoutList(const Content::List &list, qreal availWidth, int depth)
{
    QList<PageElement> elements;
    qreal indent = 20.0 * (depth + 1);

    for (int i = 0; i < list.items.size(); ++i) {
        const auto &item = list.items[i];
        qreal itemBulletWidth = 0; // track for subsequent paragraphs

        for (const auto &child : item.children) {
            std::visit([&](const auto &c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, Content::Paragraph>) {
                    Content::Paragraph para = c;

                    bool isFirstPara = (&child == &item.children.first())
                                       && !para.inlines.isEmpty();
                    bool isTask = isFirstPara && item.isTask;

                    // Resolve body text style for bullet prefix sizing.
                    // Skip InlineCode to avoid inheriting monospace font.
                    Content::TextStyle firstStyle;
                    if (isFirstPara) {
                        for (const auto &inl : para.inlines) {
                            bool found = false;
                            std::visit([&](const auto &n) {
                                using U = std::decay_t<decltype(n)>;
                                if constexpr (std::is_same_v<U, Content::TextRun>
                                              || std::is_same_v<U, Content::Link>
                                              || std::is_same_v<U, Content::FootnoteRef>) {
                                    firstStyle = n.style;
                                    found = true;
                                }
                            }, inl);
                            if (found)
                                break;
                        }
                        if (firstStyle.fontFamily.isEmpty()) {
                            firstStyle.fontFamily = QStringLiteral("Noto Serif");
                            firstStyle.fontSize = 11.0;
                        }
                    }

                    int prefixLen = 0;
                    if (isFirstPara && !isTask) {
                        // Bullet/number prefix: measure width, then use hanging indent
                        QString prefix;
                        if (list.type == Content::ListType::Ordered)
                            prefix = QString::number(list.startNumber + i) + QStringLiteral(". ");
                        else
                            prefix = QStringLiteral("\u2022 ");
                        prefixLen = prefix.size();

                        Content::TextRun prefixRun;
                        prefixRun.text = prefix;
                        prefixRun.style = firstStyle;

                        QList<Content::InlineNode> prefixInlines;
                        prefixInlines.append(prefixRun);
                        itemBulletWidth = measureInlines(prefixInlines, firstStyle);

                        para.inlines.prepend(prefixRun);
                        para.format.leftMargin += indent + itemBulletWidth;
                        para.format.firstLineIndent = -itemBulletWidth;
                    } else if (isTask) {
                        // Task items: checkbox width as hanging indent
                        qreal cbWidth = firstStyle.fontSize * 0.85 + 3.0;
                        itemBulletWidth = cbWidth;
                        para.format.leftMargin += indent + cbWidth;
                        para.format.firstLineIndent = -cbWidth;
                    } else {
                        // Subsequent paragraph in same list item:
                        // align with continuation text (past bullet)
                        para.format.leftMargin += indent + itemBulletWidth;
                    }

                    auto box = layoutParagraph(para, availWidth);
                    box.isListItem = true;

                    // Mark bullet/number glyph boxes so justify skips them
                    if (prefixLen > 0 && !box.lines.isEmpty()) {
                        for (auto &gb : box.lines.first().glyphs) {
                            if (gb.textStart < prefixLen)
                                gb.isListMarker = true;
                            else
                                break;
                        }
                    }

                    if (isTask && !box.lines.isEmpty()) {
                        GlyphBox cbBox;
                        cbBox.checkboxState = item.taskChecked
                            ? GlyphBox::Checked : GlyphBox::Unchecked;
                        cbBox.fontSize = firstStyle.fontSize;
                        cbBox.style = firstStyle;
                        cbBox.width = itemBulletWidth;
                        cbBox.ascent = firstStyle.fontSize * 0.8;
                        cbBox.descent = firstStyle.fontSize * 0.2;
                        box.lines.first().glyphs.prepend(cbBox);
                        box.lines.first().width += itemBulletWidth;
                    }

                    elements.append(box);
                } else if constexpr (std::is_same_v<T, Content::List>) {
                    // Nested list
                    auto nested = layoutList(c, availWidth, depth + 1);
                    elements.append(nested);
                }
            }, child);
        }
    }

    return elements;
}

// --- Footnote section layout ---

FootnoteSectionBox Engine::layoutFootnoteSection(const Content::FootnoteSection &fs,
                                                  qreal availWidth)
{
    FootnoteSectionBox sbox;
    sbox.width = availWidth;
    sbox.showSeparator = fs.showSeparator;
    sbox.separatorLength = fs.separatorLength;

    qreal y = fs.showSeparator ? 12.0 : 0; // space for separator

    for (const auto &fn : fs.footnotes) {
        FootnoteBox fbox;
        fbox.label = fn.label;
        fbox.numberStyle = fn.numberStyle;
        fbox.y = y;

        // Layout footnote text
        Content::ParagraphFormat fmt;
        fmt.leftMargin = 20.0;
        fmt.firstLineIndent = -20.0;
        fbox.lines = breakIntoLines(fn.content, fn.textStyle, fmt, availWidth - 20.0);

        qreal h = 0;
        for (const auto &line : fbox.lines)
            h += line.height;
        fbox.height = h;

        sbox.footnotes.append(fbox);
        y += h + 2.0; // 2pt gap between footnotes
    }

    sbox.height = y;
    return sbox;
}

// --- Table splitting ---

QList<TableBox> Engine::splitTable(const TableBox &table, qreal availHeight, qreal pageHeight)
{
    QList<TableBox> slices;

    int headerRowCount = table.headerRowCount;

    // Calculate total header height
    qreal headerHeight = 0;
    for (int i = 0; i < headerRowCount && i < table.rows.size(); ++i)
        headerHeight += table.rows[i].height;

    // If headers alone > 50% of page, don't repeat them
    bool repeatHeaders = (headerHeight <= pageHeight * 0.5);
    int effectiveHeaderCount = repeatHeaders ? headerRowCount : 0;
    qreal effectiveHeaderHeight = repeatHeaders ? headerHeight : 0;

    // Helper: create a new slice with table metadata and (optionally) header rows
    auto newSlice = [&]() -> TableBox {
        TableBox slice;
        slice.width = table.width;
        slice.borderWidth = table.borderWidth;
        slice.borderColor = table.borderColor;
        slice.innerBorderWidth = table.innerBorderWidth;
        slice.innerBorderColor = table.innerBorderColor;
        slice.headerBottomBorderWidth = table.headerBottomBorderWidth;
        slice.headerBottomBorderColor = table.headerBottomBorderColor;
        slice.cellPadding = table.cellPadding;
        slice.columnPositions = table.columnPositions;
        slice.headerRowCount = effectiveHeaderCount;
        slice.source = table.source;

        // Copy header rows with re-based y positions
        qreal hy = 0;
        for (int i = 0; i < effectiveHeaderCount && i < table.rows.size(); ++i) {
            TableRowBox row = table.rows[i];
            row.y = hy;
            for (auto &cell : row.cells)
                cell.y = hy;
            slice.rows.append(row);
            hy += row.height;
        }
        return slice;
    };

    TableBox currentSlice = newSlice();
    qreal currentHeight = effectiveHeaderHeight;
    qreal currentAvail = availHeight;
    int bodyRowsInSlice = 0;

    for (int i = headerRowCount; i < table.rows.size(); ++i) {
        qreal rowHeight = table.rows[i].height;

        // Need to start a new slice? Only if we have body rows in the current one
        if (currentHeight + rowHeight > currentAvail && bodyRowsInSlice > 0) {
            currentSlice.height = currentHeight;
            slices.append(currentSlice);

            currentSlice = newSlice();
            currentHeight = effectiveHeaderHeight;
            currentAvail = pageHeight;
            bodyRowsInSlice = 0;
        }

        // Copy body row with re-based y position
        TableRowBox row = table.rows[i];
        row.y = currentHeight;
        for (auto &cell : row.cells)
            cell.y = currentHeight;
        currentSlice.rows.append(row);
        currentHeight += rowHeight;
        bodyRowsInSlice++;
    }

    // Finalize last slice
    if (bodyRowsInSlice > 0) {
        currentSlice.height = currentHeight;
        slices.append(currentSlice);
    }

    return slices;
}

// --- Block splitting ---

std::optional<std::pair<BlockBox, BlockBox>>
splitBlockBox(const BlockBox &block, qreal availableHeight, int minLines)
{
    // Can't split blocks without lines (images, hrules)
    if (block.lines.isEmpty())
        return std::nullopt;

    int totalLines = block.lines.size();

    // Need at least minLines*2 to satisfy orphan+widow
    if (totalLines < minLines * 2)
        return std::nullopt;

    // Walk lines to find split point.
    // For code blocks (and blocks with padding), the available height for lines
    // is reduced by padding on top and bottom of each fragment.
    qreal paddingOverhead = (block.type == BlockBox::CodeBlockType) ? block.padding * 2 : 0;
    qreal availForLines = availableHeight - block.spaceBefore - paddingOverhead;
    if (availForLines <= 0)
        return std::nullopt;

    int splitAfter = 0; // number of lines in fragment 1
    qreal accum = 0;
    for (int i = 0; i < totalLines; ++i) {
        accum += block.lines[i].height;
        if (accum <= availForLines)
            splitAfter = i + 1;
        else
            break;
    }

    // Enforce orphan minimum (fragment 1 must have >= minLines)
    if (splitAfter < minLines)
        return std::nullopt;

    // Enforce widow minimum (fragment 2 must have >= minLines)
    if (totalLines - splitAfter < minLines)
        return std::nullopt;

    // Build fragment 1: lines [0..splitAfter)
    BlockBox frag1 = block;
    frag1.lines = block.lines.mid(0, splitAfter);
    frag1.isFragmentEnd = false;
    frag1.spaceAfter = 0;
    // Recompute height from lines
    qreal h1 = 0;
    for (int i = 0; i < splitAfter; ++i)
        h1 += block.lines[i].height;
    frag1.height = h1 + paddingOverhead;

    // Build fragment 2: lines [splitAfter..end)
    BlockBox frag2 = block;
    frag2.lines = block.lines.mid(splitAfter);
    frag2.isFragmentStart = false;
    frag2.spaceBefore = 0;
    frag2.firstLineIndent = 0; // continuation has no indent
    // Recompute height from lines
    qreal h2 = 0;
    for (int i = splitAfter; i < totalLines; ++i)
        h2 += block.lines[i].height;
    frag2.height = h2 + paddingOverhead;

    return std::make_pair(frag1, frag2);
}

std::optional<std::pair<FootnoteSectionBox, FootnoteSectionBox>>
splitFootnoteSection(const FootnoteSectionBox &box, qreal availableHeight)
{
    if (box.footnotes.size() < 2)
        return std::nullopt;

    // Account for separator height (~10pt) and spacing
    qreal separatorHeight = box.showSeparator ? 10.0 : 0;
    qreal availForFootnotes = availableHeight - separatorHeight;
    if (availForFootnotes <= 0)
        return std::nullopt;

    int splitAfter = 0;
    qreal accum = 0;
    for (int i = 0; i < box.footnotes.size(); ++i) {
        accum += box.footnotes[i].height;
        if (accum <= availForFootnotes)
            splitAfter = i + 1;
        else
            break;
    }

    if (splitAfter == 0 || splitAfter == box.footnotes.size())
        return std::nullopt;

    FootnoteSectionBox frag1 = box;
    frag1.footnotes = box.footnotes.mid(0, splitAfter);
    qreal h1 = separatorHeight;
    for (int i = 0; i < splitAfter; ++i)
        h1 += box.footnotes[i].height;
    frag1.height = h1;

    FootnoteSectionBox frag2;
    frag2.footnotes = box.footnotes.mid(splitAfter);
    frag2.showSeparator = false;
    frag2.width = box.width;
    // Rebase footnote y-positions
    qreal yOff = 0;
    for (auto &fn : frag2.footnotes) {
        fn.y = yOff;
        yOff += fn.height;
    }
    frag2.height = yOff;

    return std::make_pair(frag1, frag2);
}

// --- Page assignment ---

void Engine::assignToPages(const QList<PageElement> &elements,
                            const PageLayout &pageLayout,
                            LayoutResult &result)
{
    QSizeF contentSize = pageLayout.contentSizePoints();
    qreal pageHeight = contentSize.height();

    Page currentPage;
    currentPage.pageNumber = 0;
    qreal y = 0;

    QList<PageElement> queue = elements;

    for (int idx = 0; idx < queue.size(); ++idx) {
        const auto &element = queue[idx];

        // Handle tables separately for page-splitting
        if (auto *tablePtr = std::get_if<TableBox>(&element)) {
            const TableBox &table = *tablePtr;
            qreal remaining = pageHeight - y;

            if (table.height <= remaining || currentPage.elements.isEmpty()) {
                TableBox positioned = table;
                positioned.y = y;
                currentPage.elements.append(positioned);
                y += table.height;
            } else {
                auto slices = splitTable(table, remaining, pageHeight);
                for (int si = 0; si < slices.size(); ++si) {
                    if (si > 0) {
                        currentPage.contentHeight = y;
                        result.pages.append(currentPage);
                        currentPage = Page{};
                        currentPage.pageNumber = result.pages.size();
                        y = 0;
                    }
                    slices[si].y = y;
                    currentPage.elements.append(slices[si]);
                    y += slices[si].height;
                }
            }
            continue;
        }

        // Non-table elements: blocks and footnote sections
        qreal elementHeight = 0;
        qreal spaceBefore = 0;
        qreal spaceAfter = 0;
        bool keepWithNext = false;
        int lineCount = 0;

        std::visit([&](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox>) {
                elementHeight = e.height;
                spaceBefore = e.spaceBefore;
                spaceAfter = e.spaceAfter;
                keepWithNext = e.keepWithNext;
                lineCount = e.lines.size();
            } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                elementHeight = e.height;
                spaceBefore = 20.0;
            }
        }, element);

        qreal totalHeight = spaceBefore + elementHeight + spaceAfter;

        // Page break needed?
        bool needsPageBreak = (y + totalHeight > pageHeight) && !currentPage.elements.isEmpty();

        // Keep-with-next: if this is a heading (or element with keepWithNext),
        // peek at the next element. If both won't fit, break before this one.
        if (keepWithNext && !needsPageBreak && idx + 1 < queue.size()
            && !currentPage.elements.isEmpty()) {
            qreal nextHeight = 0;
            std::visit([&](const auto &ne) {
                using T = std::decay_t<decltype(ne)>;
                if constexpr (std::is_same_v<T, BlockBox>) {
                    nextHeight = ne.spaceBefore + ne.height;
                    // Only need the first couple of lines to keep with heading
                    if (ne.lines.size() > 2) {
                        qreal twoLineH = 0;
                        for (int li = 0; li < 2 && li < ne.lines.size(); ++li)
                            twoLineH += ne.lines[li].height;
                        nextHeight = ne.spaceBefore + twoLineH;
                    }
                } else if constexpr (std::is_same_v<T, TableBox>) {
                    nextHeight = ne.height;
                } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                    nextHeight = 20.0 + ne.height;
                }
            }, queue[idx + 1]);

            if (y + totalHeight + nextHeight > pageHeight)
                needsPageBreak = true;
        }

        // --- Block splitting logic ---
        // Try to split the block if it doesn't fit, rather than always
        // pushing the whole block to the next page.
        if (needsPageBreak) {
            // Before breaking to a new page, try splitting at the break point
            qreal remaining = pageHeight - y;
            bool didSplit = false;

            if (auto *bb = std::get_if<BlockBox>(&element)) {
                auto split = splitBlockBox(*bb, remaining);
                if (split) {
                    // Place fragment 1 on current page
                    auto &[f1, f2] = *split;
                    y += f1.spaceBefore;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height + f1.spaceAfter;
                    // Push fragment 2 back into queue
                    queue.insert(idx + 1, f2);
                    didSplit = true;
                }
            } else if (auto *fs = std::get_if<FootnoteSectionBox>(&element)) {
                auto split = splitFootnoteSection(*fs, remaining);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += 20.0; // spaceBefore for footnote sections
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height;
                    queue.insert(idx + 1, f2);
                    didSplit = true;
                }
            }

            if (didSplit) {
                // Fragment 1 placed; fragment 2 will be processed next iteration
                continue;
            }

            // Split failed — break to new page (existing behavior)
            currentPage.contentHeight = y;
            result.pages.append(currentPage);
            currentPage = Page{};
            currentPage.pageNumber = result.pages.size();
            y = 0;
        }

        // Handle blocks that don't fit even on a fresh empty page
        if (currentPage.elements.isEmpty() && y + totalHeight > pageHeight) {
            bool didSplit = false;

            if (auto *bb = std::get_if<BlockBox>(&element)) {
                auto split = splitBlockBox(*bb, pageHeight);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += f1.spaceBefore;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height + f1.spaceAfter;
                    queue.insert(idx + 1, f2);
                    continue;
                }
            } else if (auto *fs = std::get_if<FootnoteSectionBox>(&element)) {
                auto split = splitFootnoteSection(*fs, pageHeight);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += 20.0;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height;
                    queue.insert(idx + 1, f2);
                    continue;
                }
            }
            // If split failed, fall through to place-and-overflow (existing behavior)
        }

        y += spaceBefore;

        // Set element position
        PageElement positioned = element;
        std::visit([&](auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox>) {
                e.y = y;
            } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                e.y = y;
            }
        }, positioned);

        currentPage.elements.append(positioned);
        y += elementHeight + spaceAfter;
    }

    // Don't forget the last page
    if (!currentPage.elements.isEmpty()) {
        currentPage.contentHeight = y;
        result.pages.append(currentPage);
    }

    // Ensure at least one empty page
    if (result.pages.isEmpty()) {
        result.pages.append(Page{});
    }
}

qreal Engine::measureInlines(const QList<Content::InlineNode> &inlines,
                              const Content::TextStyle &baseStyle)
{
    auto collected = collectInlines(inlines, baseStyle);
    if (collected.text.isEmpty())
        return 0;

    QList<ShapedRun> runs = m_textShaper->shape(collected.text, collected.styleRuns);
    qreal width = 0;
    for (const auto &run : runs)
        for (const auto &g : run.glyphs)
            width += g.xAdvance;
    return width;
}

qreal Engine::measureMinInlines(const QList<Content::InlineNode> &inlines,
                                 const Content::TextStyle &baseStyle)
{
    auto collected = collectInlines(inlines, baseStyle);
    if (collected.text.isEmpty())
        return 0;

    QList<ShapedRun> runs = m_textShaper->shape(collected.text, collected.styleRuns);

    // Find ICU line break opportunities
    UErrorCode err = U_ZERO_ERROR;
    icu::UnicodeString ustr(reinterpret_cast<const UChar *>(collected.text.utf16()),
                            collected.text.length());
    std::unique_ptr<icu::BreakIterator> lineBreakIter(
        icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), err));

    QSet<int> breakPositions;
    if (lineBreakIter && U_SUCCESS(err)) {
        lineBreakIter->setText(ustr);
        for (int32_t pos = lineBreakIter->first();
             pos != icu::BreakIterator::DONE;
             pos = lineBreakIter->next()) {
            breakPositions.insert(pos);
        }
    }

    // Walk shaped glyphs, splitting at break positions to find widest word
    qreal maxWordWidth = 0;
    qreal currentWordWidth = 0;

    for (const auto &run : runs) {
        for (const auto &g : run.glyphs) {
            if (breakPositions.contains(g.cluster) && currentWordWidth > 0) {
                maxWordWidth = qMax(maxWordWidth, currentWordWidth);
                currentWordWidth = 0;
            }
            currentWordWidth += g.xAdvance;
        }
    }
    maxWordWidth = qMax(maxWordWidth, currentWordWidth);

    return maxWordWidth;
}

// --- Table column width distribution ---

QList<Engine::ColumnMetrics> Engine::measureColumnMetrics(const Content::Table &table)
{
    int numCols = 0;
    for (const auto &row : table.rows)
        numCols = qMax(numCols, row.cells.size());

    QList<ColumnMetrics> metrics(numCols, {0, 0});
    const qreal pad2 = table.cellPadding * 2;

    for (const auto &row : table.rows) {
        for (int ci = 0; ci < row.cells.size(); ++ci) {
            const auto &cell = row.cells[ci];
            Content::TextStyle cellStyle = cell.style;
            if (cellStyle.fontFamily.isEmpty()) {
                cellStyle.fontFamily = QStringLiteral("Noto Serif");
                cellStyle.fontSize = 11.0;
            }

            qreal maxW = measureInlines(cell.inlines, cellStyle) + pad2;
            qreal minW = measureMinInlines(cell.inlines, cellStyle) + pad2;

            metrics[ci].maxWidth = qMax(metrics[ci].maxWidth, maxW);
            metrics[ci].minWidth = qMax(metrics[ci].minWidth, minW);
        }
    }

    return metrics;
}

QList<qreal> Engine::distributeColumnsAuto(const QList<ColumnMetrics> &metrics,
                                            qreal availWidth)
{
    const int n = metrics.size();
    QList<qreal> widths(n);

    qreal totalMin = 0, totalMax = 0;
    for (const auto &m : metrics) {
        totalMin += m.minWidth;
        totalMax += m.maxWidth;
    }

    if (totalMax <= availWidth) {
        // All content fits without wrapping — use max widths, distribute surplus
        qreal surplus = availWidth - totalMax;
        for (int i = 0; i < n; ++i)
            widths[i] = metrics[i].maxWidth + surplus / n;
    } else if (totalMin >= availWidth) {
        // Can't avoid wrapping — use min widths, distribute proportionally
        for (int i = 0; i < n; ++i)
            widths[i] = metrics[i].minWidth * (availWidth / totalMin);
    } else {
        // Proportional distribution between min and max
        qreal W = availWidth - totalMin;
        qreal D = totalMax - totalMin;
        for (int i = 0; i < n; ++i)
            widths[i] = metrics[i].minWidth + (metrics[i].maxWidth - metrics[i].minWidth) * W / D;
    }

    return widths;
}

QList<qreal> Engine::distributeColumnsOptimal(const Content::Table &table,
                                               const QList<ColumnMetrics> &metrics,
                                               qreal availWidth)
{
    const int n = metrics.size();

    // Start with max widths
    QList<qreal> widths(n);
    qreal totalWidth = 0;
    for (int i = 0; i < n; ++i) {
        widths[i] = metrics[i].maxWidth;
        totalWidth += widths[i];
    }

    // If everything fits, distribute surplus evenly
    if (totalWidth <= availWidth) {
        qreal surplus = availWidth - totalWidth;
        for (int i = 0; i < n; ++i)
            widths[i] += surplus / n;
        return widths;
    }

    // Greedy narrowing: repeatedly narrow the column causing least height increase
    qreal overshoot = totalWidth - availWidth;

    // Helper: measure total height of a column at a given width
    auto measureColumnHeight = [&](int col, qreal colWidth) -> qreal {
        qreal totalH = 0;
        qreal innerWidth = colWidth - table.cellPadding * 2;
        if (innerWidth < 1)
            innerWidth = 1;

        for (const auto &row : table.rows) {
            if (col >= row.cells.size())
                continue;
            const auto &cell = row.cells[col];
            Content::TextStyle cellStyle = cell.style;
            if (cellStyle.fontFamily.isEmpty()) {
                cellStyle.fontFamily = QStringLiteral("Noto Serif");
                cellStyle.fontSize = 11.0;
            }
            Content::ParagraphFormat cellFmt;
            cellFmt.alignment = cell.alignment;
            auto lines = breakIntoLines(cell.inlines, cellStyle, cellFmt, innerWidth);
            qreal cellH = table.cellPadding * 2;
            for (const auto &line : lines)
                cellH += line.height;
            totalH += cellH;
        }
        return totalH;
    };

    // Cache current column heights
    QList<qreal> colHeights(n);
    for (int i = 0; i < n; ++i)
        colHeights[i] = measureColumnHeight(i, widths[i]);

    while (overshoot > 0.5) {
        // Step size: ~5% of overshoot, at least 1pt
        qreal step = qMax(1.0, overshoot * 0.05);

        int bestCol = -1;
        qreal bestCost = std::numeric_limits<qreal>::max();
        qreal bestNewHeight = 0;

        for (int i = 0; i < n; ++i) {
            // Don't narrow below minWidth
            if (widths[i] - step < metrics[i].minWidth)
                continue;

            qreal trialWidth = widths[i] - step;
            qreal newHeight = measureColumnHeight(i, trialWidth);
            qreal cost = newHeight - colHeights[i]; // height increase

            if (cost < bestCost) {
                bestCost = cost;
                bestCol = i;
                bestNewHeight = newHeight;
            }
        }

        if (bestCol < 0)
            break; // all columns at minWidth

        widths[bestCol] -= step;
        colHeights[bestCol] = bestNewHeight;
        overshoot -= step;
    }

    // If still over, distribute remaining overshoot proportionally above minWidth
    totalWidth = 0;
    for (int i = 0; i < n; ++i)
        totalWidth += widths[i];

    if (totalWidth > availWidth) {
        qreal remaining = totalWidth - availWidth;
        qreal shrinkable = 0;
        for (int i = 0; i < n; ++i)
            shrinkable += widths[i] - metrics[i].minWidth;

        if (shrinkable > 0) {
            for (int i = 0; i < n; ++i) {
                qreal share = (widths[i] - metrics[i].minWidth) / shrinkable;
                widths[i] -= remaining * share;
                widths[i] = qMax(widths[i], metrics[i].minWidth);
            }
        }
    }

    return widths;
}

} // namespace Layout
