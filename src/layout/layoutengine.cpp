/*
 * layoutengine.cpp — Content → box tree, line/page breaking
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "layoutengine.h"
#include "codespancollector.h"
#include "fontmanager.h"
#include "linebreaker.h"
#include "fragmentcache.h"
#include "pagefingerprint.h"
#include "stylerunindex.h"
#include "contentplaintext.h"
#include "headerfooterplan.h"
#include "penelopesettings.h"
#include "textshaper.h"


#include <QMarginsF>
#include <QSet>
#include <QtMath>

#include <unicode/brkiter.h>
#include <unicode/unistr.h>

namespace {

quint64 blockContentKey(const Content::BlockNode &node)
{
    Content::Document one;
    one.blocks.append(node);
    const quint64 id = std::visit([](const auto &block) { return block.blockId; }, node);
    return qHashMulti(quint64(0), id, qHash(Content::toPlainText(one)));
}

} // namespace

namespace Layout {

Engine::Engine(FontManager *fontManager, TextShaper *textShaper)
    : m_fontManager(fontManager)
    , m_textShaper(textShaper)
{
}

// --- Main entry point ---

LayoutResult Engine::layout(const Content::Document &doc, const PageLayout &pageLayout)
{
    m_footnoteStyle = doc.footnoteStyle;
    m_perPageNumbering = false;
    m_breakCancelled = false;
    LayoutResult result;
    result.pageSize = pageLayout.pageSizePoints();

    QSizeF contentSize = pageLayout.contentSizePoints();
    qreal availWidth = contentSize.width();

    LayoutCursor cursor;
    Page page;
    while (layoutNextPage(doc, pageLayout, &cursor, &page)) {
        if (m_breakCancelled) return {};
        result.pages.append(std::move(page));
        page = Page{};
    }
    if (m_breakCancelled) return {};
    if (!page.elements.isEmpty()) result.pages.append(std::move(page));
    if (result.pages.isEmpty()) result.pages.append(Page{});
    buildSourceMap(result, pageLayout);
    return result;
}

LayoutResult Engine::layoutReusing(const Content::Document &doc, const PageLayout &pageLayout,
                                   const LayoutResult *previous, bool geometryChanged)
{
    m_footnoteStyle = doc.footnoteStyle;
    m_perPageNumbering = false;
    auto fresh = [&]() { return layout(doc, pageLayout); };
    if (!previous || geometryChanged || previous->pages.isEmpty())
        return fresh();
    if (headerFooterUsesPageToken(pageLayout))
        return fresh();

    LayoutResult result;
    result.pageSize = pageLayout.pageSizePoints();
    LayoutCursor cursor;
    for (const Page &old : previous->pages) {
        if (!old.boundary || old.hasTable || old.hasFootnote || old.blockIds.isEmpty())
            break;
        if (cursor.nextBlock + old.blockIds.size() > doc.blocks.size())
            break;
        QList<quint64> ids;
        quint64 hash = 0;
        bool same = true;
        for (int k = 0; k < old.blockIds.size(); ++k) {
            const auto &node = doc.blocks.at(cursor.nextBlock + k);
            const quint64 id = std::visit([](const auto &b) { return b.blockId; }, node);
            hash = qHashMulti(hash, blockContentKey(node));
            if (id != old.blockIds.at(k))
                same = false;
            ids.append(id);
        }
        if (!same || hash != old.contentHash)
            break;
        const quint64 fp = pageFingerprint(cursor, ids, hash, cursor.prevKeepWithNext,
                                            old.tableHeaderRows, old.noteCount);
        if (fp != old.fingerprint)
            break;
        Page copy = old;
        copy.pageNumber = cursor.currentPageNumber;
        result.pages.append(copy);
        cursor.nextBlock += ids.size();
        cursor.currentPageNumber++;
        cursor.yOnPage = 0;
        cursor.y = 0;
    }
    if (cursor.nextBlock < doc.blocks.size() || result.pages.isEmpty()) {
        Page page;
        while (layoutNextPage(doc, pageLayout, &cursor, &page)) {
            if (m_breakCancelled) return {};
            result.pages.append(std::move(page));
            page = Page{};
        }
        if (m_breakCancelled) return {};
        if (!page.elements.isEmpty()) result.pages.append(std::move(page));
    }
    if (result.pages.isEmpty()) result.pages.append(Page{});
    buildSourceMap(result, pageLayout);
    return result;
}

void Engine::buildSourceMap(LayoutResult &result, const PageLayout &pageLayout) const
{
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
}

// --- Continuous (web-view) layout ---

ContinuousLayoutResult Engine::layoutContinuous(const Content::Document &doc, qreal availWidth)
{
    m_footnoteStyle = doc.footnoteStyle;
    m_perPageNumbering = false;
    m_breakCancelled = false;
    ContinuousLayoutResult result;
    result.contentWidth = availWidth;

    LayoutCursor cursor;
    LayoutBudget budget; // empty => no caps
    while (!cursor.finished) {
        auto part = layoutContinuousFrom(doc, availWidth, &cursor, budget);
        if (m_breakCancelled) return {};
        qreal offset = result.totalHeight;
        for (auto &el : part.elements) {
            std::visit([&](auto &e) { e.y += offset; }, el);
            result.elements.append(el);
        }
        for (auto &sm : part.sourceMap) {
            sm.rect.translate(0, offset);
            result.sourceMap.append(sm);
        }
        result.codeBlockRegions.append(part.codeBlockRegions);
        result.totalHeight += part.totalHeight;
        if (part.elements.isEmpty()) break;
    }
    return result;
}

ContinuousLayoutResult Engine::layoutContinuousFrom(const Content::Document &doc, qreal width,
                                                  LayoutCursor *cursor, const LayoutBudget &budget)
{
    m_footnoteStyle = doc.footnoteStyle;
    m_perPageNumbering = false;
    ContinuousLayoutResult result;
    result.contentWidth = width;

    if (!cursor) return result;

    QList<PageElement> elements;
    qreal projectedY = cursor->y;
    int blocksDone = 0;
    int bi = cursor->nextBlock;

    for (; bi < doc.blocks.size(); ++bi) {
        if (m_cancelled && m_cancelled()) { m_breakCancelled = true; return {}; }

        if (budget.maxBlocks > 0 && blocksDone >= budget.maxBlocks) break;

        QList<PageElement> blockElems = layoutBlockNode(doc.blocks[bi], width);

        if (m_breakCancelled) return {};

        // estimate contribution
        qreal contrib = 0;
        for (const auto &el : blockElems) {
            std::visit([&](const auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, BlockBox>) {
                    contrib += e.spaceBefore + e.height + e.spaceAfter;
                } else if constexpr (std::is_same_v<T, TableBox>) {
                    contrib += e.height;
                } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                    contrib += 20.0 + e.height;
                }
            }, el);
        }

        if (budget.maxHeight > 0 && !elements.isEmpty() && projectedY + contrib > cursor->y + budget.maxHeight) {
            break;
        }

        elements.append(blockElems);
        projectedY += contrib;
        ++blocksDone;
    }

    // stacking pass
    qreal y = 0;
    for (auto &element : elements) {
        std::visit([&](auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox>) {
                y += e.spaceBefore;
                e.y = y;
                y += e.height + e.spaceAfter;
                if (e.source.startLine > 0) {
                    SourceMapEntry entry;
                    entry.pageNumber = 0;
                    entry.rect = QRectF(e.x, e.y, e.width, e.height);
                    entry.startLine = e.source.startLine;
                    entry.endLine = e.source.endLine;
                    result.sourceMap.append(entry);
                    if (e.type == BlockBox::CodeBlockType) {
                        CodeBlockRegion region;
                        region.pageNumber = 0;
                        region.rect = QRectF(e.x - e.padding, e.y - e.padding, e.width + e.padding * 2, e.height + e.padding * 2);
                        region.startLine = e.source.startLine;
                        region.endLine = e.source.endLine;
                        result.codeBlockRegions.append(region);
                    }
                }
            } else if constexpr (std::is_same_v<T, TableBox>) {
                e.y = y;
                y += e.height;
                if (e.source.startLine > 0) {
                    SourceMapEntry entry;
                    entry.pageNumber = 0;
                    entry.rect = QRectF(e.x, e.y, e.width, e.height);
                    entry.startLine = e.source.startLine;
                    entry.endLine = e.source.endLine;
                    result.sourceMap.append(entry);
                }
            } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                y += 20.0;
                e.y = y;
                y += e.height;
            }
        }, element);
    }

    result.elements = std::move(elements);
    result.totalHeight = y;

    cursor->nextBlock = bi;
    cursor->y += y;
    cursor->finished = (cursor->nextBlock >= doc.blocks.size());
    return result;
}

bool Engine::layoutNextPage(const Content::Document &doc, const PageLayout &pageLayout, LayoutCursor *cursor, Page *outPage) {
    m_footnoteStyle = doc.footnoteStyle;
    m_notesAtFoot = !m_footnoteStyle.asEndnotes;
    // A restart only means something when the notes sit on their own page.
    m_perPageNumbering = m_notesAtFoot && m_footnoteStyle.restart == FootnoteStyle::PerPage;
    m_pendingNoteSeed = cursor->pendingNoteIndices;
    beginPageNumbering();
    if (!cursor || cursor->finished || !outPage) return false;
    if (m_breakCancelled) return false;

    QSizeF contentSize = pageLayout.contentSizePoints();
    qreal pageHeight = contentSize.height();
    const qreal contentWidth = contentSize.width();
    const LayoutCursor incoming = *cursor;

    auto blockIdOf = [](const Content::BlockNode &node) {
        return std::visit([](const auto &b) { return b.blockId; }, node);
    };
    // nextBlock is the first block not yet placed. The queue may already hold
    // its shaped elements (and later prefetched blocks). pullBlock is the first
    // block that still has to be shaped, so a carried-over block is not laid out twice.
    // The first block that still has to be shaped for a given cursor state.
    auto initialPullBlock = [&](const LayoutCursor &state) {
        int pull = state.nextBlock;
        if (state.queue.isEmpty())
            return pull;
        QSet<quint64> queuedIds;
        for (int qi = state.queueIndex; qi < state.queue.size(); ++qi) {
            quint64 id = 0;
            std::visit([&](const auto &element) { id = element.blockId; }, state.queue.at(qi));
            if (id != 0)
                queuedIds.insert(id);
        }
        while (pull < doc.blocks.size() && queuedIds.contains(blockIdOf(doc.blocks.at(pull))))
            ++pull;
        return pull;
    };
    int pullBlock = initialPullBlock(*cursor);
    auto pullUntil = [&](int index) -> bool {
        while (cursor->queue.size() <= index) {
            if (pullBlock >= doc.blocks.size())
                return false;
            if (m_cancelled && m_cancelled()) {
                m_breakCancelled = true;
                return false;
            }
            const int bi = pullBlock;
            auto elems = layoutBlockNode(doc.blocks.at(bi), contentWidth);
            if (m_notesAtFoot) {
                // The notes are placed per page; the trailing section is not part
                // of the page body.
                elems.removeIf([](const PageElement &element) {
                    return std::holds_alternative<FootnoteSectionBox>(element);
                });
            }
            if (m_breakCancelled)
                return false;
            ++pullBlock;
            cursor->queue.append(elems);
            if (elems.isEmpty() && cursor->queue.size() <= index)
                continue;
        }
        return cursor->queue.size() > index;
    };

    Page currentPage;
    currentPage.pageNumber = cursor->currentPageNumber;
    qreal y = cursor->yOnPage;
    QList<quint64> pageIds;
    quint64 pageHash = 0;

    auto finishPage = [&]() {
        currentPage.contentHeight = y;
        currentPage.blockIds = pageIds;
        pageHash = 0;
        int searchFrom = incoming.nextBlock;
        for (quint64 id : pageIds) {
            for (int bi = searchFrom; bi < doc.blocks.size(); ++bi) {
                if (blockIdOf(doc.blocks.at(bi)) != id)
                    continue;
                pageHash = qHashMulti(pageHash, blockContentKey(doc.blocks.at(bi)));
                searchFrom = bi + 1;
                break;
            }
        }
        currentPage.contentHash = pageHash;
        bool midBlock = false;
        if (cursor->queueIndex < cursor->queue.size()) {
            const auto &nextElement = cursor->queue.at(cursor->queueIndex);
            if (const auto *block = std::get_if<BlockBox>(&nextElement))
                midBlock = !block->isFragmentStart;
            else if (const auto *table = std::get_if<TableBox>(&nextElement))
                midBlock = pageIds.contains(table->blockId);
            else if (const auto *notes = std::get_if<FootnoteSectionBox>(&nextElement))
                midBlock = pageIds.contains(notes->blockId);
        }
        currentPage.boundary = !midBlock;
        currentPage.fingerprint = pageFingerprint(incoming, currentPage.blockIds, currentPage.contentHash,
                                                  incoming.prevKeepWithNext, currentPage.tableHeaderRows,
                                                  currentPage.noteCount);
        *outPage = currentPage;
        cursor->yOnPage = 0;
        cursor->y = 0;
        cursor->currentPageNumber++;
        int rewind = 0;
        quint64 lastId = 0;
        bool anyRewind = false;
        for (int qi = cursor->queueIndex; qi < cursor->queue.size(); ++qi) {
            quint64 id = 0;
            std::visit([&](const auto &element) { id = element.blockId; }, cursor->queue.at(qi));
            if (id == 0 || pageIds.contains(id))
                continue;
            if (!anyRewind || id != lastId) {
                ++rewind;
                anyRewind = true;
                lastId = id;
            }
        }
        cursor->nextBlock = qMax(0, pullBlock - rewind);
        if (cursor->queueIndex > 0 && cursor->queueIndex <= cursor->queue.size()) {
            cursor->queue = cursor->queue.mid(cursor->queueIndex);
            cursor->queueIndex = 0;
        }
        if (m_perPageNumbering) {
            // Markers were assigned for this page; anything unplaced has to be
            // shaped again with the next page's numbering.
            cursor->queue.clear();
            cursor->queueIndex = 0;
        }
        if (cursor->queue.isEmpty() && cursor->nextBlock >= doc.blocks.size()
            && cursor->pendingNoteIndices.isEmpty())
            cursor->finished = true;
    };

    auto remember = [&](const PageElement &el) {
        quint64 id = 0;
        std::visit([&](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox> || std::is_same_v<T, TableBox>
                          || std::is_same_v<T, FootnoteSectionBox>)
                id = e.blockId;
        }, el);
        if (id != 0 && (pageIds.isEmpty() || pageIds.last() != id))
            pageIds.append(id);
    };

    const bool notesAtFoot = m_notesAtFoot;
    auto fillBody = [&](qreal limit) {
    while (pullUntil(cursor->queueIndex)) {
        if (m_breakCancelled)
            return;

        // pullUntil appends to the queue and can reallocate it. Any reference
        // taken before that call points at a destroyed element (a valueless
        // variant), so refresh the pointer after every pull.
        const PageElement *element = &cursor->queue[cursor->queueIndex];

        if (auto *tablePtr = std::get_if<TableBox>(element)) {
            const TableBox &table = *tablePtr;
            qreal remaining = limit - y;
            if (table.height <= remaining) {
                TableBox positioned = table;
                positioned.y = y;
                currentPage.elements.append(positioned);
                remember(positioned);
                currentPage.hasTable = true;
                currentPage.tableHeaderRows += table.headerRowCount;
                y += table.height;
                ++cursor->queueIndex;
                continue;
            }
            qreal firstRowsHeight = 0;
            const int firstRows = qMin(int(table.rows.size()), table.headerRowCount + 1);
            for (int i = 0; i < firstRows; ++i) firstRowsHeight += table.rows[i].height;
            if (firstRowsHeight > remaining && !currentPage.elements.isEmpty()) {
                return;
            }
            auto slices = splitTable(table, remaining, pageHeight);
            if (!slices.isEmpty()) {
                slices[0].y = y;
                currentPage.elements.append(slices[0]);
                remember(slices[0]);
                currentPage.hasTable = true;
                currentPage.tableHeaderRows += slices[0].headerRowCount;
                y += slices[0].height;
                ++cursor->queueIndex;
                for (int si = slices.size() - 1; si >= 1; --si)
                    cursor->queue.insert(cursor->queueIndex, slices[si]);
                if (slices.size() > 1) {
                    return;
                }
                continue;
            }
            if (!currentPage.elements.isEmpty()) {
                return;
            }
            TableBox positioned = table;
            positioned.y = y;
            currentPage.elements.append(positioned);
            remember(positioned);
            currentPage.hasTable = true;
            y += table.height;
            ++cursor->queueIndex;
            continue;
        }

        qreal elementHeight = 0;
        qreal spaceBefore = 0;
        qreal spaceAfter = 0;
        bool keepWithNext = false;
        std::visit([&](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox>) {
                elementHeight = e.height;
                spaceBefore = e.spaceBefore;
                spaceAfter = e.spaceAfter;
                keepWithNext = e.keepWithNext;
            } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                elementHeight = e.height;
                spaceBefore = 20.0;
                currentPage.hasFootnote = true;
                currentPage.noteCount += e.footnotes.size();
            }
        }, *element);

        qreal totalHeight = spaceBefore + elementHeight + spaceAfter;
        bool needsPageBreak = (y + totalHeight > limit) && !currentPage.elements.isEmpty();

        if (keepWithNext && !needsPageBreak && !currentPage.elements.isEmpty()) {
            if (pullUntil(cursor->queueIndex + 1)) {
                qreal nextHeight = 0;
                std::visit([&](const auto &ne) {
                    using T = std::decay_t<decltype(ne)>;
                    if constexpr (std::is_same_v<T, BlockBox>) {
                        nextHeight = ne.spaceBefore + ne.height;
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
                }, cursor->queue[cursor->queueIndex + 1]);
                if (y + totalHeight + nextHeight > limit)
                    needsPageBreak = true;
            }
            element = &cursor->queue[cursor->queueIndex];
        }

        if (needsPageBreak) {
            qreal remaining = limit - y;
            bool didSplit = false;
            if (auto *bb = std::get_if<BlockBox>(element)) {
                auto split = splitBlockBox(*bb, remaining);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += f1.spaceBefore;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    remember(f1);
                    y += f1.height + f1.spaceAfter;
                    cursor->queue.insert(cursor->queueIndex + 1, f2);
                    ++cursor->queueIndex;
                    didSplit = true;
                }
            } else if (auto *fs = std::get_if<FootnoteSectionBox>(element)) {
                auto split = splitFootnoteSection(*fs, remaining);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += 20.0;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    remember(f1);
                    y += f1.height;
                    cursor->queue.insert(cursor->queueIndex + 1, f2);
                    ++cursor->queueIndex;
                    didSplit = true;
                }
            }
            if (didSplit)
                continue;
            return;
        }

        if (currentPage.elements.isEmpty() && y + totalHeight > limit) {
            bool didSplit = false;
            if (auto *bb = std::get_if<BlockBox>(element)) {
                auto split = splitBlockBox(*bb, pageHeight);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += f1.spaceBefore;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    remember(f1);
                    y += f1.height + f1.spaceAfter;
                    cursor->queue.insert(cursor->queueIndex + 1, f2);
                    ++cursor->queueIndex;
                    didSplit = true;
                }
            } else if (auto *fs = std::get_if<FootnoteSectionBox>(element)) {
                auto split = splitFootnoteSection(*fs, pageHeight);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += 20.0;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    remember(f1);
                    y += f1.height;
                    cursor->queue.insert(cursor->queueIndex + 1, f2);
                    ++cursor->queueIndex;
                    didSplit = true;
                }
            }
            if (didSplit)
                continue;
        }

        y += spaceBefore;
        PageElement positioned = *element;
        std::visit([&](auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox> || std::is_same_v<T, FootnoteSectionBox>)
                e.y = y;
        }, positioned);
        currentPage.elements.append(positioned);
        remember(positioned);
        y += elementHeight + spaceAfter;
        ++cursor->queueIndex;
    }
    };

    if (m_breakCancelled) return false;

    // Notes cited on this page print in a band at its foot. The band is
    // measured first and, when it would not fit, the page is laid out once
    // more with the band reserved. The note set can only shrink in that pass,
    // so one retry is enough; anything that still does not fit is carried to
    // the next page's band.
    const LayoutCursor beforeFill = *cursor;
    const qreal beforeY = cursor->yOnPage;
    fillBody(pageHeight);

    if (notesAtFoot && !m_breakCancelled) {
        const QList<int> firstNotes = pageNotes(cursor->pendingNoteIndices, currentPage.elements);
        if (!firstNotes.isEmpty()) {
            FootnoteSectionBox probe = makeNoteBand(doc, firstNotes, contentWidth);
            // A band taller than the page can never be reserved; its notes are
            // carried instead.
            if (probe.height + kNoteBandGap < pageHeight
                && y + kNoteBandGap + probe.height > pageHeight) {
                const LayoutCursor attemptCursor = *cursor;
                const Page attemptPage = currentPage;
                const qreal attemptY = y;
                const QList<quint64> attemptIds = pageIds;

                const QHash<int, int> attemptMarkers = m_pageNoteMarkers;
                const int attemptCounter = m_pageNoteCounter;

                const int attemptPull = pullBlock;

                *cursor = beforeFill;
                pullBlock = initialPullBlock(*cursor);
                currentPage = Page{};
                currentPage.pageNumber = cursor->currentPageNumber;
                pageIds.clear();
                y = beforeY;
                beginPageNumbering();
                fillBody(pageHeight - probe.height - kNoteBandGap);

                const QList<int> retryNotes = pageNotes(cursor->pendingNoteIndices, currentPage.elements);
                FootnoteSectionBox retry = makeNoteBand(doc, retryNotes, contentWidth);
                if (m_breakCancelled || currentPage.elements.isEmpty()
                    || y + kNoteBandGap + retry.height > pageHeight) {
                    *cursor = attemptCursor;
                    currentPage = attemptPage;
                    pageIds = attemptIds;
                    y = attemptY;
                    m_pageNoteMarkers = attemptMarkers;
                    m_pageNoteCounter = attemptCounter;
                    pullBlock = attemptPull;
                }
            }
        }
    }

    if (notesAtFoot) {
        QList<int> notes = pageNotes(cursor->pendingNoteIndices, currentPage.elements);
        cursor->pendingNoteIndices.clear();
        if (m_perPageNumbering) {
            std::sort(notes.begin(), notes.end(), [this](int a, int b) {
                return pageMarker(a) < pageMarker(b);
            });
        }
        if (!notes.isEmpty()) {
            FootnoteSectionBox band = makeNoteBand(doc, notes, contentWidth);
            qreal bandY = pageHeight - band.height;
            QList<int> dropped;
            while (!notes.isEmpty() && bandY < y + kNoteBandGap) {
                dropped.prepend(notes.takeLast());
                band = makeNoteBand(doc, notes, contentWidth);
                bandY = pageHeight - band.height;
            }
            cursor->pendingNoteIndices = dropped;
            if (!notes.isEmpty()) {
                band.x = 0;
                band.y = bandY;
                band.width = contentWidth;
                currentPage.elements.append(band);
                currentPage.hasFootnote = true;
                currentPage.noteCount += notes.size();
                y = qMax(y, bandY + band.height);
            }
        }
    }

    if (currentPage.elements.isEmpty() && cursor->pendingNoteIndices.isEmpty()) {
        cursor->finished = true;
        return false;
    }
    finishPage();
    return true;
}



QImage Engine::cachedImage(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    const QString key = QString::number(qHashBits(data.constData(), data.size()), 16);
    if (const QImage *cached = m_imageCache.object(key))
        return *cached;
    QImage loaded = QImage::fromData(data);
    if (loaded.isNull())
        return {};
    ++m_imageDecodes;
    if (m_imageCache.maxCost() == 0)
        m_imageCache.setMaxCost(kImageCacheBytes);
    m_imageCache.insert(key, new QImage(loaded),
                        qMax(1, int(loaded.sizeInBytes() / 1024)));
    return loaded;
}

icu::BreakIterator *Engine::lineBreakIterator()
{
    if (!m_lineBreakIterator) {
        UErrorCode err = U_ZERO_ERROR;
        m_lineBreakIterator.reset(icu::BreakIterator::createLineInstance(
            icu::Locale::getDefault(), err));
        if (U_FAILURE(err))
            m_lineBreakIterator.reset();
        ++m_breakIteratorCreations;
    }
    return m_lineBreakIterator.get();
}

FootnoteNumbering Engine::numberingContext() const
{
    FootnoteNumbering numbering;
    numbering.style = m_footnoteStyle;
    numbering.perPage = m_perPageNumbering;
    if (m_perPageNumbering) {
        numbering.pageMarkers = const_cast<QHash<int, int> *>(&m_pageNoteMarkers);
        numbering.pageCounter = const_cast<int *>(&m_pageNoteCounter);
    }
    return numbering;
}

void Engine::beginPageNumbering()
{
    m_pageNoteMarkers.clear();
    m_pageNoteCounter = 0;
    if (!m_perPageNumbering)
        return;
    // Notes carried from an earlier page keep the first markers of this page.
    for (int index : m_pendingNoteSeed)
        m_pageNoteMarkers.insert(index, ++m_pageNoteCounter);
    m_pendingNoteSeed.clear();
}

QString Engine::pageMarker(int index)
{
    if (!m_perPageNumbering)
        return Content::footnoteMarker(m_footnoteStyle, index);
    auto it = m_pageNoteMarkers.constFind(index);
    if (it == m_pageNoteMarkers.constEnd())
        it = m_pageNoteMarkers.insert(index, ++m_pageNoteCounter);
    return m_footnoteStyle.formatNumber(m_footnoteStyle.startNumber + it.value() - 1);
}

Content::FootnoteSection Engine::selectNotes(const Content::Document &doc,
                                             const QList<int> &indices) const
{
    Content::FootnoteSection section;
    for (const auto &block : doc.blocks) {
        const auto *full = std::get_if<Content::FootnoteSection>(&block);
        if (!full)
            continue;
        section.showSeparator = full->showSeparator;
        section.separatorLength = full->separatorLength;
        section.separatorWidth = full->separatorWidth;
        for (int index : indices) {
            if (index >= 0 && index < full->footnotes.size())
                section.footnotes.append(full->footnotes.at(index));
        }
        break;
    }
    return section;
}

QList<int> Engine::pageNotes(const QList<int> &pending, const QList<PageElement> &elements)
{
    QList<int> notes = pending;
    for (const auto &element : elements) {
        QList<int> cited;
        if (const auto *box = std::get_if<BlockBox>(&element)) {
            cited = box->footnoteIndices;
        } else if (const auto *table = std::get_if<TableBox>(&element)) {
            for (const auto &row : table->rows)
                for (const auto &cell : row.cells)
                    for (int index : cell.footnoteIndices)
                        if (!cited.contains(index))
                            cited.append(index);
        }
        for (int index : cited) {
            if (!notes.contains(index))
                notes.append(index);
        }
    }
    std::sort(notes.begin(), notes.end());
    return notes;
}

FootnoteSectionBox Engine::makeNoteBand(const Content::Document &doc, const QList<int> &indices,
                                        qreal width)
{
    if (indices.isEmpty())
        return {};
    // A band never repeats the endnote section's separator: the rule marks the
    // start of the note area, which each page's band draws for itself.
    Content::FootnoteSection section = selectNotes(doc, indices);
    m_noteLabelOverrides.clear();
    if (m_perPageNumbering) {
        for (int index : indices)
            m_noteLabelOverrides.insert(index, pageMarker(index));
    }
    FootnoteSectionBox band = layoutFootnoteSection(section, width);
    m_noteLabelOverrides.clear();
    band.blockId = 0;
    return band;
}

QList<PageElement> Engine::layoutBlockNode(const Content::BlockNode &block, qreal availWidth)
{
    QList<PageElement> elements;
    std::visit([&](const auto &b) {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, Content::Paragraph>) {
            elements.append(layoutParagraphElements(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::Heading>) {
            elements.append(layoutHeading(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
            elements.append(layoutCodeBlock(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
            elements.append(layoutBlockQuote(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::List>) {
            elements.append(layoutList(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::Table>) {
            elements.append(layoutTable(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::HorizontalRule>) {
            elements.append(layoutHorizontalRule(b, availWidth));
        } else if constexpr (std::is_same_v<T, Content::FootnoteSection>) {
            elements.append(layoutFootnoteSection(b, availWidth));
        }
    }, block);
    return elements;
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


namespace {
// Notes cited inside a block, in reference order.
QList<int> citedNotes(const QList<Content::InlineNode> &inlines)
{
    QList<int> indices;
    for (const auto &node : inlines) {
        if (const auto *ref = std::get_if<Content::FootnoteRef>(&node)) {
            if (!indices.contains(ref->index))
                indices.append(ref->index);
        }
    }
    return indices;
}
} // namespace

CollectedText collectInlines(const QList<Content::InlineNode> &inlines,
                              const Content::TextStyle &baseStyle,
                              const FootnoteNumbering &numbering,
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
            } else if constexpr (std::is_same_v<T, Content::Math>) {
                Content::TextStyle mathStyle = n.style;
                mathStyle.fontFamily = QStringLiteral("Noto Serif");
                mathStyle.italic = true;
                mathStyle.fontFeatures.clear();
                const int startPos = result.text.size();
                result.text.append(n.source);
                StyleRun sr;
                sr.start = startPos;
                sr.length = n.source.size();
                sr.fontFamily = mathStyle.fontFamily;
                sr.fontWeight = mathStyle.fontWeight;
                sr.fontItalic = true;
                sr.fontSize = mathStyle.fontSize;
                result.styleRuns.append(sr);
                result.textStyles.append(mathStyle);
                if (markdownMode) {
                    MarkdownRange range;
                    range.textStart = startPos;
                    range.textEnd = startPos + n.source.size();
                    range.prefix = n.display ? QStringLiteral("$$") : QStringLiteral("$");
                    range.suffix = range.prefix;
                    result.markdownRanges.append(range);
                }
            } else if constexpr (std::is_same_v<T, Content::FootnoteRef>) {
                const QString marker = numbering.markerFor(n.index);
                Content::TextStyle refStyle = n.style;
                refStyle.linkHref = Content::footnoteRefUrl(n.index);
                refStyle.linkKind = Content::LinkKind::Anchor;
                StyleRun sr;
                sr.start = result.text.size();
                sr.length = marker.size();
                sr.fontFamily = n.style.fontFamily;
                sr.fontWeight = n.style.fontWeight;
                sr.fontItalic = n.style.italic;
                sr.fontSize = n.style.fontSize;
                sr.fontFeatures = n.style.fontFeatures;
                result.styleRuns.append(sr);
                result.textStyles.append(refStyle);
                result.text.append(marker);
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
    int idx = styleRunIndexAt(charPos, collected.styleRuns);
    if (idx >= 0 && idx < collected.textStyles.size())
        return collected.textStyles[idx];
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

// Inline images split the run: text before them, the image on its own line,
// text after. Paragraphs hoist images out before they get here; headings and
// table cells come through this path.
QList<LineBox> Engine::breakIntoLines(const QList<Content::InlineNode> &inlines,
                                      const Content::TextStyle &baseStyle,
                                      const Content::ParagraphFormat &format,
                                      qreal availWidth,
                                      bool markdownRanges)
{
    QList<LineBox> result;
    QList<Content::InlineNode> segment;
    auto flushSegment = [&]() {
        if (segment.isEmpty())
            return;
        result += breakTextIntoLines(segment, baseStyle, format, availWidth, markdownRanges);
        segment.clear();
    };
    for (const auto &node : inlines) {
        if (const auto *math = std::get_if<Content::Math>(&node)) {
            if (!math->display) {
                segment.append(node);
                continue;
            }
            // Display math is a formula line of its own, centred.
            flushSegment();
            Content::ParagraphFormat centred = format;
            centred.alignment = Qt::AlignCenter;
            const QList<Content::InlineNode> formula{node};
            result += breakTextIntoLines(formula, baseStyle, centred, availWidth, markdownRanges);
            continue;
        }
        const auto *image = std::get_if<Content::InlineImage>(&node);
        if (!image) {
            segment.append(node);
            continue;
        }
        flushSegment();
        ImageBox box = makeInlineImageBox(*image, availWidth);
        if (box.image.isNull() || box.width <= 0) {
            // Keep the alt text when the bytes are missing.
            Content::TextRun fallback;
            fallback.text = QStringLiteral("[Image: %1]").arg(image->altText);
            fallback.style = baseStyle;
            segment.append(fallback);
            continue;
        }
        LineBox line;
        line.width = box.width;
        line.height = box.height;
        line.baseline = box.height;
        line.images.append(std::move(box));
        result.append(line);
    }
    flushSegment();
    return result;
}

ImageBox Engine::makeInlineImageBox(const Content::InlineImage &image, qreal availWidth)
{
    ImageBox box;
    box.altText = image.altText;
    if (image.resolvedImageData.isEmpty())
        return box;
    QImage loaded = cachedImage(image.resolvedImageData);
    if (loaded.isNull())
        return box;

    qreal width = image.width > 0 ? image.width : loaded.width();
    qreal height = image.height > 0 ? image.height : loaded.height();
    const qreal maxWidth = qMax<qreal>(1.0, availWidth);
    if (width > maxWidth) {
        height *= maxWidth / width;
        width = maxWidth;
    }
    constexpr qreal kMaxImageHeight = 500.0;
    if (height > kMaxImageHeight) {
        width *= kMaxImageHeight / height;
        height = kMaxImageHeight;
    }
    box.image = loaded;
    box.width = width;
    box.height = height;
    box.title = image.title;
    box.imageId = QString::number(qHashBits(image.resolvedImageData.constData(),
                                             image.resolvedImageData.size()), 16);
    return box;
}

QList<LineBox> Engine::breakTextIntoLines(const QList<Content::InlineNode> &inlines,
                                       const Content::TextStyle &baseStyle,
                                       const Content::ParagraphFormat &format,
                                       qreal availWidth,
                                       bool markdownRanges)
{
    QList<LineBox> lines;
    // For code blocks: still set glyph text fields (m_markdownDecorations)
    // but suppress markdown range creation (bold/italic from syntax highlighting
    // should not produce ** markers in the ActualText).
    auto collected = collectInlines(inlines, baseStyle, numberingContext(),
                                    m_markdownDecorations && markdownRanges);
    if (collected.text.isEmpty())
        return lines;

    // Shape all text
    QList<ShapedRun> shapedRuns = m_textShaper->shape(collected.text, collected.styleRuns);

    // Use the engine's ICU BreakIterator to find line break opportunities.
    icu::UnicodeString ustr(reinterpret_cast<const UChar *>(collected.text.utf16()),
                            collected.text.length());
    icu::BreakIterator *lineBreakIter = lineBreakIterator();

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

    // Hit testing and PDF ActualText both read the source slice. Markdown
    // decorations are optional; the text itself is not.
    for (auto &w : words) {
        if (w.isNewline) continue;
        w.gbox.text = collected.text.mid(w.gbox.textStart, w.gbox.textLength);
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
        kpResult = LineBreaking::findBreaksTiered(kpItems, kpLineWidths, kpConfig, [this] { return m_cancelled && m_cancelled(); });
        if (kpResult.cancelled) { m_breakCancelled = true; return {}; }
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
    box.blockId = para.blockId;
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

    bool cacheHit = false;
    auto collected = collectInlines(para.inlines, baseStyle, numberingContext(), m_markdownDecorations);
    quint64 contentHash = qHash(collected.text);
    contentHash = qHashMulti(contentHash, int(para.format.alignment));
    for (const auto &sr : collected.styleRuns)
        contentHash = qHashMulti(contentHash, sr.start, sr.length, sr.fontFamily, sr.fontWeight,
                                  int(sr.fontItalic), int(sr.fontSize * 100.0));
    if (m_fragmentCache) {
        QList<Layout::LineBox> cachedLines;
        QList<quint32> cachedGlyphs;
        if (m_fragmentCache->loadParagraphBreaks(para.blockId, contentHash,
                baseStyle.fontFamily, int(baseStyle.fontSize * 100), baseStyle.fontWeight,
                baseStyle.italic, effectiveWidth, m_hyphenateJustifiedText,
                &cachedLines, &cachedGlyphs)) {
            box.lines = cachedLines;
            for (const auto &line : box.lines)
                for (const auto &gb : line.glyphs)
                    if (gb.font)
                        for (const auto &g : gb.glyphs)
                            m_fontManager->markGlyphUsed(gb.font, g.glyphId);
            cacheHit = true;
        }
    }
    if (!cacheHit)
        box.lines = breakIntoLines(para.inlines, baseStyle, para.format, effectiveWidth);

    if (!cacheHit && !m_breakCancelled && m_fragmentCache && !box.lines.isEmpty()) {
        m_fragmentCache->storeParagraph(para.blockId, contentHash,
            baseStyle.fontFamily, int(baseStyle.fontSize * 100), baseStyle.fontWeight,
            baseStyle.italic, effectiveWidth, m_hyphenateJustifiedText,
            box.lines, {});
    }

    box.width = effectiveWidth;

    // Compute total height
    qreal h = 0;
    for (const auto &line : box.lines)
        h += line.height;
    box.height = h;
    box.x = para.format.leftMargin;
    box.firstLineIndent = para.format.firstLineIndent;
    box.footnoteIndices = citedNotes(para.inlines);
    box.source = para.source;

    return box;
}

BlockBox Engine::layoutHeading(const Content::Heading &heading, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::HeadingBlock;
    box.blockId = heading.blockId;
    box.headingLevel = heading.level;
    box.width = availWidth;
    box.spaceBefore = heading.format.spaceBefore;
    box.spaceAfter = heading.format.spaceAfter;
    box.footnoteIndices = citedNotes(heading.inlines);

    Content::TextStyle baseStyle;
    if (!heading.inlines.isEmpty()) {
        std::visit([&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Content::TextRun>) {
                baseStyle = n.style;
            }
        }, heading.inlines.first());
    }

    bool cacheHit = false;
    auto collected = collectInlines(heading.inlines, baseStyle, numberingContext(), m_markdownDecorations);
    quint64 contentHash = qHash(collected.text);
    contentHash = qHashMulti(contentHash, int(heading.format.alignment));
    for (const auto &sr : collected.styleRuns)
        contentHash = qHashMulti(contentHash, sr.start, sr.length, sr.fontFamily, sr.fontWeight,
                                  int(sr.fontItalic), int(sr.fontSize * 100.0));
    if (m_fragmentCache) {
        QList<Layout::LineBox> cachedLines;
        QList<quint32> cachedGlyphs;
        if (m_fragmentCache->loadParagraphBreaks(heading.blockId, contentHash,
                baseStyle.fontFamily, int(baseStyle.fontSize * 100), baseStyle.fontWeight,
                baseStyle.italic, availWidth, m_hyphenateJustifiedText,
                &cachedLines, &cachedGlyphs)) {
            box.lines = cachedLines;
            for (const auto &line : box.lines)
                for (const auto &gb : line.glyphs)
                    if (gb.font)
                        for (const auto &g : gb.glyphs)
                            m_fontManager->markGlyphUsed(gb.font, g.glyphId);
            cacheHit = true;
        }
    }
    if (!cacheHit)
        box.lines = breakIntoLines(heading.inlines, baseStyle, heading.format, availWidth);

    if (!cacheHit && !m_breakCancelled && m_fragmentCache && !box.lines.isEmpty()) {
        m_fragmentCache->storeParagraph(heading.blockId, contentHash,
            baseStyle.fontFamily, int(baseStyle.fontSize * 100), baseStyle.fontWeight,
            baseStyle.italic, availWidth, m_hyphenateJustifiedText,
            box.lines, {});
    }

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
    box.blockId = cb.blockId;
    box.width = availWidth - 24.0; // subtract left + right margins (12pt each)
    box.padding = cb.padding;
    box.background = cb.background;
    box.borderColor = QColor(0xe1, 0xe4, 0xe8);
    box.borderWidth = 0.5;
    box.codeLanguage = cb.language;
    box.codeFenced = cb.isFenced;
    box.spaceBefore = 6.0;
    box.spaceAfter = 10.0;

    Content::ParagraphFormat fmt;
    fmt.lineHeightPercent = cb.lineHeightPercent;
    qreal innerWidth = availWidth - cb.padding * 2 - 24.0; // margins

    // Syntax highlighting spans for the whole fence
    QList<CodeSpanCollector::Span> spans;
    const QString codeTheme = m_codeHighlighting ? m_codeTheme : PenelopeSettings::self()->codeHighlightTheme();
    const bool highlightCode = m_codeHighlighting ? *m_codeHighlighting
                                                  : PenelopeSettings::self()->syntaxHighlightingEnabled();
    // An empty theme name follows the code surface. Light-theme tokens are
    // dark, and on a dark code chip they disappear. A named theme stays as chosen.
    const bool preferDarkTheme = codeTheme.isEmpty() && cb.background.isValid()
        && cb.background.lightness() < 128;
    if (!cb.language.isEmpty()) {
        CodeSpanCollector collector(codeTheme, highlightCode, preferDarkTheme);
        spans = collector.highlight(cb.code, cb.language);
        std::sort(spans.begin(), spans.end(),
                  [](const auto &a, const auto &b) { return a.start < b.start; });
    }

    const QStringList sourceLines = cb.code.split(u'\n', Qt::KeepEmptyParts);

    int offset = 0;
    for (int li = 0; li < sourceLines.size(); ++li) {
        const QString lineText = sourceLines.at(li);
        const int lineStart = offset;
        const int lineEnd = lineStart + lineText.size();

        QList<Content::InlineNode> inlines;

        // Build this line's inlines from highlighting spans intersecting it
        if (!spans.isEmpty()) {
            int lastEnd = lineStart;
            bool any = false;
            for (const auto &span : spans) {
                int sStart = span.start;
                int sEnd = span.start + span.length;
                if (sEnd <= lineStart || sStart >= lineEnd)
                    continue;
                int segStart = qMax(sStart, lineStart);
                int segEnd = qMin(sEnd, lineEnd);
                // gap before span
                if (segStart > lastEnd) {
                    Content::TextRun run;
                    run.text = cb.code.mid(lastEnd, segStart - lastEnd);
                    run.style = cb.style;
                    inlines.append(run);
                    any = true;
                }
                Content::TextRun run;
                run.text = cb.code.mid(segStart, segEnd - segStart);
                run.style = cb.style;
                if (span.foreground.isValid()) run.style.foreground = span.foreground;
                if (span.background.isValid()) run.style.background = span.background;
                if (span.bold) run.style.fontWeight = 700;
                if (span.italic) run.style.italic = true;
                inlines.append(run);
                any = true;
                lastEnd = segEnd;
            }
            if (lastEnd < lineEnd) {
                Content::TextRun run;
                run.text = cb.code.mid(lastEnd, lineEnd - lastEnd);
                run.style = cb.style;
                inlines.append(run);
                any = true;
            }
            if (!any) {
                Content::TextRun run;
                run.text = lineText;
                run.style = cb.style;
                inlines.append(run);
            }
        } else {
            Content::TextRun run;
            run.text = lineText;
            run.style = cb.style;
            inlines.append(run);
        }

        // If line empty, still produce an empty inline? breakIntoLines returns empty for empty text; need preserve blank line.
        // Ensure blank lines are preserved.
        // We'll call breakIntoLines; if returns empty, append one empty LineBox to preserve line.
        QList<Layout::LineBox> newLines;
        bool codeCacheHit = false;
        const quint64 lineHash = qHashMulti(0, lineText, codeTheme, preferDarkTheme, highlightCode);
        if (m_fragmentCache) {
            QList<quint32> cachedGlyphs;
            if (m_fragmentCache->loadCodeLine(cb.blockId, li, lineHash, innerWidth, &newLines, &cachedGlyphs)) {
                codeCacheHit = true;
                for (const auto &line : newLines)
                    for (const auto &gb : line.glyphs)
                        if (gb.font)
                            for (const auto &g : gb.glyphs)
                                m_fontManager->markGlyphUsed(gb.font, g.glyphId);
            }
        }
        if (!codeCacheHit) {
            newLines = breakIntoLines(inlines, cb.style, fmt, innerWidth, false);
            if (m_breakCancelled) return BlockBox{};
            if (newLines.isEmpty() && lineText.isEmpty()) {
                LineBox emptyLine;
                emptyLine.height = cb.style.fontSize * 1.2;
                newLines.append(emptyLine);
            }
            if (m_fragmentCache && !m_breakCancelled)
                m_fragmentCache->storeCodeLine(cb.blockId, li, lineHash, innerWidth, newLines, {});
        }
        if (newLines.isEmpty() && lineText.isEmpty()) {
            LineBox emptyLine;
            emptyLine.height = cb.style.fontSize * 1.2;
            box.lines.append(emptyLine);
        } else {
            box.lines.append(newLines);
        }

        offset = lineEnd + 1; // skip newline

        if (m_cancelled && m_cancelled()) {
            m_breakCancelled = true;
            return BlockBox{};
        }
    }


    qreal h = cb.padding * 2;
    for (const auto &line : box.lines)
        h += line.height;
    box.height = h;
    box.x = 12.0; // inset quoted content
    box.source = cb.source;

    return box;
}

BlockBox Engine::layoutHorizontalRule(const Content::HorizontalRule &hr, qreal availWidth)
{
    BlockBox box;
    box.type = BlockBox::HRuleBlock;
    box.blockId = hr.blockId;
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
    tbox.blockId = table.blockId;
    tbox.width = availWidth;
    tbox.borderWidth = table.borderWidth;
    tbox.borderColor = table.borderColor;
    tbox.innerBorderWidth = table.innerBorderWidth;
    tbox.innerBorderColor = table.innerBorderColor;
    tbox.headerBottomBorderWidth = table.headerBottomBorderWidth;
    tbox.headerBottomBorderColor = table.headerBottomBorderColor;
    tbox.cellPadding = table.cellPadding;
    tbox.headerRowCount = table.headerRowCount;
    tbox.source = table.source;

    if (table.rows.isEmpty())
        return tbox;

    int numCols = 0;
    for (const auto &row : table.rows)
        numCols = qMax(numCols, row.cells.size());

    // Content-aware column width distribution
    auto metrics = measureColumnMetrics(table);
    if (m_cancelled && m_cancelled()) return {};

    QList<qreal> colWidths;
    if ((m_optimalTableLayout ? *m_optimalTableLayout :
        PenelopeSettings::self()->tableLayoutAlgorithm() == PenelopeSettings::EnumTableLayoutAlgorithm::Optimal))
        colWidths = distributeColumnsOptimal(table, metrics, availWidth);
    else
        colWidths = distributeColumnsAuto(metrics, availWidth);

    if (m_cancelled && m_cancelled()) return {};

    // Store column positions for grid drawing
    tbox.columnPositions.append(0);
    for (int i = 0; i < numCols; ++i)
        tbox.columnPositions.append(tbox.columnPositions.last() + colWidths[i]);

    qreal y = 0;
    int rowIdx = 0;
    for (const auto &row : table.rows) {
        if (m_cancelled && m_cancelled()) return {};
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
            cbox.footnoteIndices = citedNotes(cell.inlines);

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

    QImage qimg = cachedImage(img.resolvedImageData);
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

    box.image = qimg;
    box.imageWidth = imgW;
    box.imageHeight = imgH;
    box.imageTitle = img.title;
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
        if (m_cancelled && m_cancelled()) return {};
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Content::Paragraph>) {
                auto children = layoutParagraphElements(c, innerWidth);
                for (auto &element : children) {
                    auto &box = std::get<BlockBox>(element);
                    box.x += indent;
                    box.hasBlockQuoteBorder = true;
                    box.blockQuoteLevel = bq.level;
                    box.blockQuoteIndent = indent;
                    elements.append(element);
                }
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
    for (auto &el : elements)
        std::visit([&](auto &e) { e.blockId = bq.blockId; }, el);
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
            if (m_cancelled && m_cancelled()) return {};
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
                            prefix = QString::number(list.startNumber + i)
                                + QString(list.delimiter) + QLatin1Char(' ');
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

                    auto paragraphElements = layoutParagraphElements(para, availWidth);
                    if (paragraphElements.isEmpty()) return;
                    auto box = std::get<BlockBox>(paragraphElements.takeFirst());
                    box.isListItem = true;
                    if (list.tight)
                        box.spaceAfter = 0; // a tight list has no gap between items
                    else if (i > 0)
                        box.spaceBefore = qMax(box.spaceBefore, 6.0); // loose lists separate items

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
                    for (auto &element : paragraphElements) {
                        std::get<BlockBox>(element).isListItem = true;
                        elements.append(element);
                    }
                } else if constexpr (std::is_same_v<T, Content::List>) {
                    // Nested list
                    auto nested = layoutList(c, availWidth, depth + 1);
                    elements.append(nested);
                } else {
                    QList<PageElement> children;
                    const qreal offset = indent + itemBulletWidth;
                    const qreal width = qMax(1.0, availWidth - offset);
                    if constexpr (std::is_same_v<T, Content::CodeBlock>)
                        children.append(layoutCodeBlock(c, width));
                    else if constexpr (std::is_same_v<T, Content::Heading>)
                        children.append(layoutHeading(c, width));
                    else if constexpr (std::is_same_v<T, Content::Table>)
                        children.append(layoutTable(c, width));
                    else if constexpr (std::is_same_v<T, Content::BlockQuote>)
                        children = layoutBlockQuote(c, width);
                    else if constexpr (std::is_same_v<T, Content::HorizontalRule>)
                        children.append(layoutHorizontalRule(c, width));
                    else if constexpr (std::is_same_v<T, Content::FootnoteSection>)
                        children.append(layoutFootnoteSection(c, width));
                    for (auto &elem : children) {
                        std::visit([offset](auto &box) { box.x += offset; }, elem);
                        elements.append(elem);
                    }
                }
            }, child);
        }
    }
    for (auto &el : elements)
        std::visit([&](auto &e) { e.blockId = list.blockId; }, el);
    return elements;
}

// --- Footnote section layout ---

FootnoteSectionBox Engine::layoutFootnoteSection(const Content::FootnoteSection &fs,
                                                  qreal availWidth)
{
    FootnoteSectionBox sbox;
    sbox.blockId = fs.blockId;
    sbox.width = availWidth;
    sbox.showSeparator = fs.showSeparator;
    sbox.separatorLength = fs.separatorLength;
    sbox.separatorWidth = fs.separatorWidth;

    constexpr qreal kIndent = 20.0; // notes hang under their marker
    constexpr qreal kGap = 2.0;

    qreal y = fs.showSeparator ? 12.0 : 0; // space for the separator
    const qreal noteWidth = qMax<qreal>(1.0, availWidth - kIndent);

    for (const auto &fn : fs.footnotes) {
        if (m_cancelled && m_cancelled()) return {};
        FootnoteBox fbox;
        fbox.index = fn.index;
        fbox.references = fn.references;
        fbox.label = m_noteLabelOverrides.contains(fn.index)
            ? m_noteLabelOverrides.value(fn.index)
            : Content::footnoteMarker(m_footnoteStyle, fn.index);
        fbox.numberStyle = fn.numberStyle;
        fbox.y = y;

        qreal noteY = 0;
        auto stack = [&](NoteElement element, qreal height, bool gapAbove) {
            if (gapAbove && noteY > 0)
                noteY += kGap;
            std::visit([&](auto &e) {
                e.x += kIndent;
                e.y = noteY;
            }, element);
            noteY += height;
            fbox.elements.append(std::move(element));
        };

        for (int i = 0; i < fn.blocks.size(); ++i) {
            const Content::BlockNode &block = fn.blocks[i];

            if (i == 0) {
                if (const auto *paragraph = std::get_if<Content::Paragraph>(&block)) {
                    // The marker hangs into the margin on the first line, the
                    // way a single-run note used to render.
                    Content::ParagraphFormat fmt = paragraph->format;
                    fmt.leftMargin = kIndent;
                    fmt.firstLineIndent = -kIndent;
                    Content::TextStyle markerStyle = fn.numberStyle;
                    markerStyle.linkHref = Content::footnoteBacklinkUrl(fn.index);
                    markerStyle.linkKind = Content::LinkKind::Anchor;
                    QList<Content::InlineNode> inlines = paragraph->inlines;
                    inlines.prepend(Content::TextRun{fbox.label + QStringLiteral(". "), markerStyle});

                    BlockBox box;
                    box.type = BlockBox::ParagraphBlock;
                    box.lines = breakIntoLines(inlines, fn.textStyle, fmt,
                                               availWidth - fmt.leftMargin - fmt.rightMargin);
                    box.x = fmt.leftMargin;
                    box.firstLineIndent = fmt.firstLineIndent;
                    box.width = noteWidth;
                    box.source = paragraph->source;
                    box.blockId = paragraph->blockId;
                    qreal height = 0;
                    for (const auto &line : box.lines)
                        height += line.height;
                    box.height = height;
                    stack(std::move(box), height, false);
                    continue;
                }
                // A list, table or fence puts the marker on its own line.
                Content::ParagraphFormat fmt;
                Content::TextStyle markerStyle = fn.numberStyle;
                markerStyle.linkHref = Content::footnoteBacklinkUrl(fn.index);
                markerStyle.linkKind = Content::LinkKind::Anchor;
                QList<Content::InlineNode> labelInlines{
                    Content::TextRun{fbox.label + QStringLiteral(". "), markerStyle}};
                BlockBox labelBox;
                labelBox.type = BlockBox::ParagraphBlock;
                labelBox.lines = breakIntoLines(labelInlines, fn.numberStyle, fmt, noteWidth);
                labelBox.width = noteWidth;
                qreal height = 0;
                for (const auto &line : labelBox.lines)
                    height += line.height;
                labelBox.height = height;
                stack(std::move(labelBox), height, false);
            }

            for (auto element : layoutBlockNode(block, noteWidth)) {
                if (const auto *box = std::get_if<BlockBox>(&element)) {
                    const qreal height = box->height;
                    stack(NoteElement(*box), height, true);
                } else if (const auto *table = std::get_if<TableBox>(&element)) {
                    const qreal height = table->height;
                    stack(NoteElement(*table), height, true);
                }
            }
        }

        fbox.height = noteY;
        sbox.footnotes.append(fbox);
        y += noteY + kGap;
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
    auto newSlice = [&](bool first) -> TableBox {
        TableBox slice;
        slice.x = table.x;
        slice.width = table.width;
        slice.borderWidth = table.borderWidth;
        slice.borderColor = table.borderColor;
        slice.innerBorderWidth = table.innerBorderWidth;
        slice.innerBorderColor = table.innerBorderColor;
        slice.headerBottomBorderWidth = table.headerBottomBorderWidth;
        slice.headerBottomBorderColor = table.headerBottomBorderColor;
        slice.cellPadding = table.cellPadding;
        slice.columnPositions = table.columnPositions;
        const int sliceHeaders = first ? headerRowCount : effectiveHeaderCount;
        slice.headerRowCount = sliceHeaders;
        slice.source = table.source;

        // Copy header rows with re-based y positions
        qreal hy = 0;
        for (int i = 0; i < sliceHeaders && i < table.rows.size(); ++i) {
            TableRowBox row = table.rows[i];
            row.y = hy;
            for (auto &cell : row.cells)
                cell.y = hy;
            slice.rows.append(row);
            hy += row.height;
        }
        return slice;
    };

    TableBox currentSlice = newSlice(true);
    qreal currentHeight = headerHeight;
    qreal currentAvail = availHeight;
    int bodyRowsInSlice = 0;

    for (int i = headerRowCount; i < table.rows.size(); ++i) {
        qreal rowHeight = table.rows[i].height;

        // Need to start a new slice? Only if we have body rows in the current one
        if (currentHeight + rowHeight > currentAvail && bodyRowsInSlice > 0) {
            currentSlice.height = currentHeight;
            slices.append(currentSlice);

            currentSlice = newSlice(false);
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
    if (!currentSlice.rows.isEmpty()) {
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
    splitAfter = qMin(splitAfter, totalLines - minLines);
    if (splitAfter < minLines)
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
        if (m_cancelled && m_cancelled()) return;
        const auto &element = queue[idx];

        // Handle tables separately for page-splitting
        if (auto *tablePtr = std::get_if<TableBox>(&element)) {
            const TableBox &table = *tablePtr;
            qreal remaining = pageHeight - y;

            if (table.height <= remaining) {
                TableBox positioned = table;
                positioned.y = y;
                currentPage.elements.append(positioned);
                y += table.height;
            } else {
                qreal firstRowsHeight = 0;
                const int firstRows = qMin(int(table.rows.size()), table.headerRowCount + 1);
                for (int i = 0; i < firstRows; ++i) firstRowsHeight += table.rows[i].height;
                if (firstRowsHeight > remaining && !currentPage.elements.isEmpty()) {
                    currentPage.contentHeight = y;
                    result.pages.append(currentPage);
                    currentPage = Page{};
                    currentPage.pageNumber = result.pages.size();
                    y = 0;
                    remaining = pageHeight;
                }
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
    auto collected = collectInlines(inlines, baseStyle, numberingContext());
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
    auto collected = collectInlines(inlines, baseStyle, numberingContext());
    if (collected.text.isEmpty())
        return 0;

    QList<ShapedRun> runs = m_textShaper->shape(collected.text, collected.styleRuns);

    // Find line break opportunities with the engine's iterator.
    icu::UnicodeString ustr(reinterpret_cast<const UChar *>(collected.text.utf16()),
                            collected.text.length());
    icu::BreakIterator *lineBreakIter = lineBreakIterator();

    QSet<int> breakPositions;
    if (lineBreakIter) {
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
        if (m_cancelled && m_cancelled()) return {};
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
            if (m_cancelled && m_cancelled()) return {};
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

QList<Layout::PageElement> Layout::Engine::layoutParagraphElements(
    const Content::Paragraph &para, qreal availWidth)
{
    QList<PageElement> result;
    Content::Paragraph text = para;
    text.inlines.clear();
    auto flush = [&]() {
        if (!text.inlines.isEmpty()) result.append(layoutParagraph(text, availWidth));
        text.inlines.clear();
    };
    for (const auto &node : para.inlines) {
        if (const auto *image = std::get_if<Content::InlineImage>(&node)) {
            flush();
            if (!image->resolvedImageData.isEmpty()) {
                auto box = layoutImage(*image, qMax(1.0, availWidth - para.format.leftMargin - para.format.rightMargin));
                box.x = para.format.leftMargin;
                box.source = para.source;
                result.append(box);
            } else {
                Content::TextStyle style;
                text.inlines.append(Content::TextRun{QStringLiteral("[Image: %1]").arg(
                    image->altText.isEmpty() ? image->src : image->altText), style});
            }
        } else text.inlines.append(node);
    }
    flush();
    for (auto &el : result) {
        std::visit([&](auto &e) { e.blockId = para.blockId; }, el);
    }
    return result;
}
