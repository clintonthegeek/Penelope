# TOC Scroll-Sync Design

**Goal:** As the user scrolls the document, passively highlight the current heading in the TOC dock widget.

## Data Flow

Heading position data already exists in `LayoutResult::sourceMap`. `TocWidget::buildFromContentModel()` extracts each heading's page number and y-offset. The same data is passed to `DocumentView`.

## New API

**DocumentView:**

- `struct HeadingPosition { int page; qreal yOffset; }` — page-local y in points from top of content area.
- `setHeadingPositions(QList<HeadingPosition>)` — called after layout, stores a sorted list.
- `currentHeadingChanged(int index)` signal — emitted from `updateCurrentPage()` when the active heading changes. Index into the positions list; -1 if no heading is above the viewport.

**TocWidget:**

- `highlightHeading(int index)` slot — selects the tree item at that index. Uses `blockSignals(true)` around `setCurrentItem()` to prevent re-emitting `headingNavigate`.
- Flat item list built once during `buildFromContentModel()` to map index to tree item.

**MainWindow wiring:**

- After `buildFromContentModel()`, call `view->setHeadingPositions(...)` with the same heading positions.
- Connect `DocumentView::currentHeadingChanged` to `TocWidget::highlightHeading`.

## Heading Lookup Algorithm

In `updateCurrentPage()`, after the existing page-tracking logic:

1. Map viewport top edge to scene coordinates.
2. Reverse-scan the sorted heading list to find the last heading whose scene-y is at or above the viewport top.
3. Only emit `currentHeadingChanged(index)` when the index changes.

Linear scan is sufficient — heading counts are dozens, not thousands.

## Behaviour

- Topmost visible heading is highlighted.
- Highlight updates on all scroll events including programmatic navigation (TOC clicks).
- No feedback loop: `highlightHeading` blocks signals while selecting.
