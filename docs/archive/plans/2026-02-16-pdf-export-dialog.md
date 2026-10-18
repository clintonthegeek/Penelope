# PDF Export Options Dialog — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a KPageDialog-based PDF export options dialog with metadata, content filtering, bookmark control, and viewer preferences.

**Architecture:** A `PdfExportDialog` (KPageDialog, FlatList face) builds a `PdfExportOptions` struct from user input. The struct drives content filtering (section exclusion, page range), PDF metadata, bookmark generation, and viewer preferences. Settings persist via KConfig (global) and MetadataStore (per-document).

**Tech Stack:** Qt6 Widgets, KDE Frameworks 6 (KPageDialog, KMessageWidget, KConfig), existing PdfGenerator/PdfWriter pipeline.

---

### Task 1: Create PdfExportOptions struct

**Files:**
- Create: `src/model/pdfexportoptions.h`

**Step 1: Write the header**

```cpp
/*
 * pdfexportoptions.h — Options struct for PDF export dialog
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFEXPORTOPTIONS_H
#define PENELOPE_PDFEXPORTOPTIONS_H

#include <QSet>
#include <QString>

struct PdfExportOptions {
    // General — metadata
    QString title;
    QString author;
    QString subject;
    QString keywords;           // comma-separated

    // General — text copy behavior
    enum TextCopyMode { PlainText, MarkdownSource, UnwrappedParagraphs };
    TextCopyMode textCopyMode = PlainText;

    // Content — section selection
    QSet<int> excludedHeadingIndices;   // indices into doc.blocks of unchecked headings
    bool sectionsModified = false;      // true if user changed any checkboxes

    // Content — page range
    QString pageRangeExpr;              // raw expression, empty = all pages
    bool pageRangeModified = false;     // true if user entered a range

    // Output — bookmarks
    bool includeBookmarks = true;
    int bookmarkMaxDepth = 6;           // 1–6

    // Output — viewer preferences
    enum InitialView { ViewerDefault, ShowBookmarks, ShowThumbnails };
    InitialView initialView = ShowBookmarks;

    enum PageLayout { SinglePage, Continuous, FacingPages, FacingPagesFirstAlone };
    PageLayout pageLayout = Continuous;
};

#endif // PENELOPE_PDFEXPORTOPTIONS_H
```

**Step 2: Add to CMakeLists.txt**

In `src/CMakeLists.txt`, add `model/pdfexportoptions.h` after `model/contentmodel.h` in the `PenelopeCore` source list.

**Step 3: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 4: Commit**

```bash
git add src/model/pdfexportoptions.h src/CMakeLists.txt
git commit -m "Add PdfExportOptions struct for export dialog"
```

---

### Task 2: Add page range parser

A standalone function that parses expressions like `1-5, 8, first, last, (last-3)-last` into a `QSet<int>` of 1-based page numbers.

**Files:**
- Create: `src/model/pagerangeparser.h`
- Create: `src/model/pagerangeparser.cpp`

**Step 1: Write the header**

```cpp
/*
 * pagerangeparser.h — Parse page range expressions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PAGERANGEPARSER_H
#define PENELOPE_PAGERANGEPARSER_H

#include <QSet>
#include <QString>

namespace PageRangeParser {

struct Result {
    QSet<int> pages;        // 1-based page numbers
    bool valid = true;
    QString errorMessage;   // non-empty if invalid
};

// Parse an expression like "1-5, 8, first, (last-3)-last"
// totalPages is required to resolve "last" keyword.
Result parse(const QString &expr, int totalPages);

} // namespace PageRangeParser

#endif // PENELOPE_PAGERANGEPARSER_H
```

**Step 2: Write the implementation**

`src/model/pagerangeparser.cpp`:

```cpp
/*
 * pagerangeparser.cpp — Parse page range expressions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pagerangeparser.h"

#include <QRegularExpression>
#include <QStringList>

namespace PageRangeParser {

// Resolve a single token: a number, "first", "last", or "(last-N)"
static int resolveToken(const QString &token, int totalPages, bool &ok)
{
    ok = true;
    QString t = token.trimmed().toLower();
    if (t == QLatin1String("first")) return 1;
    if (t == QLatin1String("last")) return totalPages;

    // "(last-N)" pattern
    static QRegularExpression parenRe(
        QStringLiteral(R"(^\(last\s*-\s*(\d+)\)$)"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = parenRe.match(t);
    if (m.hasMatch()) {
        int offset = m.captured(1).toInt();
        int page = totalPages - offset;
        if (page < 1) { ok = false; return -1; }
        return page;
    }

    // "last-N" without parens (when used standalone, not as range endpoint)
    static QRegularExpression lastMinusRe(
        QStringLiteral(R"(^last\s*-\s*(\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    m = lastMinusRe.match(t);
    if (m.hasMatch()) {
        int offset = m.captured(1).toInt();
        int page = totalPages - offset;
        if (page < 1) { ok = false; return -1; }
        return page;
    }

    // Plain number
    int page = t.toInt(&ok);
    if (!ok || page < 1 || page > totalPages) {
        ok = false;
        return -1;
    }
    return page;
}

Result parse(const QString &expr, int totalPages)
{
    Result result;
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty()) {
        // Empty = all pages
        for (int i = 1; i <= totalPages; ++i)
            result.pages.insert(i);
        return result;
    }

    // Split by comma
    QStringList parts = trimmed.split(QLatin1Char(','));
    for (const QString &part : parts) {
        QString p = part.trimmed();
        if (p.isEmpty()) continue;

        // Check for range: split on '-' but not inside "(last-N)"
        // Strategy: find a '-' that is not preceded by '(' context
        // Simple approach: use regex to match "token-token" where token
        // can contain parens
        static QRegularExpression rangeRe(
            QStringLiteral(R"(^(.+?)\s*-\s*(?!.*\()(.+)$)"));

        // Better approach: split smartly. A range has exactly one '-' that
        // is not inside parentheses and not part of "last-N".
        // Find all '-' positions not inside parens:
        int splitPos = -1;
        int parenDepth = 0;
        for (int i = 0; i < p.size(); ++i) {
            if (p[i] == QLatin1Char('(')) parenDepth++;
            else if (p[i] == QLatin1Char(')')) parenDepth--;
            else if (p[i] == QLatin1Char('-') && parenDepth == 0) {
                // Check if this '-' is part of "last-N" by seeing if
                // the left side ends with "last" (possibly with spaces)
                QString left = p.left(i).trimmed().toLower();
                if (left.endsWith(QLatin1String("last"))) {
                    // This is "last-N", not a range separator.
                    // But if left IS exactly "last" or "(last-N)", this
                    // could be a range like "last-3" meaning page (last-3).
                    // We only treat it as a range separator if left is
                    // a resolvable standalone token.
                    bool leftOk;
                    resolveToken(left, totalPages, leftOk);
                    if (leftOk && left == QLatin1String("last")) {
                        // "last-N" is ambiguous. Treat as (last-N) single page
                        // unless right side is also a token.
                        QString right = p.mid(i + 1).trimmed();
                        bool rightIsToken;
                        resolveToken(right, totalPages, rightIsToken);
                        if (!rightIsToken) {
                            // "last-N" where N is just a number -> single page
                            continue;
                        }
                        // Both sides resolve -> range "last" to "right"
                        splitPos = i;
                        break;
                    }
                    continue; // skip, part of "last-N"
                }
                splitPos = i;
                break;
            }
        }

        if (splitPos >= 0) {
            // Range
            QString leftStr = p.left(splitPos).trimmed();
            QString rightStr = p.mid(splitPos + 1).trimmed();
            bool leftOk, rightOk;
            int start = resolveToken(leftStr, totalPages, leftOk);
            int end = resolveToken(rightStr, totalPages, rightOk);
            if (!leftOk || !rightOk || start > end) {
                result.valid = false;
                result.errorMessage = QObject::tr("Invalid range: %1").arg(p);
                return result;
            }
            for (int i = start; i <= end; ++i)
                result.pages.insert(i);
        } else {
            // Single page
            bool ok;
            int page = resolveToken(p, totalPages, ok);
            if (!ok) {
                result.valid = false;
                result.errorMessage = QObject::tr("Invalid page: %1").arg(p);
                return result;
            }
            result.pages.insert(page);
        }
    }

    return result;
}

} // namespace PageRangeParser
```

**Step 3: Add to CMakeLists.txt**

Add `model/pagerangeparser.cpp` and `model/pagerangeparser.h` after `model/pdfexportoptions.h`.

**Step 4: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 5: Commit**

```bash
git add src/model/pagerangeparser.h src/model/pagerangeparser.cpp src/CMakeLists.txt
git commit -m "Add page range expression parser with first/last/arithmetic"
```

---

### Task 3: Add PdfExport KConfig group

**Files:**
- Modify: `src/app/penelope.kcfg`

**Step 1: Add the PdfExport group**

After the `</group>` closing the `CopyOptions` group (line 129), add:

```xml
  <group name="PdfExport">
    <entry name="PdfAuthor" type="String">
      <label>Default author for PDF metadata.</label>
      <default></default>
    </entry>
    <entry name="PdfTextCopyMode" type="Int">
      <label>Text copy behavior: 0=PlainText, 1=MarkdownSource, 2=UnwrappedParagraphs.</label>
      <default>0</default>
      <min>0</min>
      <max>2</max>
    </entry>
    <entry name="PdfIncludeBookmarks" type="Bool">
      <label>Include PDF bookmarks/outlines.</label>
      <default>true</default>
    </entry>
    <entry name="PdfBookmarkMaxDepth" type="Int">
      <label>Maximum heading depth for bookmarks (1-6).</label>
      <default>6</default>
      <min>1</min>
      <max>6</max>
    </entry>
    <entry name="PdfInitialView" type="Int">
      <label>PDF initial view: 0=ViewerDefault, 1=ShowBookmarks, 2=ShowThumbnails.</label>
      <default>1</default>
      <min>0</min>
      <max>2</max>
    </entry>
    <entry name="PdfPageLayout" type="Int">
      <label>PDF page layout: 0=SinglePage, 1=Continuous, 2=FacingPages, 3=FacingPagesFirstAlone.</label>
      <default>1</default>
      <min>0</min>
      <max>3</max>
    </entry>
  </group>
```

**Step 2: Reconfigure and build**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make -C build -j$(($(nproc)-1))`
Expected: KConfig code generator picks up new entries, builds clean.

**Step 3: Commit**

```bash
git add src/app/penelope.kcfg
git commit -m "Add PdfExport KConfig group for export dialog defaults"
```

---

### Task 4: Create PdfExportDialog (KPageDialog)

Three pages: General (metadata + text copy mode), Content (heading tree + page range), Output (bookmarks + viewer prefs).

**Files:**
- Create: `src/widgets/pdfexportdialog.h`
- Create: `src/widgets/pdfexportdialog.cpp`

**Step 1: Write the header**

```cpp
/*
 * pdfexportdialog.h — PDF export options dialog (KPageDialog)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFEXPORTDIALOG_H
#define PENELOPE_PDFEXPORTDIALOG_H

#include <KPageDialog>

#include "contentmodel.h"
#include "pdfexportoptions.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class KMessageWidget;

class PdfExportDialog : public KPageDialog
{
    Q_OBJECT

public:
    PdfExportDialog(const Content::Document &doc,
                    int pageCount,
                    const QString &defaultTitle,
                    QWidget *parent = nullptr);

    PdfExportOptions options() const;

    // Pre-fill from saved options (KConfig + MetadataStore overlay)
    void setOptions(const PdfExportOptions &opts);

private:
    void setupGeneralPage();
    void setupContentPage();
    void setupOutputPage();

    void buildHeadingTree(const Content::Document &doc);
    void onHeadingItemChanged(QTreeWidgetItem *item, int column);
    void updateConflictWarning();
    void onSectionCheckboxChanged();
    void onPageRangeChanged();

    // General page
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_authorEdit = nullptr;
    QLineEdit *m_subjectEdit = nullptr;
    QLineEdit *m_keywordsEdit = nullptr;
    QComboBox *m_textCopyCombo = nullptr;

    // Content page
    QTreeWidget *m_headingTree = nullptr;
    QLineEdit *m_pageRangeEdit = nullptr;
    KMessageWidget *m_conflictWarning = nullptr;
    bool m_sectionsModified = false;
    bool m_pageRangeModified = false;
    int m_pageCount = 0;

    // Heading index mapping: tree item -> index into doc.blocks
    QHash<QTreeWidgetItem *, int> m_headingBlockIndex;

    // Output page
    QCheckBox *m_includeBookmarks = nullptr;
    QSpinBox *m_bookmarkDepth = nullptr;
    QComboBox *m_initialViewCombo = nullptr;
    QComboBox *m_pageLayoutCombo = nullptr;

    // Block cascading re-entrance
    bool m_updatingTree = false;
};

#endif // PENELOPE_PDFEXPORTDIALOG_H
```

**Step 2: Write the implementation**

`src/widgets/pdfexportdialog.cpp`:

```cpp
/*
 * pdfexportdialog.cpp — PDF export options dialog (KPageDialog)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pdfexportdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <KLocalizedString>
#include <KMessageWidget>

PdfExportDialog::PdfExportDialog(const Content::Document &doc,
                                 int pageCount,
                                 const QString &defaultTitle,
                                 QWidget *parent)
    : KPageDialog(parent)
    , m_pageCount(pageCount)
{
    setWindowTitle(i18n("PDF Export Options"));
    setFaceType(KPageDialog::FlatList);
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setupGeneralPage();
    setupContentPage();
    setupOutputPage();

    // Pre-fill title
    m_titleEdit->setText(defaultTitle);

    // Build heading tree from document
    buildHeadingTree(doc);

    resize(600, 500);
}

// --- General page ---

void PdfExportDialog::setupGeneralPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    // Metadata group
    auto *metaGroup = new QGroupBox(i18n("Metadata"), page);
    auto *metaForm = new QFormLayout(metaGroup);

    m_titleEdit = new QLineEdit(metaGroup);
    metaForm->addRow(i18n("Title:"), m_titleEdit);

    m_authorEdit = new QLineEdit(metaGroup);
    metaForm->addRow(i18n("Author:"), m_authorEdit);

    m_subjectEdit = new QLineEdit(metaGroup);
    metaForm->addRow(i18n("Subject:"), m_subjectEdit);

    m_keywordsEdit = new QLineEdit(metaGroup);
    m_keywordsEdit->setPlaceholderText(i18n("comma-separated"));
    metaForm->addRow(i18n("Keywords:"), m_keywordsEdit);

    layout->addWidget(metaGroup);

    // Text copy behavior group
    auto *copyGroup = new QGroupBox(i18n("Text Copy Behavior"), page);
    auto *copyForm = new QFormLayout(copyGroup);

    m_textCopyCombo = new QComboBox(copyGroup);
    m_textCopyCombo->addItem(i18n("Plain text"), PdfExportOptions::PlainText);
    m_textCopyCombo->addItem(i18n("Markdown source"), PdfExportOptions::MarkdownSource);
    m_textCopyCombo->addItem(i18n("Unwrapped paragraphs"), PdfExportOptions::UnwrappedParagraphs);
    copyForm->addRow(i18n("When text is copied from PDF:"), m_textCopyCombo);

    layout->addWidget(copyGroup);
    layout->addStretch();

    auto *pageItem = addPage(page, i18n("General"));
    pageItem->setIcon(QIcon::fromTheme(QStringLiteral("document-properties")));
}

// --- Content page ---

void PdfExportDialog::setupContentPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    // Section selection
    auto *sectionGroup = new QGroupBox(i18n("Sections"), page);
    auto *sectionLayout = new QVBoxLayout(sectionGroup);

    m_headingTree = new QTreeWidget(sectionGroup);
    m_headingTree->setHeaderHidden(true);
    m_headingTree->setRootIsDecorated(true);
    m_headingTree->setIndentation(16);
    sectionLayout->addWidget(m_headingTree);

    connect(m_headingTree, &QTreeWidget::itemChanged,
            this, &PdfExportDialog::onHeadingItemChanged);

    // Select All / Deselect All buttons
    auto *btnLayout = new QHBoxLayout;
    auto *selectAllBtn = new QPushButton(i18n("Select All"), sectionGroup);
    auto *deselectAllBtn = new QPushButton(i18n("Deselect All"), sectionGroup);
    btnLayout->addWidget(selectAllBtn);
    btnLayout->addWidget(deselectAllBtn);
    btnLayout->addStretch();
    sectionLayout->addLayout(btnLayout);

    connect(selectAllBtn, &QPushButton::clicked, this, [this]() {
        m_updatingTree = true;
        for (int i = 0; i < m_headingTree->topLevelItemCount(); ++i) {
            auto *item = m_headingTree->topLevelItem(i);
            item->setCheckState(0, Qt::Checked);
        }
        m_updatingTree = false;
        onSectionCheckboxChanged();
    });
    connect(deselectAllBtn, &QPushButton::clicked, this, [this]() {
        m_updatingTree = true;
        for (int i = 0; i < m_headingTree->topLevelItemCount(); ++i) {
            auto *item = m_headingTree->topLevelItem(i);
            item->setCheckState(0, Qt::Unchecked);
        }
        m_updatingTree = false;
        onSectionCheckboxChanged();
    });

    layout->addWidget(sectionGroup);

    // Conflict warning (hidden by default)
    m_conflictWarning = new KMessageWidget(page);
    m_conflictWarning->setMessageType(KMessageWidget::Warning);
    m_conflictWarning->setText(
        i18n("Both section selection and page range are active. "
             "Only pages that match both filters will be exported. "
             "This may produce unexpected results."));
    m_conflictWarning->setCloseButtonVisible(true);
    m_conflictWarning->setVisible(false);
    layout->addWidget(m_conflictWarning);

    // Page range
    auto *rangeGroup = new QGroupBox(i18n("Page Range"), page);
    auto *rangeForm = new QFormLayout(rangeGroup);

    m_pageRangeEdit = new QLineEdit(rangeGroup);
    m_pageRangeEdit->setPlaceholderText(
        i18n("e.g. 1-5, 8, first, (last-3)-last"));
    rangeForm->addRow(i18n("Pages:"), m_pageRangeEdit);

    auto *rangeHint = new QLabel(
        i18n("Leave empty for all pages. Supports: numbers, ranges (1-5), "
             "first, last, (last-3)-last."),
        rangeGroup);
    rangeHint->setWordWrap(true);
    rangeHint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    rangeForm->addRow(rangeHint);

    connect(m_pageRangeEdit, &QLineEdit::textChanged,
            this, &PdfExportDialog::onPageRangeChanged);

    layout->addWidget(rangeGroup);

    auto *pageItem = addPage(page, i18n("Content"));
    pageItem->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
}

// --- Output page ---

void PdfExportDialog::setupOutputPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    // Bookmarks group
    auto *bookmarkGroup = new QGroupBox(i18n("Bookmarks"), page);
    auto *bookmarkForm = new QFormLayout(bookmarkGroup);

    m_includeBookmarks = new QCheckBox(i18n("Include bookmarks"), bookmarkGroup);
    m_includeBookmarks->setChecked(true);
    bookmarkForm->addRow(m_includeBookmarks);

    m_bookmarkDepth = new QSpinBox(bookmarkGroup);
    m_bookmarkDepth->setRange(1, 6);
    m_bookmarkDepth->setValue(6);
    bookmarkForm->addRow(i18n("Maximum depth:"), m_bookmarkDepth);

    connect(m_includeBookmarks, &QCheckBox::toggled,
            m_bookmarkDepth, &QSpinBox::setEnabled);

    layout->addWidget(bookmarkGroup);

    // Viewer preferences group
    auto *viewerGroup = new QGroupBox(i18n("Viewer Preferences"), page);
    auto *viewerForm = new QFormLayout(viewerGroup);

    m_initialViewCombo = new QComboBox(viewerGroup);
    m_initialViewCombo->addItem(i18n("Viewer default"), PdfExportOptions::ViewerDefault);
    m_initialViewCombo->addItem(i18n("Show bookmarks"), PdfExportOptions::ShowBookmarks);
    m_initialViewCombo->addItem(i18n("Show thumbnails"), PdfExportOptions::ShowThumbnails);
    m_initialViewCombo->setCurrentIndex(1); // Show bookmarks
    viewerForm->addRow(i18n("Initial view:"), m_initialViewCombo);

    m_pageLayoutCombo = new QComboBox(viewerGroup);
    m_pageLayoutCombo->addItem(i18n("Single page"), PdfExportOptions::SinglePage);
    m_pageLayoutCombo->addItem(i18n("Continuous"), PdfExportOptions::Continuous);
    m_pageLayoutCombo->addItem(i18n("Facing pages"), PdfExportOptions::FacingPages);
    m_pageLayoutCombo->addItem(i18n("Facing pages (first alone)"), PdfExportOptions::FacingPagesFirstAlone);
    m_pageLayoutCombo->setCurrentIndex(1); // Continuous
    viewerForm->addRow(i18n("Page layout:"), m_pageLayoutCombo);

    layout->addWidget(viewerGroup);
    layout->addStretch();

    auto *pageItem = addPage(page, i18n("Output"));
    pageItem->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
}

// --- Heading tree ---

void PdfExportDialog::buildHeadingTree(const Content::Document &doc)
{
    m_headingTree->clear();
    m_headingBlockIndex.clear();

    QTreeWidgetItem *parents[7] = {};

    for (int blockIdx = 0; blockIdx < doc.blocks.size(); ++blockIdx) {
        const auto *heading = std::get_if<Content::Heading>(&doc.blocks[blockIdx]);
        if (!heading)
            continue;

        int level = heading->level;
        if (level < 1 || level > 6)
            continue;

        // Extract text
        QString text;
        for (const auto &node : heading->inlines) {
            std::visit([&](const auto &n) {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Content::TextRun>)
                    text += n.text;
                else if constexpr (std::is_same_v<T, Content::InlineCode>)
                    text += n.text;
                else if constexpr (std::is_same_v<T, Content::Link>)
                    text += n.text;
            }, node);
        }
        text = text.trimmed();
        if (text.isEmpty())
            continue;

        auto *item = new QTreeWidgetItem;
        item->setText(0, text);
        item->setCheckState(0, Qt::Checked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        m_headingBlockIndex[item] = blockIdx;

        // Find parent
        QTreeWidgetItem *parent = nullptr;
        for (int i = level - 1; i >= 1; --i) {
            if (parents[i]) {
                parent = parents[i];
                break;
            }
        }

        if (parent)
            parent->addChild(item);
        else
            m_headingTree->addTopLevelItem(item);

        parents[level] = item;
        for (int i = level + 1; i <= 6; ++i)
            parents[i] = nullptr;
    }

    m_headingTree->expandAll();
}

void PdfExportDialog::onHeadingItemChanged(QTreeWidgetItem *item, int column)
{
    if (column != 0 || m_updatingTree)
        return;

    // Cascade to children
    m_updatingTree = true;
    Qt::CheckState state = item->checkState(0);
    std::function<void(QTreeWidgetItem *)> cascade = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            auto *child = parent->child(i);
            child->setCheckState(0, state);
            cascade(child);
        }
    };
    cascade(item);
    m_updatingTree = false;

    onSectionCheckboxChanged();
}

void PdfExportDialog::onSectionCheckboxChanged()
{
    // Check if any heading is unchecked
    m_sectionsModified = false;
    std::function<void(QTreeWidgetItem *)> check = [&](QTreeWidgetItem *item) {
        if (item->checkState(0) == Qt::Unchecked)
            m_sectionsModified = true;
        for (int i = 0; i < item->childCount(); ++i)
            check(item->child(i));
    };
    for (int i = 0; i < m_headingTree->topLevelItemCount(); ++i)
        check(m_headingTree->topLevelItem(i));

    updateConflictWarning();
}

void PdfExportDialog::onPageRangeChanged()
{
    m_pageRangeModified = !m_pageRangeEdit->text().trimmed().isEmpty();

    // Basic validation: try parsing
    if (m_pageRangeModified && m_pageCount > 0) {
        // Inline validation will be added when pagerangeparser is available
        // For now just update the conflict warning
    }

    updateConflictWarning();
}

void PdfExportDialog::updateConflictWarning()
{
    bool showWarning = m_sectionsModified && m_pageRangeModified;
    if (showWarning && !m_conflictWarning->isVisible())
        m_conflictWarning->animatedShow();
    else if (!showWarning && m_conflictWarning->isVisible())
        m_conflictWarning->animatedHide();
}

// --- Options getter/setter ---

PdfExportOptions PdfExportDialog::options() const
{
    PdfExportOptions opts;

    // General
    opts.title = m_titleEdit->text();
    opts.author = m_authorEdit->text();
    opts.subject = m_subjectEdit->text();
    opts.keywords = m_keywordsEdit->text();
    opts.textCopyMode = static_cast<PdfExportOptions::TextCopyMode>(
        m_textCopyCombo->currentData().toInt());

    // Content — excluded headings
    std::function<void(QTreeWidgetItem *)> collectExcluded =
        [&](QTreeWidgetItem *item) {
        if (item->checkState(0) == Qt::Unchecked) {
            auto it = m_headingBlockIndex.find(item);
            if (it != m_headingBlockIndex.end())
                opts.excludedHeadingIndices.insert(it.value());
        }
        for (int i = 0; i < item->childCount(); ++i)
            collectExcluded(item->child(i));
    };
    for (int i = 0; i < m_headingTree->topLevelItemCount(); ++i)
        collectExcluded(m_headingTree->topLevelItem(i));

    opts.sectionsModified = m_sectionsModified;

    // Content — page range
    opts.pageRangeExpr = m_pageRangeEdit->text().trimmed();
    opts.pageRangeModified = m_pageRangeModified;

    // Output
    opts.includeBookmarks = m_includeBookmarks->isChecked();
    opts.bookmarkMaxDepth = m_bookmarkDepth->value();
    opts.initialView = static_cast<PdfExportOptions::InitialView>(
        m_initialViewCombo->currentData().toInt());
    opts.pageLayout = static_cast<PdfExportOptions::PageLayout>(
        m_pageLayoutCombo->currentData().toInt());

    return opts;
}

void PdfExportDialog::setOptions(const PdfExportOptions &opts)
{
    // General
    if (!opts.title.isEmpty())
        m_titleEdit->setText(opts.title);
    m_authorEdit->setText(opts.author);
    m_subjectEdit->setText(opts.subject);
    m_keywordsEdit->setText(opts.keywords);
    m_textCopyCombo->setCurrentIndex(
        m_textCopyCombo->findData(opts.textCopyMode));

    // Content — page range
    m_pageRangeEdit->setText(opts.pageRangeExpr);

    // Content — excluded headings: uncheck matching items
    if (!opts.excludedHeadingIndices.isEmpty()) {
        m_updatingTree = true;
        for (auto it = m_headingBlockIndex.begin();
             it != m_headingBlockIndex.end(); ++it) {
            if (opts.excludedHeadingIndices.contains(it.value()))
                it.key()->setCheckState(0, Qt::Unchecked);
        }
        m_updatingTree = false;
        onSectionCheckboxChanged();
    }

    // Output
    m_includeBookmarks->setChecked(opts.includeBookmarks);
    m_bookmarkDepth->setValue(opts.bookmarkMaxDepth);
    m_initialViewCombo->setCurrentIndex(
        m_initialViewCombo->findData(opts.initialView));
    m_pageLayoutCombo->setCurrentIndex(
        m_pageLayoutCombo->findData(opts.pageLayout));
}
```

**Step 3: Add to CMakeLists.txt**

In the `Widgets` section of `src/CMakeLists.txt`, add:
```
    widgets/pdfexportdialog.cpp
    widgets/pdfexportdialog.h
```

**Step 4: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 5: Commit**

```bash
git add src/widgets/pdfexportdialog.h src/widgets/pdfexportdialog.cpp src/CMakeLists.txt
git commit -m "Add PdfExportDialog with General, Content, and Output pages"
```

---

### Task 5: Add inline validation to page range input

Wire the `PageRangeParser` into the dialog's page range `QLineEdit` for real-time validation feedback.

**Files:**
- Modify: `src/widgets/pdfexportdialog.cpp`

**Step 1: Add include and validation logic**

Add `#include "pagerangeparser.h"` to the includes.

Replace the `onPageRangeChanged()` body with:

```cpp
void PdfExportDialog::onPageRangeChanged()
{
    QString text = m_pageRangeEdit->text().trimmed();
    m_pageRangeModified = !text.isEmpty();

    if (m_pageRangeModified && m_pageCount > 0) {
        auto result = PageRangeParser::parse(text, m_pageCount);
        if (!result.valid) {
            m_pageRangeEdit->setStyleSheet(
                QStringLiteral("QLineEdit { border: 1px solid red; }"));
            m_pageRangeEdit->setToolTip(result.errorMessage);
        } else {
            m_pageRangeEdit->setStyleSheet(QString());
            m_pageRangeEdit->setToolTip(
                i18np("%1 page selected", "%1 pages selected",
                      result.pages.size()));
        }
    } else {
        m_pageRangeEdit->setStyleSheet(QString());
        m_pageRangeEdit->setToolTip(QString());
    }

    updateConflictWarning();
}
```

**Step 2: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 3: Commit**

```bash
git add src/widgets/pdfexportdialog.cpp
git commit -m "Add inline page range validation with red border feedback"
```

---

### Task 6: Extend PdfGenerator with export options

Add metadata fields (author, subject, keywords), bookmark depth filtering, viewer preferences, and page layout to the PDF output.

**Files:**
- Modify: `src/pdf/pdfgenerator.h`
- Modify: `src/pdf/pdfgenerator.cpp`

**Step 1: Update the header**

In `pdfgenerator.h`, add include and new method/member:

```cpp
#include "pdfexportoptions.h"
```

Add to the public section after `setMaxJustifyGap`:

```cpp
void setExportOptions(const PdfExportOptions &opts) { m_exportOptions = opts; }
```

Add to private members:

```cpp
PdfExportOptions m_exportOptions;
```

**Step 2: Update the Info object in generate()**

In `pdfgenerator.cpp`, replace the Info object block (around lines 212-220) with:

```cpp
    // Info object
    writer.startObj(writer.infoObj());
    writer.write("<<\n");
    writer.write("/Producer " + Pdf::toLiteralString(QStringLiteral("Penelope")) + "\n");

    // Title: prefer export options, fall back to m_title
    QString infoTitle = m_exportOptions.title.isEmpty() ? m_title : m_exportOptions.title;
    if (!infoTitle.isEmpty())
        writer.write("/Title " + Pdf::toLiteralString(Pdf::toUTF16(infoTitle)) + "\n");
    if (!m_exportOptions.author.isEmpty())
        writer.write("/Author " + Pdf::toLiteralString(Pdf::toUTF16(m_exportOptions.author)) + "\n");
    if (!m_exportOptions.subject.isEmpty())
        writer.write("/Subject " + Pdf::toLiteralString(Pdf::toUTF16(m_exportOptions.subject)) + "\n");
    if (!m_exportOptions.keywords.isEmpty())
        writer.write("/Keywords " + Pdf::toLiteralString(Pdf::toUTF16(m_exportOptions.keywords)) + "\n");

    writer.write("/CreationDate " + Pdf::toDateString(QDateTime::currentDateTime()) + "\n");
    writer.write(">>");
    writer.endObj(writer.infoObj());
```

**Step 3: Update the Catalog object for viewer preferences**

Replace the Catalog object block (around lines 222-228) with:

```cpp
    // Catalog object
    writer.startObj(writer.catalogObj());
    writer.write("<<\n/Type /Catalog\n/Pages " + Pdf::toObjRef(writer.pagesObj()) + "\n");

    if (outlineObj && m_exportOptions.includeBookmarks)
        writer.write("/Outlines " + Pdf::toObjRef(outlineObj) + "\n");

    // PageMode
    switch (m_exportOptions.initialView) {
    case PdfExportOptions::ShowBookmarks:
        if (outlineObj && m_exportOptions.includeBookmarks)
            writer.write("/PageMode /UseOutlines\n");
        break;
    case PdfExportOptions::ShowThumbnails:
        writer.write("/PageMode /UseThumbs\n");
        break;
    case PdfExportOptions::ViewerDefault:
    default:
        break;
    }

    // PageLayout
    switch (m_exportOptions.pageLayout) {
    case PdfExportOptions::SinglePage:
        writer.write("/PageLayout /SinglePage\n");
        break;
    case PdfExportOptions::Continuous:
        writer.write("/PageLayout /OneColumn\n");
        break;
    case PdfExportOptions::FacingPages:
        writer.write("/PageLayout /TwoColumnLeft\n");
        break;
    case PdfExportOptions::FacingPagesFirstAlone:
        writer.write("/PageLayout /TwoColumnRight\n");
        break;
    }

    writer.write(">>");
    writer.endObj(writer.catalogObj());
```

**Step 4: Filter bookmark depth in writeOutlines()**

In `writeOutlines()`, when collecting headings, skip entries whose `level > m_exportOptions.bookmarkMaxDepth`. Find the heading collection loop and add:

```cpp
if (heading.headingLevel > m_exportOptions.bookmarkMaxDepth)
    continue;
```

Also, at the top of `writeOutlines()`, early-return if bookmarks are disabled:

```cpp
if (!m_exportOptions.includeBookmarks)
    return 0;
```

**Step 5: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 6: Commit**

```bash
git add src/pdf/pdfgenerator.h src/pdf/pdfgenerator.cpp
git commit -m "Add PDF metadata, viewer preferences, and bookmark depth filtering"
```

---

### Task 7: Add content section filtering

Implement a function that removes excluded sections from a `Content::Document` based on heading indices. An excluded heading removes all content from that heading through to (but not including) the next heading of the same or higher level.

**Files:**
- Create: `src/model/contentfilter.h`
- Create: `src/model/contentfilter.cpp`

**Step 1: Write the header**

```cpp
/*
 * contentfilter.h — Filter Content::Document by section selection
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_CONTENTFILTER_H
#define PENELOPE_CONTENTFILTER_H

#include <QSet>

#include "contentmodel.h"

namespace ContentFilter {

// Remove excluded sections from a document.
// excludedHeadingIndices: indices into doc.blocks that are Heading blocks.
// Removing a heading also removes all content up to the next heading of
// the same or higher level.
Content::Document filterSections(const Content::Document &doc,
                                  const QSet<int> &excludedHeadingIndices);

} // namespace ContentFilter

#endif // PENELOPE_CONTENTFILTER_H
```

**Step 2: Write the implementation**

```cpp
/*
 * contentfilter.cpp — Filter Content::Document by section selection
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "contentfilter.h"

namespace ContentFilter {

Content::Document filterSections(const Content::Document &doc,
                                  const QSet<int> &excludedHeadingIndices)
{
    if (excludedHeadingIndices.isEmpty())
        return doc;

    // Build a set of block indices to exclude.
    // For each excluded heading at index i with level L, exclude blocks
    // i..j-1 where j is the next heading with level <= L (or end of doc).
    QSet<int> excludedBlocks;

    for (int idx : excludedHeadingIndices) {
        if (idx < 0 || idx >= doc.blocks.size())
            continue;

        const auto *heading = std::get_if<Content::Heading>(&doc.blocks[idx]);
        if (!heading)
            continue;

        int level = heading->level;
        excludedBlocks.insert(idx);

        // Exclude subsequent blocks until we hit a heading of same or higher level
        for (int j = idx + 1; j < doc.blocks.size(); ++j) {
            const auto *nextHeading = std::get_if<Content::Heading>(&doc.blocks[j]);
            if (nextHeading && nextHeading->level <= level)
                break;
            excludedBlocks.insert(j);
        }
    }

    // Build filtered document
    Content::Document filtered;
    for (int i = 0; i < doc.blocks.size(); ++i) {
        if (!excludedBlocks.contains(i))
            filtered.blocks.append(doc.blocks[i]);
    }

    return filtered;
}

} // namespace ContentFilter
```

**Step 3: Add to CMakeLists.txt**

Add `model/contentfilter.cpp` and `model/contentfilter.h` to the source list.

**Step 4: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 5: Commit**

```bash
git add src/model/contentfilter.h src/model/contentfilter.cpp src/CMakeLists.txt
git commit -m "Add content section filtering by heading exclusion"
```

---

### Task 8: Wire dialog into MainWindow export flow

Change `onFileExportPdf()` to: build content → show PdfExportDialog → filter sections → layout → filter pages → generate PDF with options.

**Files:**
- Modify: `src/app/mainwindow.cpp`
- Modify: `src/app/mainwindow.h` (add include)

**Step 1: Add includes to mainwindow.cpp**

```cpp
#include "pdfexportdialog.h"
#include "pdfexportoptions.h"
#include "contentfilter.h"
#include "pagerangeparser.h"
#include "penelopesettings.h"
```

(Some of these may already be included.)

**Step 2: Rewrite the PDF pipeline section of onFileExportPdf()**

The current flow is:
1. Read markdown
2. Build content
3. Layout
4. Generate PDF

New flow:
1. Read markdown
2. Build content (needed for heading tree in dialog)
3. Show PdfExportDialog (with pre-loaded KConfig + MetadataStore options)
4. On accept: show file save dialog
5. Filter content by excluded sections
6. Layout
7. Filter pages by range
8. Generate PDF with options

Replace the body of the `if (view->isPdfMode())` block (lines 594-659 approximately) with:

```cpp
    if (view->isPdfMode()) {
        auto *tab = currentDocumentTab();
        if (!tab)
            return;

        QString filePath = tab->filePath();
        QString markdown;
        if (tab->isSourceMode()) {
            markdown = tab->sourceText();
        } else {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return;
            markdown = QString::fromUtf8(file.readAll());
            file.close();
        }

        StyleManager *editingSm = m_styleDockWidget->currentStyleManager();
        StyleManager *styleManager;
        if (editingSm) {
            styleManager = editingSm->clone(this);
        } else {
            styleManager = new StyleManager(this);
            QString themeId = m_styleDockWidget->currentThemeId();
            if (!m_themeManager->loadTheme(themeId, styleManager))
                m_themeManager->loadDefaults(styleManager);
        }

        QFileInfo fi(filePath);
        PageLayout pl = m_pageLayoutWidget->currentPageLayout();

        // Build content (needed for heading tree + page count)
        ContentBuilder contentBuilder;
        contentBuilder.setBasePath(fi.absolutePath());
        contentBuilder.setStyleManager(styleManager);
        if (PenelopeSettings::self()->hyphenationEnabled()
            || PenelopeSettings::self()->hyphenateJustifiedText())
            contentBuilder.setHyphenator(m_hyphenator);
        if (PenelopeSettings::self()->shortWordsEnabled())
            contentBuilder.setShortWords(m_shortWords);
        contentBuilder.setFootnoteStyle(styleManager->footnoteStyle());
        Content::Document contentDoc = contentBuilder.build(markdown);
        view->applyLanguageOverrides(contentDoc);

        // Pre-layout to get page count for dialog
        m_fontManager->resetUsage();
        Layout::Engine preLayoutEngine(m_fontManager, m_textShaper);
        preLayoutEngine.setHyphenateJustifiedText(
            PenelopeSettings::self()->hyphenateJustifiedText());
        Layout::LayoutResult preLayout = preLayoutEngine.layout(contentDoc, pl);
        int pageCount = preLayout.pages.size();

        // Load saved options
        PdfExportOptions opts;
        auto *settings = PenelopeSettings::self();
        opts.author = settings->pdfAuthor();
        opts.textCopyMode = static_cast<PdfExportOptions::TextCopyMode>(
            settings->pdfTextCopyMode());
        opts.includeBookmarks = settings->pdfIncludeBookmarks();
        opts.bookmarkMaxDepth = settings->pdfBookmarkMaxDepth();
        opts.initialView = static_cast<PdfExportOptions::InitialView>(
            settings->pdfInitialView());
        opts.pageLayout = static_cast<PdfExportOptions::PageLayout>(
            settings->pdfPageLayout());

        // Overlay per-document options from MetadataStore
        QJsonObject perDoc = m_metadataStore->load(filePath);
        if (perDoc.contains(QStringLiteral("pdfExportOptions"))) {
            QJsonObject saved = perDoc[QStringLiteral("pdfExportOptions")].toObject();
            if (saved.contains(QStringLiteral("title")))
                opts.title = saved[QStringLiteral("title")].toString();
            if (saved.contains(QStringLiteral("author")))
                opts.author = saved[QStringLiteral("author")].toString();
            if (saved.contains(QStringLiteral("subject")))
                opts.subject = saved[QStringLiteral("subject")].toString();
            if (saved.contains(QStringLiteral("keywords")))
                opts.keywords = saved[QStringLiteral("keywords")].toString();
            if (saved.contains(QStringLiteral("pageRangeExpr")))
                opts.pageRangeExpr = saved[QStringLiteral("pageRangeExpr")].toString();
            if (saved.contains(QStringLiteral("excludedHeadingIndices"))) {
                QJsonArray arr = saved[QStringLiteral("excludedHeadingIndices")].toArray();
                for (const auto &v : arr)
                    opts.excludedHeadingIndices.insert(v.toInt());
            }
        }

        // Show export dialog
        PdfExportDialog dlg(contentDoc, pageCount, fi.baseName(), this);
        dlg.setOptions(opts);
        if (dlg.exec() != QDialog::Accepted)
            return;

        opts = dlg.options();

        // Save global defaults to KConfig
        settings->setPdfAuthor(opts.author);
        settings->setPdfTextCopyMode(static_cast<int>(opts.textCopyMode));
        settings->setPdfIncludeBookmarks(opts.includeBookmarks);
        settings->setPdfBookmarkMaxDepth(opts.bookmarkMaxDepth);
        settings->setPdfInitialView(static_cast<int>(opts.initialView));
        settings->setPdfPageLayout(static_cast<int>(opts.pageLayout));
        settings->save();

        // Save per-document options to MetadataStore
        QJsonObject docOpts;
        docOpts[QStringLiteral("title")] = opts.title;
        docOpts[QStringLiteral("author")] = opts.author;
        docOpts[QStringLiteral("subject")] = opts.subject;
        docOpts[QStringLiteral("keywords")] = opts.keywords;
        docOpts[QStringLiteral("pageRangeExpr")] = opts.pageRangeExpr;
        QJsonArray excludedArr;
        for (int idx : opts.excludedHeadingIndices)
            excludedArr.append(idx);
        docOpts[QStringLiteral("excludedHeadingIndices")] = excludedArr;
        m_metadataStore->setValue(filePath, QStringLiteral("pdfExportOptions"), docOpts);

        // File save dialog
        QString path = QFileDialog::getSaveFileName(
            this, i18n("Export as PDF"),
            QString(), i18n("PDF Files (*.pdf)"));
        if (path.isEmpty())
            return;

        // Filter content by excluded sections
        Content::Document filteredDoc = contentDoc;
        if (opts.sectionsModified && !opts.excludedHeadingIndices.isEmpty())
            filteredDoc = ContentFilter::filterSections(contentDoc, opts.excludedHeadingIndices);

        // Layout with filtered content
        m_fontManager->resetUsage();
        Layout::Engine layoutEngine(m_fontManager, m_textShaper);
        layoutEngine.setHyphenateJustifiedText(
            PenelopeSettings::self()->hyphenateJustifiedText());
        Layout::LayoutResult layoutResult = layoutEngine.layout(filteredDoc, pl);

        // Filter pages by range
        if (opts.pageRangeModified && !opts.pageRangeExpr.isEmpty()) {
            auto rangeResult = PageRangeParser::parse(opts.pageRangeExpr, layoutResult.pages.size());
            if (rangeResult.valid && rangeResult.pages.size() < layoutResult.pages.size()) {
                QList<Layout::Page> filteredPages;
                for (int i = 0; i < layoutResult.pages.size(); ++i) {
                    if (rangeResult.pages.contains(i + 1))  // 1-based
                        filteredPages.append(layoutResult.pages[i]);
                }
                layoutResult.pages = filteredPages;
                // Renumber pages
                for (int i = 0; i < layoutResult.pages.size(); ++i)
                    layoutResult.pages[i].pageNumber = i;
            }
        }

        // Generate PDF with options
        PdfGenerator pdfGen(m_fontManager);
        pdfGen.setMaxJustifyGap(settings->maxJustifyGap());
        pdfGen.setExportOptions(opts);
        if (pdfGen.generateToFile(layoutResult, pl, fi.baseName(), path)) {
            statusBar()->showMessage(i18n("Exported to %1", path), 3000);
        } else {
            statusBar()->showMessage(i18n("Failed to export PDF."), 3000);
        }
    }
```

**Step 3: Add includes to mainwindow.cpp if not already present**

Ensure these are at the top:
```cpp
#include "pdfexportdialog.h"
#include "contentfilter.h"
#include "pagerangeparser.h"
```

Also add `#include <QJsonArray>` if not present.

**Step 4: Build**

Run: `make -C build -j$(($(nproc)-1))`
Expected: builds clean

**Step 5: Commit**

```bash
git add src/app/mainwindow.cpp
git commit -m "Wire PDF export dialog into export pipeline with section/page filtering"
```

---

## Files Changed Summary

| File | Change |
|------|--------|
| `src/model/pdfexportoptions.h` | New: options struct |
| `src/model/pagerangeparser.h/cpp` | New: page range expression parser |
| `src/model/contentfilter.h/cpp` | New: section filtering by heading exclusion |
| `src/widgets/pdfexportdialog.h/cpp` | New: KPageDialog with 3 pages |
| `src/app/penelope.kcfg` | Add PdfExport settings group |
| `src/pdf/pdfgenerator.h/cpp` | Metadata, viewer prefs, bookmark depth filter |
| `src/app/mainwindow.cpp` | Wire dialog into export flow |
| `src/CMakeLists.txt` | Add new source files |

## Verification

1. `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make -C build -j$(($(nproc)-1))` — full build succeeds
2. Open a markdown file with headings → File → Export as PDF
3. Verify dialog appears with three pages (General, Content, Output)
4. Verify heading tree matches TOC panel
5. Test page range: `1-3`, `first`, `last`, `(last-2)-last`
6. Test section exclusion: uncheck a heading, verify PDF omits that section
7. Verify metadata in PDF properties (title, author, subject, keywords)
8. Verify bookmarks respect depth setting
9. Verify viewer preferences (open in Okular, check page layout mode)
10. Close and reopen dialog — verify settings are remembered
