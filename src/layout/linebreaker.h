/*
 * linebreaker.h — Knuth-Plass optimal line breaking
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PENELOPE_LINEBREAKER_H
#define PENELOPE_LINEBREAKER_H

#include <QList>

namespace LineBreaking {

struct Box {
    qreal width = 0;
    int wordIndex = -1;
};

struct Glue {
    qreal width = 0;
    qreal stretch = 0;
    qreal shrink = 0;
};

struct Penalty {
    qreal width = 0;
    qreal penalty = 0;
    bool flagged = false;
};

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

enum FitnessClass { Tight = 0, Normal = 1, Loose = 2, VeryLoose = 3 };

struct Breakpoint {
    int itemIndex = 0;
    qreal adjustmentRatio = 0;
    FitnessClass fitness = Normal;
    qreal totalDemerits = 0;
};

struct BreakResult {
    QList<Breakpoint> breaks;
    bool optimal = true;
};

struct Config {
    qreal tolerance = 1.0;
    qreal looseTolerance = 4.0;
    qreal hyphenPenalty = 50.0;
    qreal consecutiveHyphenDemerits = 3000.0;
    qreal fitnessDemerits = 100.0;
    bool enableHyphenation = true;
    qreal maxLetterSpacingFraction = 0.03;
    qreal minLetterSpacingFraction = -0.02;
};

BreakResult findBreaks(const QList<Item> &items,
                       const QList<qreal> &lineWidths,
                       const Config &config = {});

BreakResult findBreaksTiered(const QList<Item> &items,
                             const QList<qreal> &lineWidths,
                             const Config &baseConfig = {});

qreal computeAdjustmentRatio(const QList<Item> &items,
                              int start, int end,
                              qreal lineWidth);

} // namespace LineBreaking

#endif // PENELOPE_LINEBREAKER_H
