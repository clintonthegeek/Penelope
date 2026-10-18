#include "blockidentity.h"
#include "contentplaintext.h"

#include <QList>
#include <QSet>

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

static quint64 idOf(const Content::BlockNode &node) {
    return std::visit([](const auto &block) { return block.blockId; }, node);
}

static void setId(Content::BlockNode &node, quint64 id) {
    std::visit([&](auto &block) { block.blockId = id; }, node);
}

static void copyBlockId(const Content::BlockNode &prev, Content::BlockNode &cur,
                        QSet<const Content::BlockNode *> *copied) {
    std::visit([&](const auto &p, auto &c) { c.blockId = p.blockId; }, prev, cur);
    if (copied)
        copied->insert(&cur);
}

static void collectIds(const QList<Content::BlockNode> &blocks, QSet<quint64> *ids) {
    for (const auto &block : blocks) {
        ids->insert(idOf(block));
        if (const auto *quote = std::get_if<Content::BlockQuote>(&block))
            collectIds(quote->children, ids);
        if (const auto *list = std::get_if<Content::List>(&block)) {
            for (const auto &item : list->items)
                collectIds(item.children, ids);
        }
    }
}

static void restabilizeChildren(const Content::BlockNode &prev, Content::BlockNode &cur,
                                QSet<const Content::BlockNode *> *copied);

static void restabilizeBlockIdsImpl(const QList<Content::BlockNode> &previous,
                                    QList<Content::BlockNode> &current,
                                    QSet<const Content::BlockNode *> *copied) {
    const int nPrev = previous.size();
    const int nCur = current.size();
    int i = 0, j = 0;
    while (i < nPrev && j < nCur) {
        if (blockFingerprint(previous[i]) == blockFingerprint(current[j])) {
            copyBlockId(previous[i], current[j], copied);
            restabilizeChildren(previous[i], current[j], copied);
            ++i;
            ++j;
            continue;
        }
        // One block replaced. Copy the following block's id so a repeated
        // neighbor keeps its identity, and leave the edited block unmatched.
        if (i + 1 < nPrev && j + 1 < nCur
            && blockFingerprint(previous[i + 1]) == blockFingerprint(current[j + 1])) {
            copyBlockId(previous[i + 1], current[j + 1], copied);
            restabilizeChildren(previous[i + 1], current[j + 1], copied);
            i += 2;
            j += 2;
            continue;
        }
        constexpr int win = 4;
        const int iEnd = qMin(i + win, nPrev);
        const int jEnd = qMin(j + win, nCur);
        bool found = false;
        int foundI = i, foundJ = j;
        for (int jj = j; jj < jEnd && !found; ++jj) {
            for (int ii = i; ii < iEnd; ++ii) {
                const QString fp = blockFingerprint(previous[ii]);
                if (fp != blockFingerprint(current[jj]))
                    continue;
                int cPrev = 0, cCur = 0;
                for (int x = i; x < iEnd; ++x)
                    if (blockFingerprint(previous[x]) == fp) ++cPrev;
                for (int x = j; x < jEnd; ++x)
                    if (blockFingerprint(current[x]) == fp) ++cCur;
                if (cPrev == 1 && cCur == 1) {
                    found = true;
                    foundI = ii;
                    foundJ = jj;
                }
            }
        }
        if (!found)
            break;
        copyBlockId(previous[foundI], current[foundJ], copied);
        restabilizeChildren(previous[foundI], current[foundJ], copied);
        i = foundI + 1;
        j = foundJ + 1;
    }
}

static void reassignCollisions(QList<Content::BlockNode> &blocks,
                               const QSet<const Content::BlockNode *> &copied,
                               QSet<quint64> *reserved,
                               quint64 *next) {
    for (auto &block : blocks) {
        quint64 id = idOf(block);
        const bool kept = copied.contains(&block);
        if (!kept && (id == 0 || reserved->contains(id))) {
            while (reserved->contains(*next))
                ++(*next);
            id = (*next)++;
            setId(block, id);
        }
        reserved->insert(id);
        if (auto *quote = std::get_if<Content::BlockQuote>(&block))
            reassignCollisions(quote->children, copied, reserved, next);
        if (auto *list = std::get_if<Content::List>(&block)) {
            for (auto &item : list->items)
                reassignCollisions(item.children, copied, reserved, next);
        }
    }
}

static void restabilizeChildren(const Content::BlockNode &prev, Content::BlockNode &cur,
                                QSet<const Content::BlockNode *> *copied) {
    if (auto *pp = std::get_if<Content::BlockQuote>(&prev)) {
        if (auto *cc = std::get_if<Content::BlockQuote>(&cur))
            restabilizeBlockIdsImpl(pp->children, cc->children, copied);
    }
    if (auto *pp = std::get_if<Content::List>(&prev)) {
        if (auto *cc = std::get_if<Content::List>(&cur)) {
            if (pp->items.size() == cc->items.size()) {
                for (int i = 0; i < pp->items.size(); ++i)
                    restabilizeBlockIdsImpl(pp->items[i].children, cc->items[i].children, copied);
            }
        }
    }
}

void restabilizeBlockIds(const QList<Content::BlockNode> &previous,
                         QList<Content::BlockNode> &current) {
    QSet<const Content::BlockNode *> copied;
    restabilizeBlockIdsImpl(previous, current, &copied);
    QSet<quint64> reserved;
    collectIds(previous, &reserved);
    quint64 next = 1;
    for (quint64 id : reserved)
        next = qMax(next, id + 1);
    reassignCollisions(current, copied, &reserved, &next);
}
