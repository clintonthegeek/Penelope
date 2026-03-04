# Theme Dock Improvements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the Theme dock functional by adding double-click to open editing docks, enforcing minimum tile sizes, and adding scroll support.

**Architecture:** Add double-click signals through the ResourcePickerCellBase → ResourcePickerWidget → ThemePickerDock signal chain. MainWindow wires the final signals to raise the appropriate sidebar panel. Wrap ThemePickerDock content in a QScrollArea with minimum width enforcement on picker grids.

**Tech Stack:** Qt6/C++, existing ResourcePickerWidget base class, Sidebar panel system

---

### Task 1: Add double-click signal to ResourcePickerCellBase

**Files:**
- Modify: `src/widgets/resourcepickerwidget.h:40-63`

**Step 1: Add doubleClicked signal and mouseDoubleClickEvent override**

In `ResourcePickerCellBase`, add the `doubleClicked` signal next to the existing `clicked` signal (line 41), and add a `mouseDoubleClickEvent` override after the existing `mousePressEvent` (line 58):

```cpp
// line 40-41: add doubleClicked signal
Q_SIGNALS:
    void clicked(const QString &id);
    void doubleClicked(const QString &id);
```

```cpp
// line 58-63: add mouseDoubleClickEvent after mousePressEvent
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            Q_EMIT clicked(m_cellId);
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            Q_EMIT doubleClicked(m_cellId);
        QWidget::mouseDoubleClickEvent(event);
    }
```

**Step 2: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Commit**

```
feat: add doubleClicked signal to ResourcePickerCellBase
```

---

### Task 2: Add resourceDoubleClicked signal to ResourcePickerWidget

**Files:**
- Modify: `src/widgets/resourcepickerwidget.h:81-82`
- Modify: `src/widgets/resourcepickerwidget.cpp:59-70`

**Step 1: Add resourceDoubleClicked signal**

In `ResourcePickerWidget`, add the signal next to the existing `resourceSelected` signal (line 81-82):

```cpp
Q_SIGNALS:
    void resourceSelected(const QString &id);
    void resourceDoubleClicked(const QString &id);
```

**Step 2: Connect cell doubleClicked in addCell()**

In `addCell()` (resourcepickerwidget.cpp:59-70), add a connection for the double-click signal after the existing clicked connection:

```cpp
void ResourcePickerWidget::addCell(ResourcePickerCellBase *cell)
{
    connect(cell, &ResourcePickerCellBase::clicked, this, [this](const QString &id) {
        setCurrentId(id);
        Q_EMIT resourceSelected(id);
    });
    connect(cell, &ResourcePickerCellBase::doubleClicked, this, [this](const QString &id) {
        setCurrentId(id);
        Q_EMIT resourceSelected(id);
        Q_EMIT resourceDoubleClicked(id);
    });
    m_gridLayout->addWidget(cell, m_row, m_col);
    if (++m_col >= gridColumns()) {
        m_col = 0;
        ++m_row;
    }
}
```

Note: The double-click handler also emits `resourceSelected` so the item is applied (single-click behavior preserved) in addition to the double-click signal.

**Step 3: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds

**Step 4: Commit**

```
feat: add resourceDoubleClicked signal to ResourcePickerWidget
```

---

### Task 3: Add edit-requested signals to ThemePickerDock

**Files:**
- Modify: `src/widgets/themepickerdock.h:43-45`
- Modify: `src/widgets/themepickerdock.cpp:42-50`

**Step 1: Add new signals to header**

In `themepickerdock.h`, add the edit-requested signals (after line 45):

```cpp
Q_SIGNALS:
    void compositionApplied();
    void templateApplied(const PageLayout &layout);
    void typeSetEditRequested(const QString &id);
    void paletteEditRequested(const QString &id);
```

**Step 2: Connect picker double-click signals in buildUI()**

In `themepickerdock.cpp`, after each existing `resourceSelected` connection, add a `resourceDoubleClicked` connection:

After line 43 (type set picker connection):
```cpp
    connect(m_typeSetPicker, &TypeSetPickerWidget::resourceSelected,
            this, &ThemePickerDock::onTypeSetSelected);
    connect(m_typeSetPicker, &TypeSetPickerWidget::resourceDoubleClicked,
            this, &ThemePickerDock::typeSetEditRequested);
```

After line 49 (palette picker connection):
```cpp
    connect(m_palettePicker, &PalettePickerWidget::resourceSelected,
            this, &ThemePickerDock::onPaletteSelected);
    connect(m_palettePicker, &PalettePickerWidget::resourceDoubleClicked,
            this, &ThemePickerDock::paletteEditRequested);
```

**Step 3: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds

**Step 4: Commit**

```
feat: add typeSetEditRequested/paletteEditRequested signals to ThemePickerDock
```

---

### Task 4: Wire edit-requested signals in MainWindow

**Files:**
- Modify: `src/app/mainwindow.cpp:363-413`

**Step 1: Add connections for the new signals**

In `setupSidebars()`, after the existing ThemePickerDock connections (after line 377), add:

```cpp
    // Double-click in Theme grid -> raise editing dock
    connect(m_themePickerDock, &ThemePickerDock::typeSetEditRequested,
            this, [this](const QString &id) {
        m_typeDockWidget->setCurrentTypeSetId(id);
        m_rightSidebar->showPanel(m_typeTabId);
    });
    connect(m_themePickerDock, &ThemePickerDock::paletteEditRequested,
            this, [this](const QString &id) {
        m_colorDockWidget->setCurrentPaletteId(id);
        m_rightSidebar->showPanel(m_colorTabId);
    });
```

**Step 2: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Manual test**

Run: `./build/bin/Penelope`
- Open a document
- In Theme dock, single-click a type set → it applies (existing behavior, should still work)
- Double-click a type set → Type dock should raise with that set selected
- Double-click a colour palette → Colour dock should raise with that palette selected

**Step 4: Commit**

```
feat: wire Theme dock double-click to raise Type/Colour editing docks
```

---

### Task 5: Enforce minimum width on picker grids

**Files:**
- Modify: `src/widgets/resourcepickerwidget.h:84-104`
- Modify: `src/widgets/resourcepickerwidget.cpp:9-29`

**Step 1: Add virtual cellSize() method and minimum width enforcement**

In `resourcepickerwidget.h`, add a pure virtual method to the protected section:

```cpp
    /// Override to return the fixed cell size for minimum-width calculation.
    virtual QSize cellSize() const = 0;
```

In `resourcepickerwidget.cpp`, update the constructor to store the grid container as a member (so we can set its minimum width later). Currently the grid container is a local variable (line 22). We need it accessible. Add a member `QWidget *m_gridContainer` to the header.

In `resourcepickerwidget.h`, add to private section:
```cpp
    QWidget *m_gridContainer = nullptr;
```

In `resourcepickerwidget.cpp` constructor, change line 22 from:
```cpp
    auto *gridContainer = new QWidget(this);
```
to:
```cpp
    m_gridContainer = new QWidget(this);
```
And update line 23 and 26 to use `m_gridContainer` instead of `gridContainer`.

**Step 2: Set minimum width after rebuildGrid()**

In `rebuildGrid()` (resourcepickerwidget.cpp:47-57), after `populateGrid()`, set the minimum width:

```cpp
void ResourcePickerWidget::rebuildGrid()
{
    while (QLayoutItem *item = m_gridLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    m_row = 0;
    m_col = 0;
    populateGrid();

    // Enforce minimum width: columns × cell width + (columns-1) × spacing
    const int cols = gridColumns();
    const int spacing = m_gridLayout->spacing();
    const int minW = cols * cellSize().width() + (cols - 1) * spacing;
    m_gridContainer->setMinimumWidth(minW);
}
```

**Step 3: Implement cellSize() in each concrete picker**

In `src/widgets/typesetpickerwidget.h` / `.cpp`:
```cpp
QSize cellSize() const override { return {120, 62}; }
```

In `src/widgets/palettepickerwidget.h` / `.cpp`:
```cpp
QSize cellSize() const override { return {75, 52}; }
```

In `src/widgets/pagetemplatepickerwidget.h` / `.cpp`:
```cpp
QSize cellSize() const override { return {120, 50}; }
```

These go in the protected section of each class. The values match the existing `setFixedSize()` calls in each cell constructor.

**Step 4: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds

**Step 5: Commit**

```
feat: enforce minimum width on resource picker grids
```

---

### Task 6: Wrap ThemePickerDock content in QScrollArea

**Files:**
- Modify: `src/widgets/themepickerdock.cpp:14,32-67`

**Step 1: Add QScrollArea include and wrap content**

Add include at the top of `themepickerdock.cpp`:
```cpp
#include <QScrollArea>
```

Rewrite `buildUI()` to wrap content in a scroll area:

```cpp
void ThemePickerDock::buildUI()
{
    // Outer layout holds just the scroll area
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    outerLayout->addWidget(scrollArea);

    // Inner content widget
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // --- Type Set Picker ---
    m_typeSetPicker = new TypeSetPickerWidget(m_typeSetManager, content);
    layout->addWidget(m_typeSetPicker);

    connect(m_typeSetPicker, &TypeSetPickerWidget::resourceSelected,
            this, &ThemePickerDock::onTypeSetSelected);
    connect(m_typeSetPicker, &TypeSetPickerWidget::resourceDoubleClicked,
            this, &ThemePickerDock::typeSetEditRequested);

    // --- Color Palette Picker ---
    m_palettePicker = new PalettePickerWidget(m_paletteManager, content);
    layout->addWidget(m_palettePicker);

    connect(m_palettePicker, &PalettePickerWidget::resourceSelected,
            this, &ThemePickerDock::onPaletteSelected);
    connect(m_palettePicker, &PalettePickerWidget::resourceDoubleClicked,
            this, &ThemePickerDock::paletteEditRequested);

    // --- Page Template Picker (initially hidden — visible in print mode) ---
    m_templateSection = new QWidget(content);
    auto *templateLayout = new QVBoxLayout(m_templateSection);
    templateLayout->setContentsMargins(0, 0, 0, 0);

    m_templatePicker = new PageTemplatePickerWidget(m_pageTemplateManager, m_templateSection);
    templateLayout->addWidget(m_templatePicker);

    connect(m_templatePicker, &PageTemplatePickerWidget::resourceSelected,
            this, &ThemePickerDock::onTemplateSelected);

    m_templateSection->setVisible(false);
    layout->addWidget(m_templateSection);

    layout->addStretch();

    scrollArea->setWidget(content);
}
```

Note: This task combines the scroll area wrapping with the double-click connections from Task 3. Since Task 3 modifies the same `buildUI()` method, this task rewrites the whole method incorporating both changes. If executing tasks sequentially, Task 3 should add the connections first, then Task 6 rewrites the method to add the scroll area (keeping the connections).

**Step 2: Build to verify compilation**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Manual test**

Run: `./build/bin/Penelope`
- Open a document
- Resize the Theme dock very narrow → tiles should NOT squish, scroll area width prevents it
- Resize the Theme dock very short → vertical scrollbar should appear
- Double-click still works to open editing docks
- Single-click still applies type sets / palettes

**Step 4: Commit**

```
feat: wrap Theme dock content in scroll area for proper layout
```
