/*
 * pdfexportdialog.cpp — PDF export options dialog (KPageDialog)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pdfexportdialog.h"

#include "pagerangeparser.h"
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
    setFaceType(KPageDialog::List);
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

    m_markdownCopyCheck = new QCheckBox(i18n("Embed markdown syntax"), copyGroup);
    m_markdownCopyCheck->setToolTip(
        i18n("Hidden markdown characters (bold, italic, links, etc.) are embedded "
             "so that copying text from the PDF returns markdown source."));
    copyForm->addRow(m_markdownCopyCheck);

    m_unwrapParagraphsCheck = new QCheckBox(i18n("Unwrap paragraphs"), copyGroup);
    m_unwrapParagraphsCheck->setToolTip(
        i18n("Hidden text is embedded so that copying from the PDF returns "
             "paragraphs without soft line breaks."));
    copyForm->addRow(m_unwrapParagraphsCheck);

    m_xobjectGlyphsCheck = new QCheckBox(i18n("Render glyphs as vector art"), copyGroup);
    m_xobjectGlyphsCheck->setToolTip(
        i18n("Draws all font glyphs as reusable vector shapes instead of text operators. "
             "Produces smaller files and prevents visible text from interfering with "
             "markdown copy. Required when 'Embed markdown syntax' is enabled."));
    copyForm->addRow(m_xobjectGlyphsCheck);

    // Markdown copy requires xobject glyphs — force-check and disable
    connect(m_markdownCopyCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_xobjectGlyphsCheck->setChecked(true);
            m_xobjectGlyphsCheck->setEnabled(false);
        } else {
            m_xobjectGlyphsCheck->setEnabled(true);
        }
    });

    layout->addWidget(copyGroup);

    // Font rendering group
    auto *fontGroup = new QGroupBox(i18n("Font Rendering"), page);
    auto *fontForm = new QFormLayout(fontGroup);

    m_hersheyFontsCheck = new QCheckBox(i18n("Use Hershey stroke fonts"), fontGroup);
    m_hersheyFontsCheck->setToolTip(
        i18n("Replaces TTF/OTF fonts with Hershey vector stroke fonts at export time. "
             "Produces smaller files with a distinctive hand-drawn aesthetic."));
    fontForm->addRow(m_hersheyFontsCheck);

    layout->addWidget(fontGroup);

    // Ink waste warning (hidden by default)
    m_inkWarning = new KMessageWidget(page);
    m_inkWarning->setMessageType(KMessageWidget::Warning);
    m_inkWarning->setWordWrap(true);
    m_inkWarning->setText(
        i18n("This color palette uses non-white backgrounds that may waste ink when printed."));
    m_inkWarning->setCloseButtonVisible(true);
    m_inkWarning->setVisible(false);
    layout->addWidget(m_inkWarning);

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
        std::function<void(QTreeWidgetItem *)> setAll = [&](QTreeWidgetItem *item) {
            item->setCheckState(0, Qt::Checked);
            for (int i = 0; i < item->childCount(); ++i)
                setAll(item->child(i));
        };
        for (int i = 0; i < m_headingTree->topLevelItemCount(); ++i)
            setAll(m_headingTree->topLevelItem(i));
        m_updatingTree = false;
        onSectionCheckboxChanged();
    });
    connect(deselectAllBtn, &QPushButton::clicked, this, [this]() {
        m_updatingTree = true;
        std::function<void(QTreeWidgetItem *)> clearAll = [&](QTreeWidgetItem *item) {
            item->setCheckState(0, Qt::Unchecked);
            for (int i = 0; i < item->childCount(); ++i)
                clearAll(item->child(i));
        };
        for (int i = 0; i < m_headingTree->topLevelItemCount(); ++i)
            clearAll(m_headingTree->topLevelItem(i));
        m_updatingTree = false;
        onSectionCheckboxChanged();
    });

    layout->addWidget(sectionGroup);

    // Conflict warning (hidden by default)
    m_conflictWarning = new KMessageWidget(page);
    m_conflictWarning->setMessageType(KMessageWidget::Warning);
    m_conflictWarning->setWordWrap(true);
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
                else if constexpr (std::is_same_v<T, Content::Math>)
                    text += n.source;
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
    QString text = m_pageRangeEdit->text().trimmed();
    m_pageRangeModified = !text.isEmpty();
    buttonBox()->button(QDialogButtonBox::Ok)->setEnabled(
        PageRangeParser::parse(text, m_pageCount).valid);

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
    opts.markdownCopy = m_markdownCopyCheck->isChecked();
    opts.unwrapParagraphs = m_unwrapParagraphsCheck->isChecked();
    opts.xobjectGlyphs = m_xobjectGlyphsCheck->isChecked();
    opts.useHersheyFonts = m_hersheyFontsCheck->isChecked();

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
    m_markdownCopyCheck->setChecked(opts.markdownCopy);
    m_unwrapParagraphsCheck->setChecked(opts.unwrapParagraphs);
    m_xobjectGlyphsCheck->setChecked(opts.xobjectGlyphs || opts.markdownCopy);
    m_xobjectGlyphsCheck->setEnabled(!opts.markdownCopy);
    m_hersheyFontsCheck->setChecked(opts.useHersheyFonts);

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

void PdfExportDialog::setHasNonWhiteBackgrounds(bool hasNonWhite)
{
    if (hasNonWhite && !m_inkWarning->isVisible())
        m_inkWarning->animatedShow();
    else if (!hasNonWhite && m_inkWarning->isVisible())
        m_inkWarning->animatedHide();
}
