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

    // Show/hide ink waste warning based on current palette
    void setHasNonWhiteBackgrounds(bool hasNonWhite);

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
    QCheckBox *m_markdownCopyCheck = nullptr;
    QCheckBox *m_unwrapParagraphsCheck = nullptr;
    QCheckBox *m_xobjectGlyphsCheck = nullptr;
    QCheckBox *m_hersheyFontsCheck = nullptr;
    KMessageWidget *m_inkWarning = nullptr;

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
