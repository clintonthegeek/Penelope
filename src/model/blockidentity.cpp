#include "blockidentity.h"
#include "contentplaintext.h"

#include <QList>

static QString blockTag(const Content::BlockNode &b) {
    return std::visit([](const auto &x) -> QString {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, Content::Paragraph>) return QStringLiteral("P");
        else if constexpr (std::is_same_v<T, Content::Heading>) return QStringLiteral("H");
        else if constexpr (std::is_same_v<T, Content::CodeBlock>) return QStringLiteral("C");
        else if constexpr (std::is_same_v<T, Content::BlockQuote>) return QStringLiteral("Q");
        else if constexpr (std::is_same_v<T, Content::List>) return QStringLiteral("L");
        else if constexpr (std::is_same_v<T, Content::Table>) return QStringLiteral("T");
        else if constexpr (std::is_same_v<T, Content::HorizontalRule>) return QStringLiteral("R");
        else if constexpr (std::is_same_v<T, Content::FootnoteSection>) return QStringLiteral("F");
        else return QStringLiteral("?");
    }, b);
}

static QString blockFingerprint(const Content::BlockNode &b) {
    Content::Document d;
    d.blocks.append(b);
    return blockTag(b) + QStringLiteral("\n") + Content::toPlainText(d);
}

static void copyBlockId(const Content::BlockNode &prev, Content::BlockNode &cur) {
    std::visit([&](const auto &p, auto &c) { c.blockId = p.blockId; }, prev, cur);
}

static void restabilizeChildren(const Content::BlockNode &prev, Content::BlockNode &cur) {
    std::visit([&](const auto &p, auto &c) {
        using P = std::decay_t<decltype(p)>;
        using C = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<P, Content::BlockQuote> && std::is_same_v<C, Content::BlockQuote>) {
            restabilizeBlockIds(p.children, c.children);
        } else if constexpr (std::is_same_v<P, Content::List> && std::is_same_v<C, Content::List>) {
            if (p.items.size() == c.items.size()) {
                for (int i = 0; i < p.items.size(); ++i) {
                    // items are not blocks; restabilize their children
                    // Need mutable access to c.items[i].children
                }
            }
        }
    }, prev, cur);
    // Handle list items separately (children mutable)
    if (auto *pp = std::get_if<Content::List>(&prev)) {
        if (auto *cc = std::get_if<Content::List>(&cur)) {
            if (pp->items.size() == cc->items.size()) {
                for (int i = 0; i < pp->items.size(); ++i) {
                    // children are QList<BlockNode> in item; const in pp? pp is const ref? Actually prev param const
                    QList<Content::BlockNode> prevChildren = pp->items[i].children;
                    restabilizeBlockIds(prevChildren, cc->items[i].children);
                }
            }
        }
    }
}

void restabilizeBlockIds(const QList<Content::BlockNode> &previous,
                         QList<Content::BlockNode> &current) {
    const int nPrev = previous.size();
    const int nCur = current.size();
    int i = 0, j = 0;
    while (i < nPrev && j < nCur) {
        if (blockFingerprint(previous[i]) == blockFingerprint(current[j])) {
            copyBlockId(previous[i], current[j]);
            restabilizeChildren(previous[i], current[j]);
            ++i; ++j;
            continue;
        }
        // search window
        int win = 4;
        int iEnd = qMin(i + win, nPrev);
        int jEnd = qMin(j + win, nCur);
        bool found = false;
        int foundI = i, foundJ = j;
        for (int jj = j; jj < jEnd && !found; ++jj) {
            for (int ii = i; ii < iEnd; ++ii) {
                if (blockFingerprint(previous[ii]) != blockFingerprint(current[jj]))
                    continue;
                // occurrence counts in windows
                int cPrev = 0, cCur = 0;
                for (int x = i; x < iEnd; ++x)
                    if (blockFingerprint(previous[x]) == blockFingerprint(previous[ii])) ++cPrev;
                for (int x = j; x < jEnd; ++x)
                    if (blockFingerprint(current[x]) == blockFingerprint(current[jj])) ++cCur;
                if (cPrev == 1 && cCur == 1) {
                    found = true;
                    foundI = ii;
                    foundJ = jj;
                    break;
                }
            }
        }
        if (found) {
            if (foundI == i && foundJ == j) {
                // no progress possible; advance
                ++i; ++j;
            } else {
                i = foundI;
                j = foundJ;
            }
            continue;
        }
        ++i; ++j;
    }
}
