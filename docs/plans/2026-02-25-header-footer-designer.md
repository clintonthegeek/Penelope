# Header & Footer Designer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the awkward placeholder-tag text fields in the Page dock with a modern drag-and-drop tile designer in a dedicated modal dialog, and audit all header/footer drawing routines.

**Architecture:** New `HeaderFooterDialog` (QDialog) launched from a button in `PageLayoutWidget`. The dialog contains a tile palette (draggable snippet/placeholder tiles), header and footer designer sections each with left/center/right drop-target fields, and master page options via checkboxes. A `DropTargetLineEdit` QLineEdit subclass handles both drag-drop and keyboard input. The existing `PageLayout` data model is unchanged — the dialog simply provides a better UI for populating the same fields.

**Tech Stack:** Qt6 (QDialog, QDrag, QMimeData, QLineEdit), KDE Frameworks 6

---

### Task 1: Create DropTargetLineEdit widget

A QLineEdit subclass that accepts drag-and-drop of text/plain MIME data and inserts it at the cursor position.

**Files:**
- Create: `src/widgets/droptargetlineedit.h`
- Create: `src/widgets/droptargetlineedit.cpp`
- Modify: `src/CMakeLists.txt:111-112` — add new source files

**Step 1: Create the header file**

Create `src/widgets/droptargetlineedit.h`:

```cpp
#ifndef PENELOPE_DROPTARGETLINEEDIT_H
#define PENELOPE_DROPTARGETLINEEDIT_H

#include <QLineEdit>

class QDragEnterEvent;
class QDropEvent;

class DropTargetLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit DropTargetLineEdit(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

#endif // PENELOPE_DROPTARGETLINEEDIT_H
```

**Step 2: Create the implementation file**

Create `src/widgets/droptargetlineedit.cpp`:

```cpp
#include "droptargetlineedit.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

DropTargetLineEdit::DropTargetLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setAcceptDrops(true);
}

void DropTargetLineEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasText())
        event->acceptProposedAction();
    else
        QLineEdit::dragEnterEvent(event);
}

void DropTargetLineEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasText())
        event->acceptProposedAction();
    else
        QLineEdit::dragMoveEvent(event);
}

void DropTargetLineEdit::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasText()) {
        // Insert the dropped text at the current cursor position
        int pos = cursorPositionAt(event->position().toPoint());
        QString current = text();
        current.insert(pos, event->mimeData()->text());
        setText(current);
        setCursorPosition(pos + event->mimeData()->text().length());
        event->acceptProposedAction();
    } else {
        QLineEdit::dropEvent(event);
    }
}
```

**Step 3: Register in CMakeLists.txt**

In `src/CMakeLists.txt`, add after the `pagelayoutwidget` entries (around line 112):

```cmake
    widgets/droptargetlineedit.cpp
    widgets/droptargetlineedit.h
```

**Step 4: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds with no errors.

**Step 5: Commit**

```bash
git add src/widgets/droptargetlineedit.h src/widgets/droptargetlineedit.cpp src/CMakeLists.txt
git commit -m "feat: add DropTargetLineEdit widget for drag-and-drop text insertion"
```

---

### Task 2: Create HeaderFooterDialog — basic structure

The modal dialog with tile palette, header/footer sections, and OK/Cancel buttons. Uses `DropTargetLineEdit` for the six fields.

**Files:**
- Create: `src/widgets/headerfooterdialog.h`
- Create: `src/widgets/headerfooterdialog.cpp`
- Modify: `src/CMakeLists.txt` — add new source files

**Step 1: Create the header file**

Create `src/widgets/headerfooterdialog.h`:

```cpp
#ifndef PENELOPE_HEADERFOOTERDIALOG_H
#define PENELOPE_HEADERFOOTERDIALOG_H

#include <QDialog>

#include "pagelayout.h"

class QCheckBox;
class DropTargetLineEdit;

class HeaderFooterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HeaderFooterDialog(const PageLayout &layout, QWidget *parent = nullptr);

    PageLayout result() const;

private:
    QWidget *createTilePalette();
    QWidget *createFieldRow(DropTargetLineEdit *&leftEdit,
                            DropTargetLineEdit *&centerEdit,
                            DropTargetLineEdit *&rightEdit);
    void updateMasterPageVisibility();
    void loadFromLayout(const PageLayout &layout);

    // Default header/footer fields
    DropTargetLineEdit *m_headerLeftEdit = nullptr;
    DropTargetLineEdit *m_headerCenterEdit = nullptr;
    DropTargetLineEdit *m_headerRightEdit = nullptr;
    DropTargetLineEdit *m_footerLeftEdit = nullptr;
    DropTargetLineEdit *m_footerCenterEdit = nullptr;
    DropTargetLineEdit *m_footerRightEdit = nullptr;

    // First page overrides
    QCheckBox *m_differentFirstPage = nullptr;
    QWidget *m_firstPageSection = nullptr;
    DropTargetLineEdit *m_firstHeaderLeftEdit = nullptr;
    DropTargetLineEdit *m_firstHeaderCenterEdit = nullptr;
    DropTargetLineEdit *m_firstHeaderRightEdit = nullptr;
    DropTargetLineEdit *m_firstFooterLeftEdit = nullptr;
    DropTargetLineEdit *m_firstFooterCenterEdit = nullptr;
    DropTargetLineEdit *m_firstFooterRightEdit = nullptr;

    // Odd/even page overrides
    QCheckBox *m_differentOddEven = nullptr;
    QWidget *m_oddEvenSection = nullptr;
    QWidget *m_defaultSection = nullptr;
    DropTargetLineEdit *m_leftHeaderLeftEdit = nullptr;
    DropTargetLineEdit *m_leftHeaderCenterEdit = nullptr;
    DropTargetLineEdit *m_leftHeaderRightEdit = nullptr;
    DropTargetLineEdit *m_leftFooterLeftEdit = nullptr;
    DropTargetLineEdit *m_leftFooterCenterEdit = nullptr;
    DropTargetLineEdit *m_leftFooterRightEdit = nullptr;
    DropTargetLineEdit *m_rightHeaderLeftEdit = nullptr;
    DropTargetLineEdit *m_rightHeaderCenterEdit = nullptr;
    DropTargetLineEdit *m_rightHeaderRightEdit = nullptr;
    DropTargetLineEdit *m_rightFooterLeftEdit = nullptr;
    DropTargetLineEdit *m_rightFooterCenterEdit = nullptr;
    DropTargetLineEdit *m_rightFooterRightEdit = nullptr;

    // Copy of incoming layout for fields we don't edit
    PageLayout m_baseLayout;
};

#endif // PENELOPE_HEADERFOOTERDIALOG_H
```

**Step 2: Create the implementation file**

Create `src/widgets/headerfooterdialog.cpp`:

```cpp
#include "headerfooterdialog.h"
#include "droptargetlineedit.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDrag>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>

// --- DragTileLabel: a small draggable label for the tile palette ---

class DragTileLabel : public QLabel
{
public:
    DragTileLabel(const QString &displayText, const QString &insertText, QWidget *parent = nullptr)
        : QLabel(displayText, parent)
        , m_insertText(insertText)
    {
        setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
        setMargin(6);
        setCursor(Qt::OpenHandCursor);
        setToolTip(insertText);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_dragStartPos = event->pos();
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        if ((event->pos() - m_dragStartPos).manhattanLength()
            < QApplication::startDragDistance())
            return;

        auto *drag = new QDrag(this);
        auto *mimeData = new QMimeData;
        mimeData->setText(m_insertText);
        drag->setMimeData(mimeData);
        drag->exec(Qt::CopyAction);
    }

private:
    QString m_insertText;
    QPoint m_dragStartPos;
};

// --- HeaderFooterDialog ---

HeaderFooterDialog::HeaderFooterDialog(const PageLayout &layout, QWidget *parent)
    : QDialog(parent)
    , m_baseLayout(layout)
{
    setWindowTitle(tr("Edit Headers & Footers"));
    setMinimumWidth(600);

    auto *mainLayout = new QVBoxLayout(this);

    // Tile palette
    mainLayout->addWidget(createTilePalette());

    // Default header/footer section
    m_defaultSection = new QWidget;
    auto *defaultLayout = new QVBoxLayout(m_defaultSection);
    defaultLayout->setContentsMargins(0, 0, 0, 0);

    auto *headerGroup = new QGroupBox(tr("Header"));
    auto *headerLayout = new QVBoxLayout(headerGroup);
    headerLayout->addWidget(createFieldRow(m_headerLeftEdit, m_headerCenterEdit, m_headerRightEdit));
    defaultLayout->addWidget(headerGroup);

    auto *footerGroup = new QGroupBox(tr("Footer"));
    auto *footerLayout = new QVBoxLayout(footerGroup);
    footerLayout->addWidget(createFieldRow(m_footerLeftEdit, m_footerCenterEdit, m_footerRightEdit));
    defaultLayout->addWidget(footerGroup);

    mainLayout->addWidget(m_defaultSection);

    // Different first page
    m_differentFirstPage = new QCheckBox(tr("Different first page"));
    mainLayout->addWidget(m_differentFirstPage);

    m_firstPageSection = new QWidget;
    auto *firstLayout = new QVBoxLayout(m_firstPageSection);
    firstLayout->setContentsMargins(0, 0, 0, 0);

    auto *firstHeaderGroup = new QGroupBox(tr("First Page — Header"));
    auto *firstHeaderLayout = new QVBoxLayout(firstHeaderGroup);
    firstHeaderLayout->addWidget(createFieldRow(m_firstHeaderLeftEdit, m_firstHeaderCenterEdit, m_firstHeaderRightEdit));
    firstLayout->addWidget(firstHeaderGroup);

    auto *firstFooterGroup = new QGroupBox(tr("First Page — Footer"));
    auto *firstFooterLayout = new QVBoxLayout(firstFooterGroup);
    firstFooterLayout->addWidget(createFieldRow(m_firstFooterLeftEdit, m_firstFooterCenterEdit, m_firstFooterRightEdit));
    firstLayout->addWidget(firstFooterGroup);

    mainLayout->addWidget(m_firstPageSection);

    // Different odd/even pages
    m_differentOddEven = new QCheckBox(tr("Different odd and even pages"));
    mainLayout->addWidget(m_differentOddEven);

    m_oddEvenSection = new QWidget;
    auto *oddEvenLayout = new QVBoxLayout(m_oddEvenSection);
    oddEvenLayout->setContentsMargins(0, 0, 0, 0);

    auto *leftHeaderGroup = new QGroupBox(tr("Even Pages — Header"));
    auto *leftHeaderLayout = new QVBoxLayout(leftHeaderGroup);
    leftHeaderLayout->addWidget(createFieldRow(m_leftHeaderLeftEdit, m_leftHeaderCenterEdit, m_leftHeaderRightEdit));
    oddEvenLayout->addWidget(leftHeaderGroup);

    auto *leftFooterGroup = new QGroupBox(tr("Even Pages — Footer"));
    auto *leftFooterLayout = new QVBoxLayout(leftFooterGroup);
    leftFooterLayout->addWidget(createFieldRow(m_leftFooterLeftEdit, m_leftFooterCenterEdit, m_leftFooterRightEdit));
    oddEvenLayout->addWidget(leftFooterGroup);

    auto *rightHeaderGroup = new QGroupBox(tr("Odd Pages — Header"));
    auto *rightHeaderLayout = new QVBoxLayout(rightHeaderGroup);
    rightHeaderLayout->addWidget(createFieldRow(m_rightHeaderLeftEdit, m_rightHeaderCenterEdit, m_rightHeaderRightEdit));
    oddEvenLayout->addWidget(rightHeaderGroup);

    auto *rightFooterGroup = new QGroupBox(tr("Odd Pages — Footer"));
    auto *rightFooterLayout = new QVBoxLayout(rightFooterGroup);
    rightFooterLayout->addWidget(createFieldRow(m_rightFooterLeftEdit, m_rightFooterCenterEdit, m_rightFooterRightEdit));
    oddEvenLayout->addWidget(rightFooterGroup);

    mainLayout->addWidget(m_oddEvenSection);

    // Button box
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Visibility connections
    connect(m_differentFirstPage, &QCheckBox::toggled, this, &HeaderFooterDialog::updateMasterPageVisibility);
    connect(m_differentOddEven, &QCheckBox::toggled, this, &HeaderFooterDialog::updateMasterPageVisibility);

    // Load initial values
    loadFromLayout(layout);
    updateMasterPageVisibility();
}

QWidget *HeaderFooterDialog::createTilePalette()
{
    auto *group = new QGroupBox(tr("Drag tiles into fields below"));
    auto *layout = new QHBoxLayout(group);
    layout->setSpacing(8);

    struct TileDef { QString label; QString insert; };
    const TileDef tiles[] = {
        {tr("Page X of Y"),  QStringLiteral("{page} / {pages}")},
        {tr("Page Number"),  QStringLiteral("{page}")},
        {tr("Title"),        QStringLiteral("{title}")},
        {tr("Filename"),     QStringLiteral("{filename}")},
        {tr("Date"),         QStringLiteral("{date}")},
        {tr("Full Date"),    QStringLiteral("{date:d MMMM yyyy}")},
    };

    for (const auto &tile : tiles) {
        layout->addWidget(new DragTileLabel(tile.label, tile.insert, group));
    }
    layout->addStretch();

    return group;
}

QWidget *HeaderFooterDialog::createFieldRow(DropTargetLineEdit *&leftEdit,
                                             DropTargetLineEdit *&centerEdit,
                                             DropTargetLineEdit *&rightEdit)
{
    auto *widget = new QWidget;
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);

    row->addWidget(new QLabel(tr("Left:")));
    leftEdit = new DropTargetLineEdit;
    leftEdit->setPlaceholderText(tr("Left"));
    row->addWidget(leftEdit, 1);

    row->addWidget(new QLabel(tr("Center:")));
    centerEdit = new DropTargetLineEdit;
    centerEdit->setPlaceholderText(tr("Center"));
    row->addWidget(centerEdit, 1);

    row->addWidget(new QLabel(tr("Right:")));
    rightEdit = new DropTargetLineEdit;
    rightEdit->setPlaceholderText(tr("Right"));
    row->addWidget(rightEdit, 1);

    return widget;
}

void HeaderFooterDialog::updateMasterPageVisibility()
{
    bool firstPage = m_differentFirstPage->isChecked();
    bool oddEven = m_differentOddEven->isChecked();

    m_firstPageSection->setVisible(firstPage);

    // When odd/even is enabled, default section becomes "Odd Pages"
    // and the odd/even section shows "Even Pages" (left) fields
    m_oddEvenSection->setVisible(oddEven);

    // When odd/even is enabled, relabel default section
    // (handled by the group box titles being fixed — default is always shown
    //  as "Header"/"Footer" and becomes the odd-page template)
}

void HeaderFooterDialog::loadFromLayout(const PageLayout &layout)
{
    // Default fields
    m_headerLeftEdit->setText(layout.headerLeft);
    m_headerCenterEdit->setText(layout.headerCenter);
    m_headerRightEdit->setText(layout.headerRight);
    m_footerLeftEdit->setText(layout.footerLeft);
    m_footerCenterEdit->setText(layout.footerCenter);
    m_footerRightEdit->setText(layout.footerRight);

    // Check if first page master exists
    bool hasFirst = layout.masterPages.contains(QStringLiteral("first"));
    m_differentFirstPage->setChecked(hasFirst);
    if (hasFirst) {
        const MasterPage &mp = layout.masterPages[QStringLiteral("first")];
        if (mp.hasHeaderLeft)   m_firstHeaderLeftEdit->setText(mp.headerLeft);
        if (mp.hasHeaderCenter) m_firstHeaderCenterEdit->setText(mp.headerCenter);
        if (mp.hasHeaderRight)  m_firstHeaderRightEdit->setText(mp.headerRight);
        if (mp.hasFooterLeft)   m_firstFooterLeftEdit->setText(mp.footerLeft);
        if (mp.hasFooterCenter) m_firstFooterCenterEdit->setText(mp.footerCenter);
        if (mp.hasFooterRight)  m_firstFooterRightEdit->setText(mp.footerRight);
    }

    // Check if left/right masters exist
    bool hasLeft = layout.masterPages.contains(QStringLiteral("left"));
    bool hasRight = layout.masterPages.contains(QStringLiteral("right"));
    m_differentOddEven->setChecked(hasLeft || hasRight);
    if (hasLeft) {
        const MasterPage &mp = layout.masterPages[QStringLiteral("left")];
        if (mp.hasHeaderLeft)   m_leftHeaderLeftEdit->setText(mp.headerLeft);
        if (mp.hasHeaderCenter) m_leftHeaderCenterEdit->setText(mp.headerCenter);
        if (mp.hasHeaderRight)  m_leftHeaderRightEdit->setText(mp.headerRight);
        if (mp.hasFooterLeft)   m_leftFooterLeftEdit->setText(mp.footerLeft);
        if (mp.hasFooterCenter) m_leftFooterCenterEdit->setText(mp.footerCenter);
        if (mp.hasFooterRight)  m_leftFooterRightEdit->setText(mp.footerRight);
    }
    if (hasRight) {
        const MasterPage &mp = layout.masterPages[QStringLiteral("right")];
        if (mp.hasHeaderLeft)   m_rightHeaderLeftEdit->setText(mp.headerLeft);
        if (mp.hasHeaderCenter) m_rightHeaderCenterEdit->setText(mp.headerCenter);
        if (mp.hasHeaderRight)  m_rightHeaderRightEdit->setText(mp.headerRight);
        if (mp.hasFooterLeft)   m_rightFooterLeftEdit->setText(mp.footerLeft);
        if (mp.hasFooterCenter) m_rightFooterCenterEdit->setText(mp.footerCenter);
        if (mp.hasFooterRight)  m_rightFooterRightEdit->setText(mp.footerRight);
    }
}

PageLayout HeaderFooterDialog::result() const
{
    PageLayout pl = m_baseLayout;

    // Write back default fields
    pl.headerLeft = m_headerLeftEdit->text();
    pl.headerCenter = m_headerCenterEdit->text();
    pl.headerRight = m_headerRightEdit->text();
    pl.footerLeft = m_footerLeftEdit->text();
    pl.footerCenter = m_footerCenterEdit->text();
    pl.footerRight = m_footerRightEdit->text();

    // Clear master pages we manage (preserve margin overrides from other sources)
    pl.masterPages.remove(QStringLiteral("first"));
    pl.masterPages.remove(QStringLiteral("left"));
    pl.masterPages.remove(QStringLiteral("right"));

    // First page
    if (m_differentFirstPage->isChecked()) {
        MasterPage mp;
        mp.name = QStringLiteral("first");

        auto setIfNonEmpty = [](const DropTargetLineEdit *edit, QString &field, bool &hasField) {
            if (!edit->text().isEmpty()) {
                field = edit->text();
                hasField = true;
            }
        };

        setIfNonEmpty(m_firstHeaderLeftEdit,   mp.headerLeft,   mp.hasHeaderLeft);
        setIfNonEmpty(m_firstHeaderCenterEdit,  mp.headerCenter, mp.hasHeaderCenter);
        setIfNonEmpty(m_firstHeaderRightEdit,   mp.headerRight,  mp.hasHeaderRight);
        setIfNonEmpty(m_firstFooterLeftEdit,    mp.footerLeft,   mp.hasFooterLeft);
        setIfNonEmpty(m_firstFooterCenterEdit,  mp.footerCenter, mp.hasFooterCenter);
        setIfNonEmpty(m_firstFooterRightEdit,   mp.footerRight,  mp.hasFooterRight);

        if (!mp.isDefault())
            pl.masterPages.insert(QStringLiteral("first"), mp);
    }

    // Odd/even pages
    if (m_differentOddEven->isChecked()) {
        auto buildMasterPage = [](const QString &name,
                                   const DropTargetLineEdit *hL, const DropTargetLineEdit *hC, const DropTargetLineEdit *hR,
                                   const DropTargetLineEdit *fL, const DropTargetLineEdit *fC, const DropTargetLineEdit *fR) {
            MasterPage mp;
            mp.name = name;
            auto setIfNonEmpty = [](const DropTargetLineEdit *edit, QString &field, bool &hasField) {
                if (!edit->text().isEmpty()) {
                    field = edit->text();
                    hasField = true;
                }
            };
            setIfNonEmpty(hL, mp.headerLeft,   mp.hasHeaderLeft);
            setIfNonEmpty(hC, mp.headerCenter, mp.hasHeaderCenter);
            setIfNonEmpty(hR, mp.headerRight,  mp.hasHeaderRight);
            setIfNonEmpty(fL, mp.footerLeft,   mp.hasFooterLeft);
            setIfNonEmpty(fC, mp.footerCenter, mp.hasFooterCenter);
            setIfNonEmpty(fR, mp.footerRight,  mp.hasFooterRight);
            return mp;
        };

        MasterPage leftMp = buildMasterPage(QStringLiteral("left"),
            m_leftHeaderLeftEdit, m_leftHeaderCenterEdit, m_leftHeaderRightEdit,
            m_leftFooterLeftEdit, m_leftFooterCenterEdit, m_leftFooterRightEdit);
        if (!leftMp.isDefault())
            pl.masterPages.insert(QStringLiteral("left"), leftMp);

        MasterPage rightMp = buildMasterPage(QStringLiteral("right"),
            m_rightHeaderLeftEdit, m_rightHeaderCenterEdit, m_rightHeaderRightEdit,
            m_rightFooterLeftEdit, m_rightFooterCenterEdit, m_rightFooterRightEdit);
        if (!rightMp.isDefault())
            pl.masterPages.insert(QStringLiteral("right"), rightMp);
    }

    return pl;
}
```

**Step 3: Register in CMakeLists.txt**

In `src/CMakeLists.txt`, add after the `droptargetlineedit` entries:

```cmake
    widgets/headerfooterdialog.cpp
    widgets/headerfooterdialog.h
```

**Step 4: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

**Step 5: Commit**

```bash
git add src/widgets/headerfooterdialog.h src/widgets/headerfooterdialog.cpp src/CMakeLists.txt
git commit -m "feat: add HeaderFooterDialog with tile palette and drop-target fields"
```

---

### Task 3: Integrate dialog into PageLayoutWidget

Remove the 6 QLineEdit fields and placeholder hint from `PageLayoutWidget`. Add an "Edit Headers & Footers..." button that launches the dialog.

**Files:**
- Modify: `src/widgets/pagelayoutwidget.h`
- Modify: `src/widgets/pagelayoutwidget.cpp`

**Step 1: Update the header**

In `src/widgets/pagelayoutwidget.h`:

- Remove forward declaration: `class QLineEdit;`
- Add forward declaration: `class QPushButton;`
- Remove member variables:
  - `QLineEdit *m_headerLeftEdit` through `m_footerRightEdit` (all 6)
- Add member variable: `QPushButton *m_editHfButton = nullptr;`
- Add slot: `void onEditHeadersFooters();`

The `m_headerCheck` and `m_footerCheck` members stay.

**Step 2: Update the implementation**

In `src/widgets/pagelayoutwidget.cpp`:

1. Remove `#include <QLineEdit>`, add `#include <QPushButton>` and `#include "headerfooterdialog.h"`

2. In the constructor, replace the header/footer field section (lines ~89-151, from `// --- Header section ---` through the placeholder hint label) with:

```cpp
    // --- Header section ---
    m_headerCheck = new QCheckBox(tr("Header"));
    layout->addWidget(m_headerCheck);

    // --- Footer section ---
    m_footerCheck = new QCheckBox(tr("Footer"));
    m_footerCheck->setChecked(true);
    layout->addWidget(m_footerCheck);

    // Edit button
    m_editHfButton = new QPushButton(tr("Edit Headers && Footers..."));
    m_editHfButton->setEnabled(true); // footer is on by default
    layout->addWidget(m_editHfButton);

    auto updateEditButton = [this]() {
        m_editHfButton->setEnabled(m_headerCheck->isChecked() || m_footerCheck->isChecked());
    };
    connect(m_headerCheck, &QCheckBox::toggled, this, updateEditButton);
    connect(m_footerCheck, &QCheckBox::toggled, this, updateEditButton);
    connect(m_editHfButton, &QPushButton::clicked, this, &PageLayoutWidget::onEditHeadersFooters);
```

3. Remove the 6 QLineEdit signal connections (lines ~172-185) for `headerLeftEdit`, `headerCenterEdit`, `headerRightEdit`, `footerLeftEdit`, `footerCenterEdit`, `footerRightEdit`.

4. In `blockAllSignals()`: remove the 6 QLineEdit `blockSignals` calls. Keep the checkboxes.

5. In `saveCurrentPageTypeState()`: the base layout header/footer text fields are no longer read from QLineEdits. Instead, they are stored in `m_baseLayout` directly (the dialog writes to `m_baseLayout` when accepted). Only read checkbox states from the widget:

```cpp
    if (m_currentPageType.isEmpty()) {
        // ... page size, orientation, margins unchanged ...
        m_baseLayout.headerEnabled = m_headerCheck->isChecked();
        // headerLeft/Center/Right are stored in m_baseLayout directly by the dialog
        m_baseLayout.footerEnabled = m_footerCheck->isChecked();
        // footerLeft/Center/Right likewise
    } else {
        // Master page: only save checkbox states, not text fields
        // (text fields are managed by the dialog)
        MasterPage mp;
        mp.name = m_currentPageType;
        Qt::CheckState hState = m_headerCheck->checkState();
        mp.headerEnabled = (hState == Qt::PartiallyChecked) ? -1 : (hState == Qt::Checked ? 1 : 0);
        Qt::CheckState fState = m_footerCheck->checkState();
        mp.footerEnabled = (fState == Qt::PartiallyChecked) ? -1 : (fState == Qt::Checked ? 1 : 0);
        mp.marginTop = m_marginTopSpin->value();
        mp.marginBottom = m_marginBottomSpin->value();
        mp.marginLeft = m_marginLeftSpin->value();
        mp.marginRight = m_marginRightSpin->value();

        // Preserve any existing text overrides from the dialog
        auto existingIt = m_masterPages.find(m_currentPageType);
        if (existingIt != m_masterPages.end()) {
            const MasterPage &existing = existingIt.value();
            mp.headerLeft = existing.headerLeft;
            mp.hasHeaderLeft = existing.hasHeaderLeft;
            mp.headerCenter = existing.headerCenter;
            mp.hasHeaderCenter = existing.hasHeaderCenter;
            mp.headerRight = existing.headerRight;
            mp.hasHeaderRight = existing.hasHeaderRight;
            mp.footerLeft = existing.footerLeft;
            mp.hasFooterLeft = existing.hasFooterLeft;
            mp.footerCenter = existing.footerCenter;
            mp.hasFooterCenter = existing.hasFooterCenter;
            mp.footerRight = existing.footerRight;
            mp.hasFooterRight = existing.hasFooterRight;
        }

        if (mp.isDefault())
            m_masterPages.remove(m_currentPageType);
        else
            m_masterPages.insert(m_currentPageType, mp);
    }
```

6. In `loadPageTypeState()`: remove all QLineEdit setText/setPlaceholderText/setEnabled calls. Keep the checkbox loading.

7. In `currentPageLayout()`: remove the block reading headerLeft/Center/Right and footerLeft/Center/Right from QLineEdits. The values come from `m_baseLayout` directly.

8. Add the `onEditHeadersFooters()` slot:

```cpp
void PageLayoutWidget::onEditHeadersFooters()
{
    saveCurrentPageTypeState();

    PageLayout current = m_baseLayout;
    current.masterPages = m_masterPages;
    current.headerEnabled = m_headerCheck->isChecked();
    current.footerEnabled = m_footerCheck->isChecked();

    HeaderFooterDialog dlg(current, this);
    if (dlg.exec() == QDialog::Accepted) {
        PageLayout result = dlg.result();

        // Store text fields back into base layout
        m_baseLayout.headerLeft = result.headerLeft;
        m_baseLayout.headerCenter = result.headerCenter;
        m_baseLayout.headerRight = result.headerRight;
        m_baseLayout.footerLeft = result.footerLeft;
        m_baseLayout.footerCenter = result.footerCenter;
        m_baseLayout.footerRight = result.footerRight;

        // Store master pages
        m_masterPages = result.masterPages;

        Q_EMIT pageLayoutChanged();
    }
}
```

**Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

**Step 4: Manual test**

Run: `./build/bin/Penelope`
- Open a document
- Go to the Page dock
- Verify Header and Footer checkboxes are visible
- Verify "Edit Headers & Footers..." button appears and is clickable
- Click button, verify dialog opens with tile palette and field rows
- Drag a tile into a field, verify text is inserted
- Type free text in a field
- Check "Different first page" — verify first page section appears
- Check "Different odd and even pages" — verify even/odd sections appear
- Click OK, verify changes apply (document rebuilds with new header/footer)

**Step 5: Commit**

```bash
git add src/widgets/pagelayoutwidget.h src/widgets/pagelayoutwidget.cpp
git commit -m "feat: replace header/footer text fields with dialog launcher button"
```

---

### Task 4: Include QApplication header for DragTileLabel

The `DragTileLabel` in `headerfooterdialog.cpp` uses `QApplication::startDragDistance()` which requires `#include <QApplication>`.

**Files:**
- Modify: `src/widgets/headerfooterdialog.cpp`

**Step 1: Add missing include**

Add `#include <QApplication>` to the includes in `headerfooterdialog.cpp`.

**Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

**Step 3: Commit**

```bash
git add src/widgets/headerfooterdialog.cpp
git commit -m "fix: add missing QApplication include for drag distance"
```

---

### Task 5: Audit header/footer drawing — canvas PageItem

Verify that `PageItem::paint()` correctly renders headers and footers using `resolvedForPage()`.

**Files:**
- Audit: `src/canvas/pageitem.cpp:70-125`
- Audit: `src/canvas/pageitem.h`

**Step 1: Read and verify**

Read `src/canvas/pageitem.cpp` lines 70-125. Check:
1. `PageMetadata` is populated with correct `pageNumber`, `totalPages`, `fileName`, `title`
2. `resolvedForPage(m_pageNumber)` is called (not raw layout)
3. Header rect is positioned at content top, footer rect at content bottom
4. `HeaderFooterRenderer::drawHeader` and `drawFooter` are called with the resolved layout
5. Separator width is reasonable (0.5)

**Step 2: Verify PageItem receives totalPages and title**

Check how PageItem gets `m_totalPages` and `m_title` set. These must be set when pages are created/updated in `DocumentView`.

**Step 3: Document findings**

If everything is correct, note "PASS" and move on. If there are issues, fix them.

**Step 4: Commit (if any fixes)**

```bash
git add src/canvas/pageitem.cpp
git commit -m "fix: correct header/footer rendering in canvas page items"
```

---

### Task 6: Audit header/footer drawing — PDF generator

The PDF generator re-implements header/footer text rendering manually using raw PDF operators instead of using QPainter-based `HeaderFooterRenderer`. Verify correctness and consistency.

**Files:**
- Audit: `src/pdf/pdfgenerator.cpp:599-706`

**Step 1: Read and verify**

Read `src/pdf/pdfgenerator.cpp` lines 599-706. Check:
1. `resolvedForPage(pageNumber)` is called on the input `pageLayout` ✓ (line 603)
2. `HeaderFooterRenderer::resolveField()` is reused for placeholder resolution ✓ (line 606)
3. Font is Noto Sans 9pt (matches `HeaderFooterRenderer::drawFields`) ✓ (line 612)
4. Color is `0.53 0.53 0.53` RGB which is `#878787` — close to `#888888` in the QPainter path. Check if this is close enough or should be exactly `0x88/0xFF = 0.533...` → `0.53` rounds to this, so it's correct.
5. Separator line color: `0.53 0.53 0.53 RG` — but QPainter path uses `#cccccc`. **This is a mismatch.** The separator should be `0.80 0.80 0.80` to match `#cccccc`.
6. Header Y position: `pageHeight - mTop + kSeparatorGap` — should place text above the separator
7. Footer Y position: `mBottom - kSeparatorGap` — should place text below the separator
8. Separator line positions match content area boundaries

**Step 2: Fix separator color mismatch**

In `pdfgenerator.cpp`, the separator line uses `0.53 0.53 0.53 RG` (same as text color). It should use `0.80 0.80 0.80 RG` to match the `#cccccc` used in `HeaderFooterRenderer`.

Change both separator blocks (header ~line 685 and footer ~line 701):
```
"0.53 0.53 0.53 RG" → "0.80 0.80 0.80 RG"
```

**Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

**Step 4: Commit**

```bash
git add src/pdf/pdfgenerator.cpp
git commit -m "fix: correct PDF header/footer separator line color to match canvas rendering"
```

---

### Task 7: Audit header/footer drawing — print controller

Verify that `PrintController::renderPage()` correctly renders headers and footers.

**Files:**
- Audit: `src/print/printcontroller.cpp:74-198`

**Step 1: Read and verify**

Read `src/print/printcontroller.cpp` lines 74-198. Check:
1. `PageMetadata` populated with `pageNumber`, `totalPages`, `m_fileName`, `m_documentTitle`
2. `resolvedForPage(pageNumber)` is called
3. Header/footer rects are correctly scaled by `dpiScale`
4. `HeaderFooterRenderer::drawHeader/drawFooter` are called with the resolved layout
5. Separator width is scaled by `dpiScale`

**Step 2: Document findings**

If everything is correct, note "PASS" and move on. If there are issues, fix them.

**Step 3: Commit (if any fixes)**

```bash
git add src/print/printcontroller.cpp
git commit -m "fix: correct header/footer rendering in print controller"
```

---

### Task 8: Final integration test

**Step 1: Build the full project**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Clean build, no errors.

**Step 2: Manual end-to-end test**

Run: `./build/bin/Penelope`

1. Open a multi-page markdown document
2. Go to Page dock → check Header, check Footer
3. Click "Edit Headers & Footers..."
4. Drag "Title" tile into Header Left, drag "Page X of Y" into Footer Right
5. Click OK
6. Verify headers/footers appear in the paged canvas view
7. Check "Different first page" → set first page header to empty → verify first page has no header
8. Export to PDF → verify headers/footers render correctly in the PDF
9. Print preview → verify headers/footers render correctly

**Step 3: Commit any final fixes**

```bash
git commit -m "fix: final integration fixes for header/footer designer"
```
