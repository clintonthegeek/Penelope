# TOC Scroll-Sync Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Passively highlight the current heading in the TOC dock as the user scrolls the document.

**Architecture:** DocumentView receives a sorted list of heading positions, finds the topmost visible heading on every scroll event, and emits `currentHeadingChanged(int index)`. TocWidget receives this signal and highlights the corresponding tree item. MainWindow wires the two together and passes heading data from the layout result.

**Tech Stack:** C++/Qt6, QTreeWidget, QGraphicsView

---

### Task 1: Add heading position data and signal to DocumentView

**Files:**
- Modify: `src/canvas/documentview.h:26-32` (add struct after ViewState)
- Modify: `src/canvas/documentview.h:67` (add setter method)
- Modify: `src/canvas/documentview.h:113-118` (add signal)
- Modify: `src/canvas/documentview.h:196-203` (add member variables)

**Step 1: Add HeadingPosition struct to documentview.h**

After the `ViewState` struct (line 32), add:

```cpp
struct HeadingPosition {
    int page = 0;
    qreal yOffset = 0; // page-local, points from top of content area
};
```

**Step 2: Add setter and signal to DocumentView**

In the public section, after `scrollToPosition` (line 67), add:

```cpp
    void setHeadingPositions(const QList<HeadingPosition> &positions);
```

In the Q_SIGNALS section, after `codeBlockLanguageChanged` (line 118), add:

```cpp
    void currentHeadingChanged(int index);
```

In the private section, after `m_wordSelection` (line 203), add:

```cpp
    QList<HeadingPosition> m_headingPositions;
    int m_currentHeading = -1;
```

**Step 3: Implement setHeadingPositions in documentview.cpp**

Add after `scrollToPosition()` (after line 662):

```cpp
void DocumentView::setHeadingPositions(const QList<HeadingPosition> &positions)
{
    m_headingPositions = positions;
    m_currentHeading = -1;
}
```

**Step 4: Build to verify**

Run: `make -C build -j$(($(nproc)-1))`
Expected: Clean build.

**Step 5: Commit**

```
feat: add heading position tracking to DocumentView
```

---

### Task 2: Implement heading lookup in updateCurrentPage()

**Files:**
- Modify: `src/canvas/documentview.cpp:1567-1595` (the `updateCurrentPage` method)

**Step 1: Add heading lookup after existing page-tracking logic**

At the end of `updateCurrentPage()` (before the closing `}` at line 1595), add the heading lookup. The approach: map the viewport top to scene coordinates, then reverse-scan headings to find the last one at or above the viewport top.

```cpp
    // --- Heading scroll-sync ---
    if (m_headingPositions.isEmpty())
        return;

    QPointF viewTop = mapToScene(viewport()->rect().topLeft());

    int bestHeading = -1;
    for (int i = m_headingPositions.size() - 1; i >= 0; --i) {
        const auto &hp = m_headingPositions[i];
        // Convert heading's page-local yOffset to scene coordinates
        // by finding the page item and adding its scene position
        QPointF headingScene;
        bool found = false;
        for (auto *item : m_pdfPageItems) {
            if (item->pageNumber() == hp.page) {
                headingScene = item->pos() + QPointF(0, hp.yOffset);
                found = true;
                break;
            }
        }
        if (!found)
            continue;
        if (headingScene.y() <= viewTop.y()) {
            bestHeading = i;
            break;
        }
    }

    // If no heading is above viewport top, use the first heading
    // (we're above all headings, so highlight the first one)
    if (bestHeading == -1 && !m_headingPositions.isEmpty())
        bestHeading = 0;

    if (bestHeading != m_currentHeading) {
        m_currentHeading = bestHeading;
        Q_EMIT currentHeadingChanged(bestHeading);
    }
```

Note: The nested loop (headings x pages) is fine because both lists are small (dozens of headings, dozens of pages). If performance were a concern we'd precompute scene-y values after layout, but that's premature here.

**Step 2: Build to verify**

Run: `make -C build -j$(($(nproc)-1))`
Expected: Clean build.

**Step 3: Commit**

```
feat: emit currentHeadingChanged on scroll in DocumentView
```

---

### Task 3: Add highlightHeading slot to TocWidget

**Files:**
- Modify: `src/widgets/tocwidget.h:23-34` (add slot and member)
- Modify: `src/widgets/tocwidget.cpp:73-147` (build flat list during buildFromContentModel)
- Modify: `src/widgets/tocwidget.cpp` (add highlightHeading implementation)

**Step 1: Add declaration to tocwidget.h**

After the `clear()` method (line 23), add:

```cpp
    void highlightHeading(int index);
```

In the private section (after line 33), add:

```cpp
    QList<QTreeWidgetItem *> m_flatHeadingItems;
```

**Step 2: Build the flat heading list in buildFromContentModel**

In `tocwidget.cpp`, at the start of `buildFromContentModel()` (after `m_treeWidget->clear()` at line 76), add:

```cpp
    m_flatHeadingItems.clear();
```

After each item is added to the tree (after `parents[level] = item;` at line 141), add:

```cpp
        m_flatHeadingItems.append(item);
```

Also clear the list in `clear()` (after `m_treeWidget->clear()` at line 151):

```cpp
    m_flatHeadingItems.clear();
```

And clear it at the top of `buildFromDocument()` (after `m_treeWidget->clear()` at line 27):

```cpp
    m_flatHeadingItems.clear();
```

**Step 3: Implement highlightHeading**

Add after `onItemClicked()` at the end of `tocwidget.cpp`:

```cpp
void TocWidget::highlightHeading(int index)
{
    if (index < 0 || index >= m_flatHeadingItems.size()) {
        m_treeWidget->setCurrentItem(nullptr);
        return;
    }
    // Block signals to prevent onItemClicked → headingNavigate feedback loop
    m_treeWidget->blockSignals(true);
    m_treeWidget->setCurrentItem(m_flatHeadingItems[index]);
    m_treeWidget->scrollToItem(m_flatHeadingItems[index]);
    m_treeWidget->blockSignals(false);
}
```

**Step 4: Build to verify**

Run: `make -C build -j$(($(nproc)-1))`
Expected: Clean build.

**Step 5: Commit**

```
feat: add highlightHeading slot to TocWidget
```

---

### Task 4: Wire everything together in MainWindow

**Files:**
- Modify: `src/app/mainwindow.cpp:267-272` (add signal connection after headingNavigate connect)
- Modify: `src/app/mainwindow.cpp:1098` (pass heading positions after buildFromContentModel)
- Modify: `src/app/mainwindow.cpp:1215` (same for the other buildFromContentModel call site)

**Step 1: Add signal connection**

After the `headingNavigate` connection (line 272), add:

```cpp
    connect(m_tocWidget, &TocWidget::headingNavigate,
            this, [this](int page, qreal yOffset) {
        ...
    });

    // Scroll-sync: highlight current heading in TOC as user scrolls
    // (connected per-view when tabs change — see connectViewSignals or equivalent)
```

Actually, since `DocumentView` instances change per tab, we need to connect the signal when the active view changes. Let me check how that works.

**Step 1 (revised): Find where per-view signals are connected**

The connection must be made each time a new DocumentView is created or becomes active. The simplest approach: connect right after `buildFromContentModel` at each call site, and pass heading positions at the same time.

At both `buildFromContentModel` call sites (lines 1098 and 1215), add after each:

```cpp
        // Pass heading positions to view for scroll-sync
        {
            QList<HeadingPosition> headingPositions;
            for (const auto &block : contentDoc.blocks) {
                const Content::Heading *heading = std::get_if<Content::Heading>(&block);
                if (!heading || heading->level < 1 || heading->level > 6)
                    continue;
                HeadingPosition hp;
                hp.page = 0;
                hp.yOffset = 0;
                if (heading->source.startLine > 0) {
                    for (const auto &entry : layoutResult.sourceMap) {
                        if (entry.startLine == heading->source.startLine
                            && entry.endLine == heading->source.endLine) {
                            hp.page = entry.pageNumber;
                            hp.yOffset = entry.rect.top();
                            break;
                        }
                    }
                }
                headingPositions.append(hp);
            }
            view->setHeadingPositions(headingPositions);
        }
```

Where `view` is the DocumentView at each call site (`view` at line 1098, `tab->documentView()` at line 1215).

**Step 2: Connect the signal to TocWidget**

The simplest wiring: a one-time connection in the constructor (near line 272) that connects any active view's signal. But since views change per tab, use a lambda that finds the current view.

Better approach: connect directly after setting heading positions at each call site:

```cpp
        connect(view, &DocumentView::currentHeadingChanged,
                m_tocWidget, &TocWidget::highlightHeading);
```

Add this line at both call sites, right after `setHeadingPositions`. Use `Qt::UniqueConnection` to avoid duplicates if the same view is reconnected on re-render:

```cpp
        connect(view, &DocumentView::currentHeadingChanged,
                m_tocWidget, &TocWidget::highlightHeading,
                Qt::UniqueConnection);
```

**Step 3: Build to verify**

Run: `make -C build -j$(($(nproc)-1))`
Expected: Clean build.

**Step 4: Commit**

```
feat: wire TOC scroll-sync in MainWindow
```

---

### Task 5: Build, test, and verify

**Step 1: Full build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: Clean build, 100%.

**Step 2: Run tests**

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: All tests pass.

**Step 3: Manual verification**

Open a document with multiple headings. Scroll through the document and verify:
- The TOC highlights the topmost visible heading as you scroll
- Clicking a TOC item scrolls to it AND highlights it
- Switching between tabs doesn't cause stale highlights
- Documents with no headings don't crash

---

## Execution Notes

- Tasks 1-3 are independent additions to separate files — safe to parallelize.
- Task 4 depends on tasks 1-3 (wires them together).
- Task 5 is manual testing after integration.
