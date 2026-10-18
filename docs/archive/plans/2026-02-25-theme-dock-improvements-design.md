# Theme Dock Improvements Design

## Problem

The Theme dock is a demo-quality implementation:
1. Double-clicking type sets / colour palettes does nothing — users expect it to open the editing dock
2. Tile grids can be squished to nothing with no minimum size enforcement
3. No scrollbars when the dock is too small to show all content

## Requirements

- Double-click a type set → raise the Type dock with that set selected
- Double-click a colour palette → raise the Colour dock with that palette selected
- Single-click behavior unchanged (applies immediately)
- Fixed-size tiles with minimum dock width to prevent squishing
- Vertical scrollbar when content exceeds dock height

## Design

### 1. Double-click signals (ResourcePickerWidget layer)

**ResourcePickerCellBase** — add `doubleClicked(const QString &id)` signal, override `mouseDoubleClickEvent` to emit it.

**ResourcePickerWidget** — add `resourceDoubleClicked(const QString &id)` signal. In `addCell()`, connect each cell's `doubleClicked` to the widget's `resourceDoubleClicked`.

### 2. ThemePickerDock signals

Add two new signals:
- `typeSetEditRequested(const QString &id)`
- `paletteEditRequested(const QString &id)`

Connect:
- `TypeSetPickerWidget::resourceDoubleClicked` → emit `typeSetEditRequested`
- `PalettePickerWidget::resourceDoubleClicked` → emit `paletteEditRequested`

### 3. MainWindow wiring

Connect new ThemePickerDock signals:
- `typeSetEditRequested` → set Type dock's current type set + `m_rightSidebar->showPanel(m_typeTabId)`
- `paletteEditRequested` → set Colour dock's current palette + `m_rightSidebar->showPanel(m_colorTabId)`

### 4. Layout — minimum sizes

**ResourcePickerWidget** — after `rebuildGrid()`, compute and set minimum width on the grid container:
`gridColumns() × cellWidth + (gridColumns() - 1) × spacing`

Each concrete picker has deterministic cell sizes (120×62 for type sets, 75×52 for palettes, 120×50 for templates), so minimum widths are fixed.

### 5. Layout — scroll area

**ThemePickerDock** — wrap content in a `QScrollArea` with `widgetResizable(true)` and `horizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)`. This provides vertical scrolling when the dock height is insufficient while the minimum width prevents horizontal squishing.

## Files Changed

- `src/widgets/resourcepickerwidget.h/cpp` — double-click signal, minimum width
- `src/widgets/themepickerdock.h/cpp` — new signals, scroll area wrapper
- `src/app/mainwindow.cpp` — wire new signals to raise docks
