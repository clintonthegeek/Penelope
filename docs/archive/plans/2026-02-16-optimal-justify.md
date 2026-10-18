# Optimal Text Justification Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the greedy line-breaking algorithm with Knuth-Plass optimal line breaking, add blended word + letter spacing for justification, and implement tiered threshold degradation — so justified text looks professional instead of having river gaps.

**Architecture:** The layout engine's `breakIntoLines()` is refactored to model text as boxes/glue/penalties, run the Knuth-Plass dynamic-programming optimizer to find minimum-badness breakpoints, then store per-line adjustment ratios. The PDF generator's `renderLineBox()` uses these ratios to distribute slack across both word gaps and inter-character spacing (blended justify). When no acceptable break sequence exists, tiered fallback tries progressively looser tolerances and hyphenation before falling back to ragged.

**Tech Stack:** Qt6 C++20, ICU BreakIterator (existing), Knuth-Plass algorithm (new implementation).

**Design doc:** See brainstorm analysis in conversation context.

---

## Background: Current Pipeline

```
collectInlines() → TextShaper::shape() → ICU break positions → build WordBox list
→ greedy line break → trim trailing spaces → compute line metrics
→ renderLineBox() → calculate extraPerGap → distribute evenly across word gaps
```

**Current limitations:**
1. Greedy first-fit line breaking (no lookahead)
2. Justify only expands word gaps (no letter spacing)
3. All-or-nothing maxJustifyGap threshold (justify or ragged, nothing between)
4. `letterSpacing` in ContentModel is never consumed by TextShaper or PdfGenerator

---

## Data Model Changes

### New struct: `JustifyInfo` on `LineBox`

Currently `LineBox` stores `width` and `isLastLine`. We add per-line justify metadata computed during line breaking, so the PDF generator doesn't need to re-derive it.

```cpp
// In layoutengine.h, inside LineBox:
struct JustifyInfo {
    qreal adjustmentRatio = 0;  // Knuth-Plass r: <0 = shrink, 0 = perfect, >0 = stretch
    int wordGapCount = 0;       // eligible inter-word gaps
    int charCount = 0;          // total characters (for letter spacing distribution)
    qreal extraWordSpacing = 0; // pre-computed per-word-gap expansion (points)
    qreal extraLetterSpacing = 0; // pre-computed per-character expansion (points)
};
```

This is set by the layout engine during line breaking and consumed by the PDF generator during rendering. The PDF generator no longer computes its own gap distribution — it uses the values from `JustifyInfo`.

---

### Task 1: Add JustifyInfo to LineBox and wire through rendering

**Files:**
- Modify: `src/layout/layoutengine.h:80-91` (add JustifyInfo to LineBox)
- Modify: `src/pdf/pdfgenerator.cpp:590-621` (consume JustifyInfo instead of computing gaps)
- Modify: `src/pdf/pdfgenerator.cpp:631-776` (apply blended spacing during rendering)

**Step 1: Add JustifyInfo struct to LineBox**

In `src/layout/layoutengine.h`, after line 91 (`bool showTrailingHyphen`), add inside `LineBox`:

```cpp
struct JustifyInfo {
    qreal adjustmentRatio = 0;
    int wordGapCount = 0;
    int charCount = 0;
    qreal extraWordSpacing = 0;
    qreal extraLetterSpacing = 0;
};
JustifyInfo justify;
```

**Step 2: Refactor renderLineBox to consume JustifyInfo**

In `src/pdf/pdfgenerator.cpp`, replace the justify calculation block (lines 590-621) with code that reads from `line.justify`:

```cpp
bool doJustify = false;
qreal extraPerGap = 0;
qreal extraPerChar = 0;

if (line.alignment == Qt::AlignJustify && !line.isLastLine
    && line.glyphs.size() > 1) {
    if (line.justify.wordGapCount > 0
        && (line.justify.extraWordSpacing != 0 || line.justify.extraLetterSpacing != 0)) {
        doJustify = true;
        extraPerGap = line.justify.extraWordSpacing;
        extraPerChar = line.justify.extraLetterSpacing;
    }
}
```

**Step 3: Apply letter spacing in rendering loops**

In the justify rendering paths (both markdown and non-markdown), after rendering each glyph box, add `extraPerChar * glyphCount` to the x position. For each glyph box:

```cpp
x += gbox.width + extraPerChar * gbox.glyphs.size();
// Then add word gap if applicable:
if (!skipGap)
    x += extraPerGap;
```

Update all three rendering paths (markdown Phase 1 positions, non-markdown justify, non-markdown non-justify).

**Step 4: Build and verify**

```bash
make -C build -j$(($(nproc)-1))
```

Expected: Compiles cleanly. Behaviour unchanged because JustifyInfo fields default to 0 (old code path still works — layout engine hasn't been changed yet).

**Step 5: Commit**

```bash
git add src/layout/layoutengine.h src/pdf/pdfgenerator.cpp
git commit -m "Add JustifyInfo to LineBox, refactor renderLineBox to consume it"
```

---

### Task 2: Implement Knuth-Plass data model (boxes, glue, penalties)

**Files:**
- Create: `src/layout/linebreaker.h`
- Create: `src/layout/linebreaker.cpp`
- Modify: `src/CMakeLists.txt` (add new source files)

**Step 1: Create the line breaker header**

Create `src/layout/linebreaker.h` with the Knuth-Plass data model:

```cpp
/*
 * linebreaker.h — Knuth-Plass optimal line breaking
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PENELOPE_LINEBREAKER_H
#define PENELOPE_LINEBREAKER_H

#include <QList>
#include <limits>

namespace LineBreaking {

// The three Knuth-Plass item types
struct Box {
    qreal width = 0;      // fixed width (glyph box)
    int wordIndex = -1;    // index into the word list
};

struct Glue {
    qreal width = 0;      // natural (desired) width
    qreal stretch = 0;    // maximum additional stretch
    qreal shrink = 0;     // maximum shrink (positive value)
};

struct Penalty {
    qreal width = 0;      // width if break occurs here (e.g. hyphen width)
    qreal penalty = 0;    // cost of breaking here (negative = preferred break)
    bool flagged = false;  // true for hyphen breaks (consecutive flagged = extra demerits)
};

// A paragraph item is one of the three types
struct Item {
    enum Type { BoxType, GlueType, PenaltyType };
    Type type = BoxType;
    Box box;
    Glue glue;
    Penalty penalty;

    static Item makeBox(qreal width, int wordIndex) {
        Item i; i.type = BoxType; i.box = {width, wordIndex}; return i;
    }
    static Item makeGlue(qreal width, qreal stretch, qreal shrink) {
        Item i; i.type = GlueType; i.glue = {width, stretch, shrink}; return i;
    }
    static Item makePenalty(qreal width, qreal penalty, bool flagged = false) {
        Item i; i.type = PenaltyType; i.penalty = {width, penalty, flagged}; return i;
    }
};

// Fitness class for adjacent-line penalties
enum FitnessClass { Tight = 0, Normal = 1, Loose = 2, VeryLoose = 3 };

// Result: one breakpoint per line
struct Breakpoint {
    int itemIndex = 0;           // index in item list where this line breaks
    qreal adjustmentRatio = 0;   // r: how much glue was stretched/shrunk
    FitnessClass fitness = Normal;
    qreal totalDemerits = 0;
};

struct BreakResult {
    QList<Breakpoint> breaks;    // one per line (the break at the END of each line)
    bool optimal = true;         // false if fell back to greedy
};

// Configuration
struct Config {
    qreal tolerance = 1.0;           // maximum acceptable |adjustment ratio|
    qreal looseTolerance = 4.0;      // emergency fallback tolerance
    qreal hyphenPenalty = 50.0;      // penalty for hyphen breaks
    qreal consecutiveHyphenDemerits = 3000.0;  // extra demerits for consecutive hyphens
    qreal fitnessDemerits = 100.0;   // extra demerits for adjacent fitness class difference > 1
    bool enableHyphenation = true;   // consider soft-hyphen break points

    // Blended spacing limits (as fraction of font size)
    qreal maxLetterSpacingFraction = 0.03;  // 3% of em (InDesign default)
    qreal minLetterSpacingFraction = -0.02; // -2% of em (allow slight tightening)
};

// Main entry point
BreakResult findBreaks(const QList<Item> &items,
                       const QList<qreal> &lineWidths,  // width per line (first may differ due to indent)
                       const Config &config = {});

// Compute adjustment ratio for a potential line
qreal computeAdjustmentRatio(const QList<Item> &items,
                              int start, int end,
                              qreal lineWidth);

// Compute blended spacing from adjustment ratio
struct BlendedSpacing {
    qreal extraWordSpacing = 0;
    qreal extraLetterSpacing = 0;
};
BlendedSpacing computeBlendedSpacing(qreal adjustmentRatio,
                                      qreal naturalWordGlueWidth,
                                      int wordGapCount,
                                      int charCount,
                                      qreal fontSize,
                                      const Config &config);

} // namespace LineBreaking

#endif // PENELOPE_LINEBREAKER_H
```

**Step 2: Create stub implementation**

Create `src/layout/linebreaker.cpp` with stub functions that return empty/default results:

```cpp
/*
 * linebreaker.cpp — Knuth-Plass optimal line breaking
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "linebreaker.h"
#include <cmath>
#include <QDebug>

namespace LineBreaking {

BreakResult findBreaks(const QList<Item> &items,
                       const QList<qreal> &lineWidths,
                       const Config &config)
{
    Q_UNUSED(items); Q_UNUSED(lineWidths); Q_UNUSED(config);
    BreakResult result;
    result.optimal = false;
    return result;
}

qreal computeAdjustmentRatio(const QList<Item> &items,
                              int start, int end,
                              qreal lineWidth)
{
    Q_UNUSED(items); Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(lineWidth);
    return 0;
}

BlendedSpacing computeBlendedSpacing(qreal adjustmentRatio,
                                      qreal naturalWordGlueWidth,
                                      int wordGapCount,
                                      int charCount,
                                      qreal fontSize,
                                      const Config &config)
{
    Q_UNUSED(adjustmentRatio); Q_UNUSED(naturalWordGlueWidth);
    Q_UNUSED(wordGapCount); Q_UNUSED(charCount);
    Q_UNUSED(fontSize); Q_UNUSED(config);
    return {};
}

} // namespace LineBreaking
```

**Step 3: Add to CMakeLists.txt**

Add `layout/linebreaker.cpp` and `layout/linebreaker.h` to the source list in `src/CMakeLists.txt`.

**Step 4: Build**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 5: Commit**

```bash
git add src/layout/linebreaker.h src/layout/linebreaker.cpp src/CMakeLists.txt
git commit -m "Add Knuth-Plass line breaker data model (boxes, glue, penalties)"
```

---

### Task 3: Implement the Knuth-Plass algorithm core

**Files:**
- Modify: `src/layout/linebreaker.cpp` (implement `findBreaks`)

This is the heart of the feature. The algorithm uses dynamic programming to find the sequence of breakpoints that minimizes total demerits across all lines.

**Step 1: Implement computeAdjustmentRatio**

```cpp
qreal computeAdjustmentRatio(const QList<Item> &items,
                              int start, int end,
                              qreal lineWidth)
{
    qreal totalWidth = 0;
    qreal totalStretch = 0;
    qreal totalShrink = 0;

    for (int i = start; i < end; ++i) {
        switch (items[i].type) {
        case Item::BoxType:
            totalWidth += items[i].box.width;
            break;
        case Item::GlueType:
            // Glue at the start of a line after a break is skipped
            if (i > start || items[i].glue.width > 0) {
                totalWidth += items[i].glue.width;
                totalStretch += items[i].glue.stretch;
                totalShrink += items[i].glue.shrink;
            }
            break;
        case Item::PenaltyType:
            // Penalty width (hyphen) is added only at the break point itself
            break;
        }
    }
    // Add penalty width at the break point
    if (end < items.size() && items[end].type == Item::PenaltyType)
        totalWidth += items[end].penalty.width;

    qreal shortfall = lineWidth - totalWidth;
    if (shortfall > 0) {
        return (totalStretch > 0) ? shortfall / totalStretch
                                  : std::numeric_limits<qreal>::infinity();
    } else if (shortfall < 0) {
        return (totalShrink > 0) ? shortfall / totalShrink  // negative ratio
                                 : -std::numeric_limits<qreal>::infinity();
    }
    return 0; // perfect fit
}
```

**Step 2: Implement findBreaks with dynamic programming**

The algorithm maintains a list of active breakpoint nodes. For each feasible break position, it computes the adjustment ratio from each active node, calculates demerits, and keeps the best path.

```cpp
BreakResult findBreaks(const QList<Item> &items,
                       const QList<qreal> &lineWidths,
                       const Config &config)
{
    if (items.isEmpty())
        return {};

    // Active node in the DP search
    struct Node {
        int itemIndex;          // break position in items
        int lineNumber;         // line ending at this break (0 = before first line)
        FitnessClass fitness;
        qreal totalWidth;       // sum of box+glue widths up to this point
        qreal totalStretch;     // sum of glue stretch up to this point
        qreal totalShrink;      // sum of glue shrink up to this point
        qreal totalDemerits;
        int prevNode;           // index of previous node in path (-1 = start)
        bool flagged;           // previous break was at a flagged penalty
    };

    QList<Node> nodes;
    // Initial node: break before the first item
    nodes.append({0, 0, Normal, 0, 0, 0, 0, -1, false});

    QList<int> activeList = {0};

    // Running totals for efficient range computation
    qreal sumWidth = 0, sumStretch = 0, sumShrink = 0;
    QList<qreal> prefixWidth, prefixStretch, prefixShrink;
    prefixWidth.reserve(items.size() + 1);
    prefixStretch.reserve(items.size() + 1);
    prefixShrink.reserve(items.size() + 1);
    prefixWidth.append(0);
    prefixStretch.append(0);
    prefixShrink.append(0);

    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        if (item.type == Item::BoxType) {
            sumWidth += item.box.width;
        } else if (item.type == Item::GlueType) {
            sumWidth += item.glue.width;
            sumStretch += item.glue.stretch;
            sumShrink += item.glue.shrink;
        }
        prefixWidth.append(sumWidth);
        prefixStretch.append(sumStretch);
        prefixShrink.append(sumShrink);
    }

    // Helper: get line width for a given line number
    auto getLineWidth = [&](int lineNum) -> qreal {
        if (lineNum < lineWidths.size())
            return lineWidths[lineNum];
        return lineWidths.isEmpty() ? 400.0 : lineWidths.last();
    };

    // Helper: compute r from a node to position i
    auto computeR = [&](const Node &node, int breakIdx) -> qreal {
        qreal w = prefixWidth[breakIdx] - node.totalWidth;
        qreal y = prefixStretch[breakIdx] - node.totalStretch;
        qreal z = prefixShrink[breakIdx] - node.totalShrink;

        // Add penalty width if breaking at a penalty
        if (breakIdx < items.size() && items[breakIdx].type == Item::PenaltyType)
            w += items[breakIdx].penalty.width;

        // Skip leading glue after the break (inter-word space at line start)
        // The node's totalWidth already accounts for items up to the break,
        // but glue immediately after a break should be ignored for the NEXT line.
        // (Handled by the prefix sums: the next line starts from breakIdx.)

        qreal lineW = getLineWidth(node.lineNumber);
        qreal shortfall = lineW - w;
        if (shortfall > 0)
            return (y > 1e-9) ? shortfall / y : std::numeric_limits<qreal>::infinity();
        else if (shortfall < 0)
            return (z > 1e-9) ? shortfall / z : -std::numeric_limits<qreal>::infinity();
        return 0;
    };

    // Helper: fitness class from adjustment ratio
    auto fitnessOf = [](qreal r) -> FitnessClass {
        if (r < -0.5) return Tight;
        if (r <= 0.5) return Normal;
        if (r <= 1.0) return Loose;
        return VeryLoose;
    };

    // Process each item as a potential breakpoint
    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];

        // Can we break here?
        bool canBreak = false;
        if (item.type == Item::PenaltyType && item.penalty.penalty < 10000)
            canBreak = true;
        // Can break at glue if preceded by a box
        if (item.type == Item::GlueType && i > 0 && items[i - 1].type == Item::BoxType)
            canBreak = true;

        if (!canBreak)
            continue;

        // Evaluate all active nodes as potential previous breaks
        QList<int> toDeactivate;
        struct Candidate {
            int nodeIndex;
            qreal demerits;
            FitnessClass fitness;
            qreal r;
        };
        // Best candidate per fitness class
        Candidate best[4] = {};
        for (int f = 0; f < 4; ++f)
            best[f] = {-1, std::numeric_limits<qreal>::infinity(), static_cast<FitnessClass>(f), 0};

        for (int ai = 0; ai < activeList.size(); ++ai) {
            int ni = activeList[ai];
            const Node &anode = nodes[ni];

            qreal r = computeR(anode, i);

            // Deactivate if line is too short even with max stretch
            // (no future break will make it better)
            if (r < -1.0) {
                toDeactivate.append(ai);
                continue;
            }

            // Skip if too loose
            if (r > config.tolerance && r > config.looseTolerance)
                continue;

            // Compute demerits
            qreal badness = 100.0 * std::pow(std::abs(r), 3.0);
            qreal pen = (item.type == Item::PenaltyType) ? item.penalty.penalty : 0;
            qreal d;
            if (pen >= 0)
                d = (1.0 + badness + pen) * (1.0 + badness + pen);
            else if (pen > -10000)
                d = (1.0 + badness) * (1.0 + badness) - pen * pen;
            else
                d = (1.0 + badness) * (1.0 + badness);

            // Consecutive flagged penalty (consecutive hyphens)
            bool flagged = (item.type == Item::PenaltyType && item.penalty.flagged);
            if (flagged && anode.flagged)
                d += config.consecutiveHyphenDemerits;

            // Fitness class demerits (adjacent lines with very different tightness)
            FitnessClass fc = fitnessOf(r);
            if (std::abs(static_cast<int>(fc) - static_cast<int>(anode.fitness)) > 1)
                d += config.fitnessDemerits;

            qreal totalD = anode.totalDemerits + d;

            if (totalD < best[fc].demerits) {
                best[fc] = {ni, totalD, fc, r};
            }
        }

        // Remove deactivated nodes (in reverse to preserve indices)
        std::sort(toDeactivate.begin(), toDeactivate.end(), std::greater<int>());
        for (int idx : toDeactivate)
            activeList.removeAt(idx);

        // Create new active nodes from best candidates
        for (int f = 0; f < 4; ++f) {
            if (best[f].nodeIndex < 0)
                continue;
            Node newNode;
            newNode.itemIndex = i;
            newNode.lineNumber = nodes[best[f].nodeIndex].lineNumber + 1;
            newNode.fitness = best[f].fitness;
            newNode.totalWidth = prefixWidth[i + 1];
            newNode.totalStretch = prefixStretch[i + 1];
            newNode.totalShrink = prefixShrink[i + 1];
            newNode.totalDemerits = best[f].demerits;
            newNode.prevNode = best[f].nodeIndex;
            newNode.flagged = (item.type == Item::PenaltyType && item.penalty.flagged);
            nodes.append(newNode);
            activeList.append(nodes.size() - 1);
        }

        // If all active nodes were deactivated and none created, we need emergency mode
        if (activeList.isEmpty()) {
            // Fallback: force break at this position with high demerits
            Node emergency;
            emergency.itemIndex = i;
            emergency.lineNumber = nodes.isEmpty() ? 1 : nodes.last().lineNumber + 1;
            emergency.fitness = VeryLoose;
            emergency.totalWidth = prefixWidth[i + 1];
            emergency.totalStretch = prefixStretch[i + 1];
            emergency.totalShrink = prefixShrink[i + 1];
            emergency.totalDemerits = 1e10;
            emergency.prevNode = nodes.size() - 1;
            emergency.flagged = false;
            nodes.append(emergency);
            activeList.append(nodes.size() - 1);
        }
    }

    // Find the best active node at the end (includes forced final break)
    int bestEnd = -1;
    qreal bestDemerits = std::numeric_limits<qreal>::infinity();
    for (int ni : activeList) {
        if (nodes[ni].totalDemerits < bestDemerits) {
            bestDemerits = nodes[ni].totalDemerits;
            bestEnd = ni;
        }
    }

    if (bestEnd < 0) {
        BreakResult result;
        result.optimal = false;
        return result;
    }

    // Trace back through the path to get breakpoints
    QList<Breakpoint> breaks;
    for (int ni = bestEnd; ni >= 0 && nodes[ni].prevNode >= 0; ni = nodes[ni].prevNode) {
        Breakpoint bp;
        bp.itemIndex = nodes[ni].itemIndex;
        bp.adjustmentRatio = 0; // will be recomputed
        bp.fitness = nodes[ni].fitness;
        bp.totalDemerits = nodes[ni].totalDemerits;
        breaks.prepend(bp);
    }

    // Recompute adjustment ratios for each line
    int prevBreak = 0;
    for (int bi = 0; bi < breaks.size(); ++bi) {
        int lineNum = bi;
        qreal lineW = getLineWidth(lineNum);
        breaks[bi].adjustmentRatio = computeAdjustmentRatio(items, prevBreak, breaks[bi].itemIndex, lineW);
        prevBreak = breaks[bi].itemIndex + 1; // skip past the break item
        // Skip leading glue after break
        while (prevBreak < items.size() && items[prevBreak].type == Item::GlueType)
            ++prevBreak;
    }

    BreakResult result;
    result.breaks = breaks;
    result.optimal = true;
    return result;
}
```

**Step 3: Implement computeBlendedSpacing**

```cpp
BlendedSpacing computeBlendedSpacing(qreal adjustmentRatio,
                                      qreal naturalWordGlueWidth,
                                      int wordGapCount,
                                      int charCount,
                                      qreal fontSize,
                                      const Config &config)
{
    BlendedSpacing result;
    if (wordGapCount <= 0 || std::abs(adjustmentRatio) < 1e-9)
        return result;

    // Total slack to distribute
    // adjustmentRatio * totalStretch (or totalShrink) gives total adjustment
    // But we work with the simpler approach: compute total slack from word glue
    qreal wordStretch = naturalWordGlueWidth * 0.5; // 50% stretch per word gap
    qreal wordShrink = naturalWordGlueWidth * 0.33;  // 33% shrink per word gap

    qreal totalSlack;
    if (adjustmentRatio > 0)
        totalSlack = adjustmentRatio * wordStretch * wordGapCount;
    else
        totalSlack = adjustmentRatio * wordShrink * wordGapCount;

    // Letter spacing limits
    qreal maxLetterAdj = config.maxLetterSpacingFraction * fontSize;
    qreal minLetterAdj = config.minLetterSpacingFraction * fontSize;

    if (charCount > 0) {
        // Try to split: 2/3 into word spacing, 1/3 into letter spacing
        qreal letterPortion = totalSlack / 3.0;
        qreal wordPortion = totalSlack - letterPortion;

        // Clamp letter spacing to limits
        qreal letterPerChar = letterPortion / charCount;
        letterPerChar = qBound(minLetterAdj, letterPerChar, maxLetterAdj);
        qreal actualLetterTotal = letterPerChar * charCount;

        // Remainder goes to word spacing
        qreal actualWordTotal = totalSlack - actualLetterTotal;
        result.extraWordSpacing = actualWordTotal / wordGapCount;
        result.extraLetterSpacing = letterPerChar;
    } else {
        // No characters to letter-space, all goes to word gaps
        result.extraWordSpacing = totalSlack / wordGapCount;
    }

    return result;
}
```

**Step 4: Build**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 5: Commit**

```bash
git add src/layout/linebreaker.cpp
git commit -m "Implement Knuth-Plass core algorithm with blended spacing"
```

---

### Task 4: Convert WordBox list to Knuth-Plass items

**Files:**
- Modify: `src/layout/layoutengine.cpp` (add conversion function, not yet wired in)
- Modify: `src/layout/layoutengine.h` (add `#include "linebreaker.h"`)

**Step 1: Add include**

In `src/layout/layoutengine.h`, add `#include "linebreaker.h"` after the existing includes.

In `src/layout/layoutengine.cpp`, add `#include "linebreaker.h"` at the top.

**Step 2: Add item conversion function**

In `src/layout/layoutengine.cpp`, add a helper function (above `breakIntoLines`) that converts the existing `WordBox` list into Knuth-Plass `Item` list:

```cpp
namespace {

// Convert word boxes + break metadata into Knuth-Plass items
QList<LineBreaking::Item> buildKPItems(
    const QList<WordBox> &words,
    const Content::ParagraphFormat &format,
    qreal hyphenWidth)
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

        // Box: the word itself
        items.append(LineBreaking::Item::makeBox(word.gbox.width, i));

        // After each word (except the last), add glue or soft-hyphen penalty
        if (i + 1 < words.size() && !words[i + 1].isNewline) {
            if (word.gbox.trailingSoftHyphen) {
                // Soft hyphen: penalty with flagged=true, width = hyphen width
                items.append(LineBreaking::Item::makePenalty(hyphenWidth, 50.0, true));
            }

            // Inter-word glue: natural width = trailing space width in this word
            // Estimate: last glyph's advance if it's a space, or ~1/3 em
            qreal spaceWidth = word.gbox.fontSize * 0.25; // reasonable default
            if (!word.gbox.glyphs.isEmpty()) {
                // The trailing glyph might be a space (stripped during trimming
                // but width was already included). Use a fraction of fontSize.
                spaceWidth = word.gbox.fontSize * 0.25;
            }

            // Glue: natural=spaceWidth, stretch=spaceWidth*0.5, shrink=spaceWidth*0.33
            if (format.alignment == Qt::AlignJustify) {
                items.append(LineBreaking::Item::makeGlue(
                    spaceWidth,
                    spaceWidth * 0.5,
                    spaceWidth * 0.33));
            } else {
                // For non-justified text, glue has zero flexibility
                items.append(LineBreaking::Item::makeGlue(spaceWidth, 0, 0));
            }
        }
    }

    // Forced final break
    items.append(LineBreaking::Item::makePenalty(0, -1e7));

    return items;
}

} // anonymous namespace
```

**Note on space width:** The current code includes trailing spaces in glyph box widths, then trims them. We need the natural inter-word space width for glue. In the current shaping pipeline, spaces are the trailing glyph of each word box. We can compute this from the space glyph's advance width. A more accurate approach (done in Task 5) measures the actual space glyph width from the font.

**Step 3: Build**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 4: Commit**

```bash
git add src/layout/layoutengine.cpp src/layout/layoutengine.h
git commit -m "Add WordBox to Knuth-Plass item list conversion"
```

---

### Task 5: Wire Knuth-Plass into breakIntoLines with fallback

**Files:**
- Modify: `src/layout/layoutengine.cpp:457-588` (replace greedy algorithm)

This is the integration task. The greedy line breaker is replaced with a two-pass approach:
1. Try Knuth-Plass optimal breaking
2. If it fails (no feasible breaks), fall back to greedy

**Step 1: Measure space width accurately**

Before building KP items, measure the actual space glyph width from the font used for body text. Add this at the start of `breakIntoLines`, after shaping:

```cpp
// Measure natural inter-word space width from the primary font
qreal naturalSpaceWidth = baseStyle.fontSize * 0.25; // fallback
if (!shapedRuns.isEmpty() && shapedRuns.first().font) {
    FontFace *face = shapedRuns.first().font;
    naturalSpaceWidth = m_fontManager->glyphWidth(
        face, m_fontManager->spaceGlyphId(face), baseStyle.fontSize);
    if (naturalSpaceWidth <= 0)
        naturalSpaceWidth = baseStyle.fontSize * 0.25;
}
```

**Note:** `FontManager::spaceGlyphId()` may need to be added. If not, use `FT_Get_Char_Index(face->ftFace, ' ')` or for Hershey fonts, `face->hersheyFont->advanceWidth(' ') * scale`. Alternatively, examine the shaped space glyph in the first shaped run.

A simpler approach that avoids new API: scan the shaped runs for a space character cluster and read its xAdvance:

```cpp
qreal naturalSpaceWidth = baseStyle.fontSize * 0.25;
for (const auto &run : shapedRuns) {
    for (const auto &g : run.glyphs) {
        if (g.cluster < collected.text.size()
            && collected.text[g.cluster] == ' '
            && g.xAdvance > 0) {
            naturalSpaceWidth = g.xAdvance;
            goto foundSpace;
        }
    }
}
foundSpace:
```

**Step 2: Replace greedy with KP + fallback**

Replace the greedy line breaking section (lines 457-527) with:

```cpp
// --- Knuth-Plass optimal line breaking ---
qreal firstLineIndent = format.firstLineIndent;
qreal firstLineWidth = availWidth - firstLineIndent;

// Measure hyphen width for soft-hyphen penalties
qreal hyphenWidth = naturalSpaceWidth * 0.6; // reasonable estimate
// (Could measure actual '-' glyph width for accuracy)

// Build line widths list (first line may be narrower due to indent)
QList<qreal> lineWidths;
lineWidths.append(firstLineWidth);
lineWidths.append(availWidth); // all subsequent lines

// Convert word boxes to KP items
auto kpItems = buildKPItems(words, format, hyphenWidth);

// Configure KP
LineBreaking::Config kpConfig;
kpConfig.enableHyphenation = m_hyphenateJustifiedText;

// Try optimal breaking
auto kpResult = LineBreaking::findBreaks(kpItems, lineWidths, kpConfig);

if (kpResult.optimal && !kpResult.breaks.isEmpty()) {
    // Build lines from KP breakpoints
    int wordStart = 0;
    for (int bi = 0; bi < kpResult.breaks.size(); ++bi) {
        const auto &bp = kpResult.breaks[bi];
        LineBox line;
        line.alignment = format.alignment;

        // Find the word index range for this line
        int breakItemIdx = bp.itemIndex;
        // The break item is a Penalty or Glue — find the last Box before it
        int lastBoxWordIdx = -1;
        for (int ii = breakItemIdx; ii >= 0; --ii) {
            if (kpItems[ii].type == LineBreaking::Item::BoxType) {
                lastBoxWordIdx = kpItems[ii].box.wordIndex;
                break;
            }
        }

        // Collect word boxes for this line
        int charCount = 0;
        int gapCount = 0;
        for (int wi = wordStart; wi < words.size() && wi <= lastBoxWordIdx; ++wi) {
            if (words[wi].isNewline) continue;
            line.glyphs.append(words[wi].gbox);
            charCount += words[wi].gbox.glyphs.size();
            if (wi > wordStart && !words[wi].gbox.startsAfterSoftHyphen
                && !words[wi - 1].gbox.isListMarker)
                gapCount++;
        }

        // Check if break is at a soft-hyphen penalty
        if (breakItemIdx < kpItems.size()
            && kpItems[breakItemIdx].type == LineBreaking::Item::PenaltyType
            && kpItems[breakItemIdx].penalty.flagged) {
            line.showTrailingHyphen = true;
        }

        // Mark last line
        if (bi == kpResult.breaks.size() - 1)
            line.isLastLine = true;

        // Compute blended spacing
        if (format.alignment == Qt::AlignJustify && !line.isLastLine && gapCount > 0) {
            auto spacing = LineBreaking::computeBlendedSpacing(
                bp.adjustmentRatio, naturalSpaceWidth,
                gapCount, charCount, baseStyle.fontSize, kpConfig);
            line.justify.adjustmentRatio = bp.adjustmentRatio;
            line.justify.wordGapCount = gapCount;
            line.justify.charCount = charCount;
            line.justify.extraWordSpacing = spacing.extraWordSpacing;
            line.justify.extraLetterSpacing = spacing.extraLetterSpacing;
        }

        lines.append(line);

        // Advance wordStart past the words we just consumed
        wordStart = (lastBoxWordIdx >= 0) ? lastBoxWordIdx + 1 : wordStart + 1;
    }
} else {
    // Fallback: greedy line breaking (existing code)
    qreal lineWidth = availWidth - firstLineIndent;
    qreal currentX = 0;
    LineBox currentLine;
    currentLine.alignment = format.alignment;
    bool isFirstLine = true;

    // ... (keep existing greedy code as-is for fallback)
}
```

**Step 3: Keep the existing greedy code inside the `else` block**

Move the current greedy line breaking (lines 457-527) into the else-block verbatim. Also add basic justify info computation for the greedy path:

After the greedy path builds lines, compute `JustifyInfo` for each line using the old approach:

```cpp
// Greedy fallback: compute JustifyInfo using old method
for (auto &line : lines) {
    if (line.alignment != Qt::AlignJustify || line.isLastLine)
        continue;
    int gapCount = 0;
    int charCount = 0;
    for (int i = 0; i < line.glyphs.size(); ++i) {
        charCount += line.glyphs[i].glyphs.size();
        if (i > 0 && !line.glyphs[i].startsAfterSoftHyphen
            && !line.glyphs[i - 1].isListMarker)
            gapCount++;
    }
    if (gapCount > 0) {
        qreal extraSpace = availWidth - line.width;
        qreal extraPerGap = extraSpace / gapCount;
        if (extraPerGap <= m_maxJustifyGap) {  // Need maxJustifyGap accessible here
            line.justify.wordGapCount = gapCount;
            line.justify.charCount = charCount;
            line.justify.extraWordSpacing = extraPerGap;
            // No letter spacing in greedy fallback
        }
    }
}
```

**Note:** The `m_maxJustifyGap` is currently on PdfGenerator, not on Engine. For the greedy fallback, we'll need to either pass it as a parameter or add it to Engine. Add a `setMaxJustifyGap(qreal)` to Engine (similar to PdfGenerator).

**Step 4: Build and test**

```bash
make -C build -j$(($(nproc)-1))
```

Run the app, open a document with justified text, export to PDF. Compare visual quality.

**Step 5: Commit**

```bash
git add src/layout/layoutengine.cpp src/layout/layoutengine.h
git commit -m "Wire Knuth-Plass line breaking into layout engine with greedy fallback"
```

---

### Task 6: Handle glue width accurately from shaped text

**Files:**
- Modify: `src/layout/layoutengine.cpp` (refine space width measurement in buildKPItems)

The initial `buildKPItems` uses a rough `fontSize * 0.25` for space width. This task refines it to use the actual shaped space glyph advance width, which varies per font.

**Step 1: Pass natural space width into buildKPItems**

Change `buildKPItems` signature to accept `qreal naturalSpaceWidth`:

```cpp
QList<LineBreaking::Item> buildKPItems(
    const QList<WordBox> &words,
    const Content::ParagraphFormat &format,
    qreal hyphenWidth,
    qreal naturalSpaceWidth)
```

Use `naturalSpaceWidth` for glue natural width. Stretch = `naturalSpaceWidth * 0.5`, shrink = `naturalSpaceWidth * 0.33`.

**Step 2: Measure space width per-run for mixed-font paragraphs**

For paragraphs with mixed fonts (e.g. inline code with a different font), the space width should come from the font that shaped the trailing space. Scan the word's trailing glyph to determine if it's a space and use its actual xAdvance:

```cpp
// In buildKPItems, when computing glue after a word box:
qreal spaceW = naturalSpaceWidth; // default
const auto &gbox = word.gbox;
if (!gbox.glyphs.isEmpty()) {
    // Check if the last glyph in this word is a space
    const auto &lastG = gbox.glyphs.last();
    if (lastG.cluster < textLength  // bounds check would need text passed in
        && lastG.xAdvance > 0) {
        // Use actual advance — but only if it's a space character
        // (We don't have the text here, so use a heuristic: if the word box
        // has trailing space, its width includes it. The trimming step will
        // remove it. For KP items, we separate box and glue.)
    }
}
```

Actually, the cleaner approach: split the trailing space OUT of the word box width before creating the Box item, and put it into the Glue item. This requires a small refactor:

In `buildKPItems`, the box width should be the word width minus any trailing space width. The glue width IS the trailing space width.

To do this, we need to know which words have trailing spaces. The current code trims trailing spaces from non-last lines AFTER line breaking. For KP, we need to know the space width BEFORE line breaking.

**Approach:** Before building KP items, scan each word box for trailing space glyphs and record the space width separately:

```cpp
struct WordMeta {
    qreal contentWidth = 0;  // width without trailing space
    qreal spaceWidth = 0;    // trailing space glyph width (if any)
};
QList<WordMeta> wordMeta;
wordMeta.reserve(words.size());
for (const auto &word : words) {
    WordMeta meta;
    meta.contentWidth = word.gbox.width;
    // Check if last glyph is a space
    if (!word.isNewline && !word.gbox.glyphs.isEmpty()) {
        int lastCluster = word.gbox.glyphs.last().cluster;
        if (lastCluster < collected.text.size()
            && collected.text[lastCluster].isSpace()) {
            meta.spaceWidth = word.gbox.glyphs.last().xAdvance;
            meta.contentWidth -= meta.spaceWidth;
        }
    }
    wordMeta.append(meta);
}
```

Then in `buildKPItems`, use `meta.contentWidth` for the Box and `meta.spaceWidth` for the Glue natural width.

**Step 3: Build and test**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 4: Commit**

```bash
git add src/layout/layoutengine.cpp
git commit -m "Use actual shaped space widths for Knuth-Plass glue"
```

---

### Task 7: Tiered tolerance with automatic hyphenation escalation

**Files:**
- Modify: `src/layout/linebreaker.cpp` (add multi-pass with escalating tolerance)
- Modify: `src/layout/layoutengine.cpp` (wire tiered passes)

**Step 1: Add tiered findBreaks wrapper**

In `linebreaker.cpp`, add a function that tries progressively looser settings:

```cpp
BreakResult findBreaksTiered(const QList<Item> &items,
                             const QList<qreal> &lineWidths,
                             const Config &baseConfig)
{
    // Pass 1: strict tolerance, with hyphenation if enabled
    {
        Config c = baseConfig;
        c.tolerance = 1.0;
        auto result = findBreaks(items, lineWidths, c);
        if (result.optimal)
            return result;
    }

    // Pass 2: relaxed tolerance (allows looser lines)
    {
        Config c = baseConfig;
        c.tolerance = 2.0;
        auto result = findBreaks(items, lineWidths, c);
        if (result.optimal)
            return result;
    }

    // Pass 3: emergency tolerance (very loose lines allowed)
    {
        Config c = baseConfig;
        c.tolerance = baseConfig.looseTolerance;
        auto result = findBreaks(items, lineWidths, c);
        if (result.optimal)
            return result;
    }

    // Pass 4: give up on optimal, fall back to greedy
    BreakResult fallback;
    fallback.optimal = false;
    return fallback;
}
```

Add declaration to `linebreaker.h`:

```cpp
BreakResult findBreaksTiered(const QList<Item> &items,
                             const QList<qreal> &lineWidths,
                             const Config &baseConfig = {});
```

**Step 2: Use findBreaksTiered in layoutengine.cpp**

In `breakIntoLines`, replace the call to `findBreaks` with `findBreaksTiered`:

```cpp
auto kpResult = LineBreaking::findBreaksTiered(kpItems, lineWidths, kpConfig);
```

**Step 3: Build and test**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 4: Commit**

```bash
git add src/layout/linebreaker.h src/layout/linebreaker.cpp src/layout/layoutengine.cpp
git commit -m "Add tiered tolerance passes for graceful justify degradation"
```

---

### Task 8: Propagate maxJustifyGap to layout engine

**Files:**
- Modify: `src/layout/layoutengine.h` (add m_maxJustifyGap member)
- Modify: `src/app/mainwindow.cpp` (pass maxJustifyGap to engine)
- Modify: `src/pdf/pdfgenerator.cpp` (remove old gap calculation, rely on JustifyInfo)

**Step 1: Add maxJustifyGap to Engine**

In `src/layout/layoutengine.h`, add to the Engine class:

```cpp
void setMaxJustifyGap(qreal gap) { m_maxJustifyGap = gap; }
// ...
qreal m_maxJustifyGap = 14.0;
```

**Step 2: Wire from mainwindow**

In `src/app/mainwindow.cpp`, wherever `pdfGen.setMaxJustifyGap(...)` is called, also call `engine.setMaxJustifyGap(...)` on the layout engine (or pass it through the appropriate path).

Search for all places where the layout engine is created/configured and ensure `maxJustifyGap` is set.

**Step 3: Use in greedy fallback**

In the greedy fallback path in `breakIntoLines`, use `m_maxJustifyGap` for the threshold check:

```cpp
if (extraPerGap <= m_maxJustifyGap) {
    line.justify.extraWordSpacing = extraPerGap;
    // ...
}
```

**Step 4: Clean up PdfGenerator**

In `renderLineBox`, the old justify calculation block (counting gaps, computing extraPerGap) can now be simplified to just reading from `line.justify`. Remove the gap-counting logic and rely entirely on JustifyInfo.

**Step 5: Build and test**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 6: Commit**

```bash
git add src/layout/layoutengine.h src/app/mainwindow.cpp src/pdf/pdfgenerator.cpp
git commit -m "Move justify gap threshold to layout engine, simplify PDF generator"
```

---

### Task 9: Integration testing and tuning

**Files:** Various — tuning pass based on visual results.

**Step 1: Full build and smoke test**

```bash
make -C build -j$(($(nproc)-1))
./build/bin/Penelope
```

Open a markdown document with:
- Regular body text (justified paragraphs)
- Short paragraphs (few words per line — stress test)
- Long words (forces hyphenation)
- Mixed fonts (inline code within body text)
- Hershey theme (verify spacing improves for stroke fonts)

**Step 2: Compare before/after PDFs**

Export the same document with:
1. Default theme (TrueType fonts)
2. Hershey theme (stroke fonts)

Check for:
- Even word spacing across lines (no rivers)
- Subtle letter spacing (not visually jarring)
- Hyphenation at reasonable points
- Last lines of paragraphs are left-aligned (not justified)
- No crashes or rendering artifacts

**Step 3: Tune parameters**

Based on visual results, adjust in `linebreaker.h` Config defaults:
- `tolerance`: Start at 1.0 (TeX default). Increase if too many lines fall back.
- `maxLetterSpacingFraction`: Start at 0.03 (3% of em). Decrease if letter spacing is visible.
- `minLetterSpacingFraction`: Start at -0.02. Might need -0.03 for Hershey fonts.
- Glue stretch/shrink ratios: Start at 50%/33% of natural width. Adjust if lines are too tight/loose.
- `hyphenPenalty`: 50 (TeX default). Increase to reduce hyphenation frequency.

**Step 4: Commit tuning changes**

```bash
git add -u
git commit -m "Tune Knuth-Plass parameters for optimal visual quality"
```

---

### Task 10: Remove dead code and clean up

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp` (remove old gap calculation entirely)
- Modify: `src/pdf/pdfgenerator.h` (m_maxJustifyGap may be removable if fully moved to Engine)

**Step 1: Audit renderLineBox**

Verify that all justify logic now reads from `line.justify` and no old gap-counting code remains. Remove any dead code paths.

**Step 2: Audit breakIntoLines**

Ensure the greedy fallback path is clean and also populates JustifyInfo correctly.

**Step 3: Build**

```bash
make -C build -j$(($(nproc)-1))
```

**Step 4: Commit**

```bash
git add -u
git commit -m "Clean up: remove old justify gap calculation, consolidate in layout engine"
```
