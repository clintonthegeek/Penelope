// SPDX-License-Identifier: GPL-2.0-or-later

#include "typedockwidget.h"
#include "itemselectorbar.h"
#include "footnoteconfigwidget.h"
#include "stylepropertieseditor.h"
#include "tablestylepropertieseditor.h"
#include "styletreemodel.h"
#include "characterstyle.h"
#include "paragraphstyle.h"
#include "stylemanager.h"
#include "tablestyle.h"
#include "typeset.h"
#include "typesetmanager.h"
#include "themecomposer.h"
#include "hersheyfont.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QStackedWidget>
#include <QToolBox>
#include <QTreeView>
#include <QVBoxLayout>

TypeDockWidget::TypeDockWidget(TypeSetManager *typeSetManager,
                               ThemeComposer *themeComposer,
                               QWidget *parent)
    : QWidget(parent)
    , m_typeSetManager(typeSetManager)
    , m_themeComposer(themeComposer)
{
    buildUI();
    populateSelector();

    connect(m_typeSetManager, &TypeSetManager::typeSetsChanged,
            this, &TypeDockWidget::populateSelector);
}

void TypeDockWidget::buildUI()
{
    HersheyFontRegistry::instance().ensureLoaded();
    const QStringList hersheyFamilies = HersheyFontRegistry::instance().familyNames();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // --- Type Set Selector ---
    m_selectorBar = new ItemSelectorBar(this);
    layout->addWidget(m_selectorBar);

    connect(m_selectorBar, &ItemSelectorBar::currentItemChanged,
            this, &TypeDockWidget::onTypeSetSelectionChanged);
    connect(m_selectorBar, &ItemSelectorBar::duplicateRequested,
            this, &TypeDockWidget::onDuplicateTypeSet);
    connect(m_selectorBar, &ItemSelectorBar::saveRequested,
            this, &TypeDockWidget::onSaveTypeSet);
    connect(m_selectorBar, &ItemSelectorBar::deleteRequested,
            this, &TypeDockWidget::onDeleteTypeSet);

    // --- Tool Box ---
    m_toolBox = new QToolBox;
    layout->addWidget(m_toolBox, 1);

    // --- Page 0: Quick Settings ---
    auto *quickPage = new QWidget;
    auto *quickLayout = new QVBoxLayout(quickPage);
    quickLayout->setContentsMargins(0, 0, 0, 0);

    auto createFontRow = [&](const QString &label,
                             QFontComboBox *&fontCombo,
                             QComboBox *&hersheyCombo,
                             QFontComboBox::FontFilters filters = QFontComboBox::AllFonts) {
        auto *rowLayout = new QHBoxLayout;
        auto *lbl = new QLabel(label);
        lbl->setFixedWidth(55);
        rowLayout->addWidget(lbl);

        fontCombo = new QFontComboBox;
        fontCombo->setFontFilters(filters);
        rowLayout->addWidget(fontCombo, 1);

        hersheyCombo = new QComboBox;
        hersheyCombo->addItems(hersheyFamilies);
        rowLayout->addWidget(hersheyCombo, 1);

        quickLayout->addLayout(rowLayout);

        connect(fontCombo, &QFontComboBox::currentFontChanged,
                this, &TypeDockWidget::onFontRoleEdited);
        connect(hersheyCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &TypeDockWidget::onFontRoleEdited);
    };

    createFontRow(tr("Body:"), m_bodyFontCombo, m_bodyHersheyCombo);
    createFontRow(tr("Heading:"), m_headingFontCombo, m_headingHersheyCombo);
    createFontRow(tr("Mono:"), m_monoFontCombo, m_monoHersheyCombo,
                  QFontComboBox::MonospacedFonts);

    quickLayout->addStretch();
    m_toolBox->addItem(quickPage, tr("Quick Settings"));

    // --- Page 1: Styles ---
    auto *stylesPage = new QWidget;
    auto *stylesLayout = new QVBoxLayout(stylesPage);
    stylesLayout->setContentsMargins(0, 0, 0, 0);

    m_showPreviewsCheck = new QCheckBox(tr("Show previews"));
    m_showPreviewsCheck->setChecked(true);
    stylesLayout->addWidget(m_showPreviewsCheck);
    connect(m_showPreviewsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_treeModel->setShowPreviews(checked);
    });

    m_treeModel = new StyleTreeModel(this);
    m_treeModel->setShowPreviews(true);
    m_styleTree = new QTreeView;
    m_styleTree->setModel(m_treeModel);
    m_styleTree->setHeaderHidden(true);
    m_styleTree->setRootIsDecorated(true);
    m_styleTree->setExpandsOnDoubleClick(true);
    m_styleTree->setMinimumHeight(120);
    stylesLayout->addWidget(m_styleTree, 1);

    connect(m_styleTree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this]() { onTreeSelectionChanged(); });

    // Properties Editor (stacked: paragraph/char, table, footnotes)
    m_propsEditor = new StylePropertiesEditor;
    m_tablePropsEditor = new TableStylePropertiesEditor;
    m_footnoteConfig = new FootnoteConfigWidget;

    m_editorStack = new QStackedWidget;
    m_editorStack->addWidget(m_propsEditor);          // index 0
    m_editorStack->addWidget(m_tablePropsEditor);      // index 1
    m_editorStack->addWidget(m_footnoteConfig);        // index 2
    m_editorStack->hide();
    stylesLayout->addWidget(m_editorStack, 1);

    m_toolBox->addItem(stylesPage, tr("Styles"));
    m_toolBox->setCurrentIndex(1); // Styles open by default

    connect(m_propsEditor, &StylePropertiesEditor::propertyChanged,
            this, &TypeDockWidget::onStylePropertyChanged);
    connect(m_tablePropsEditor, &TableStylePropertiesEditor::propertyChanged,
            this, &TypeDockWidget::onTableStylePropertyChanged);
    connect(m_footnoteConfig, &FootnoteConfigWidget::footnoteStyleChanged,
            this, &TypeDockWidget::onFootnoteStyleChanged);
}

// --- Type Set selector slots ---

void TypeDockWidget::populateSelector()
{
    const QStringList ids = m_typeSetManager->availableTypeSets();
    QStringList names;
    QStringList builtinIds;
    for (const QString &id : ids) {
        names.append(m_typeSetManager->typeSetName(id));
        if (m_typeSetManager->isBuiltin(id))
            builtinIds.append(id);
    }
    m_selectorBar->setItems(ids, names, builtinIds);
}

void TypeDockWidget::setCurrentTypeSetId(const QString &id)
{
    m_selectorBar->setCurrentId(id);
    loadTypeSetIntoFontCombos(id);
}

QString TypeDockWidget::currentTypeSetId() const
{
    return m_selectorBar->currentId();
}

void TypeDockWidget::onTypeSetSelectionChanged(const QString &id)
{
    loadTypeSetIntoFontCombos(id);

    // Apply to ThemeComposer
    TypeSet ts = m_typeSetManager->typeSet(id);
    if (!ts.id.isEmpty()) {
        m_themeComposer->setTypeSet(ts);
        Q_EMIT typeSetChanged(id);
    }
}

void TypeDockWidget::loadTypeSetIntoFontCombos(const QString &id)
{
    TypeSet ts = m_typeSetManager->typeSet(id);
    if (ts.id.isEmpty())
        return;

    // Block signals while populating
    const QSignalBlocker b1(m_bodyFontCombo);
    const QSignalBlocker b2(m_bodyHersheyCombo);
    const QSignalBlocker b3(m_headingFontCombo);
    const QSignalBlocker b4(m_headingHersheyCombo);
    const QSignalBlocker b5(m_monoFontCombo);
    const QSignalBlocker b6(m_monoHersheyCombo);

    m_bodyFontCombo->setCurrentFont(QFont(ts.body.family));
    int idx = m_bodyHersheyCombo->findText(ts.body.hersheyFamily);
    if (idx >= 0) m_bodyHersheyCombo->setCurrentIndex(idx);

    m_headingFontCombo->setCurrentFont(QFont(ts.heading.family));
    idx = m_headingHersheyCombo->findText(ts.heading.hersheyFamily);
    if (idx >= 0) m_headingHersheyCombo->setCurrentIndex(idx);

    m_monoFontCombo->setCurrentFont(QFont(ts.mono.family));
    idx = m_monoHersheyCombo->findText(ts.mono.hersheyFamily);
    if (idx >= 0) m_monoHersheyCombo->setCurrentIndex(idx);

    setFontCombosEnabled(!m_typeSetManager->isBuiltin(id));
}

void TypeDockWidget::setFontCombosEnabled(bool enabled)
{
    m_bodyFontCombo->setEnabled(enabled);
    m_bodyHersheyCombo->setEnabled(enabled);
    m_headingFontCombo->setEnabled(enabled);
    m_headingHersheyCombo->setEnabled(enabled);
    m_monoFontCombo->setEnabled(enabled);
    m_monoHersheyCombo->setEnabled(enabled);
}

void TypeDockWidget::onDuplicateTypeSet()
{
    QString srcId = m_selectorBar->currentId();
    TypeSet ts = m_typeSetManager->typeSet(srcId);
    if (ts.id.isEmpty())
        return;

    ts.id.clear();
    ts.name = tr("Copy of %1").arg(ts.name);
    QString newId = m_typeSetManager->saveTypeSet(ts);
    m_selectorBar->setCurrentId(newId);
    loadTypeSetIntoFontCombos(newId);
}

void TypeDockWidget::onSaveTypeSet()
{
    QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_typeSetManager->isBuiltin(id))
        return;

    TypeSet ts = m_typeSetManager->typeSet(id);
    ts.body.family = m_bodyFontCombo->currentFont().family();
    ts.body.hersheyFamily = m_bodyHersheyCombo->currentText();
    ts.heading.family = m_headingFontCombo->currentFont().family();
    ts.heading.hersheyFamily = m_headingHersheyCombo->currentText();
    ts.mono.family = m_monoFontCombo->currentFont().family();
    ts.mono.hersheyFamily = m_monoHersheyCombo->currentText();

    m_typeSetManager->saveTypeSet(ts);

    // Update ThemeComposer
    m_themeComposer->setTypeSet(ts);
    Q_EMIT typeSetChanged(id);
}

void TypeDockWidget::onDeleteTypeSet()
{
    QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_typeSetManager->isBuiltin(id))
        return;

    int ret = QMessageBox::question(this, tr("Delete Type Set"),
                                    tr("Delete \"%1\"?").arg(m_typeSetManager->typeSetName(id)),
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    m_typeSetManager->deleteTypeSet(id);
    // Selector repopulates via typeSetsChanged signal; select first
    const QStringList ids = m_typeSetManager->availableTypeSets();
    if (!ids.isEmpty()) {
        m_selectorBar->setCurrentId(ids.first());
        onTypeSetSelectionChanged(ids.first());
    }
}

void TypeDockWidget::onFontRoleEdited()
{
    QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_typeSetManager->isBuiltin(id))
        return;

    // Build a TypeSet from current combos and push to composer
    TypeSet ts = m_typeSetManager->typeSet(id);
    ts.body.family = m_bodyFontCombo->currentFont().family();
    ts.body.hersheyFamily = m_bodyHersheyCombo->currentText();
    ts.heading.family = m_headingFontCombo->currentFont().family();
    ts.heading.hersheyFamily = m_headingHersheyCombo->currentText();
    ts.mono.family = m_monoFontCombo->currentFont().family();
    ts.mono.hersheyFamily = m_monoHersheyCombo->currentText();

    m_themeComposer->setTypeSet(ts);
    Q_EMIT typeSetChanged(id);
}

// --- Style tree methods (preserved from StyleDockWidget) ---

StyleManager *TypeDockWidget::currentStyleManager() const
{
    return m_editingStyles;
}

void TypeDockWidget::populateFromStyleManager(StyleManager *sm)
{
    delete m_editingStyles;
    m_editingStyles = sm->clone(const_cast<TypeDockWidget *>(this));

    m_treeModel->setStyleManager(m_editingStyles);
    m_styleTree->expandAll();

    m_propsEditor->clear();
    m_tablePropsEditor->clear();
    m_selectedStyleName.clear();
    m_selectedIsTable = false;
    m_selectedIsFootnote = false;
    m_editorStack->hide();

    m_footnoteConfig->loadFootnoteStyle(m_editingStyles->footnoteStyle());
}

void TypeDockWidget::refreshTreeModel()
{
    if (!m_editingStyles)
        return;
    m_treeModel->setStyleManager(m_editingStyles);
    if (m_showPreviewsCheck->isChecked())
        m_treeModel->refresh();
}

void TypeDockWidget::onTreeSelectionChanged()
{
    QModelIndex current = m_styleTree->currentIndex();
    if (!current.isValid() || m_treeModel->isCategoryNode(current)) {
        m_propsEditor->clear();
        m_tablePropsEditor->clear();
        m_selectedStyleName.clear();
        m_selectedIsTable = false;
        m_selectedIsFootnote = false;
        m_editorStack->hide();
        return;
    }

    m_selectedIsFootnote = m_treeModel->isFootnoteNode(current);
    if (m_selectedIsFootnote) {
        m_selectedStyleName.clear();
        m_selectedIsTable = false;
        m_editorStack->setCurrentIndex(2);
        m_editorStack->show();
        return;
    }

    m_selectedStyleName = m_treeModel->styleName(current);
    m_selectedIsParagraph = m_treeModel->isParagraphStyle(current);
    m_selectedIsTable = m_treeModel->isTableStyle(current);

    if (m_selectedIsTable) {
        m_editorStack->setCurrentIndex(1);
        loadSelectedTableStyle();
    } else {
        m_editorStack->setCurrentIndex(0);
        loadSelectedStyle();
    }
    m_editorStack->show();
}

void TypeDockWidget::loadSelectedStyle()
{
    if (m_selectedStyleName.isEmpty() || !m_editingStyles)
        return;

    if (m_selectedIsParagraph) {
        ParagraphStyle *style = m_editingStyles->paragraphStyle(m_selectedStyleName);
        if (!style) return;
        ParagraphStyle resolved = m_editingStyles->resolvedParagraphStyle(m_selectedStyleName);
        QStringList parents = m_editingStyles->paragraphStyleNames();
        parents.removeAll(m_selectedStyleName);
        m_propsEditor->loadParagraphStyle(*style, resolved, parents);
    } else {
        CharacterStyle *style = m_editingStyles->characterStyle(m_selectedStyleName);
        if (!style) return;
        CharacterStyle resolved = m_editingStyles->resolvedCharacterStyle(m_selectedStyleName);
        QStringList parents = m_editingStyles->characterStyleNames();
        parents.removeAll(m_selectedStyleName);
        m_propsEditor->loadCharacterStyle(*style, resolved, parents);
    }
}

void TypeDockWidget::onStylePropertyChanged()
{
    if (m_selectedStyleName.isEmpty() || !m_editingStyles)
        return;

    if (m_selectedIsParagraph) {
        ParagraphStyle *existing = m_editingStyles->paragraphStyle(m_selectedStyleName);
        if (!existing) return;
        ParagraphStyle fresh(m_selectedStyleName);
        fresh.setHeadingLevel(existing->headingLevel());
        m_propsEditor->applyToParagraphStyle(fresh);
        m_editingStyles->addParagraphStyle(fresh);
    } else {
        CharacterStyle fresh(m_selectedStyleName);
        m_propsEditor->applyToCharacterStyle(fresh);
        m_editingStyles->addCharacterStyle(fresh);
    }

    if (m_showPreviewsCheck->isChecked())
        m_treeModel->refresh();

    Q_EMIT styleOverrideChanged();
}

void TypeDockWidget::loadSelectedTableStyle()
{
    if (m_selectedStyleName.isEmpty() || !m_editingStyles)
        return;

    TableStyle *ts = m_editingStyles->tableStyle(m_selectedStyleName);
    if (!ts)
        return;

    QStringList paraNames = m_editingStyles->paragraphStyleNames();
    m_tablePropsEditor->loadTableStyle(*ts, paraNames);
}

void TypeDockWidget::onTableStylePropertyChanged()
{
    if (m_selectedStyleName.isEmpty() || !m_editingStyles)
        return;

    TableStyle fresh(m_selectedStyleName);
    m_tablePropsEditor->applyToTableStyle(fresh);
    m_editingStyles->addTableStyle(fresh);

    Q_EMIT styleOverrideChanged();
}

void TypeDockWidget::onFootnoteStyleChanged()
{
    if (!m_editingStyles)
        return;

    m_editingStyles->setFootnoteStyle(m_footnoteConfig->currentFootnoteStyle());
    Q_EMIT styleOverrideChanged();
}
