// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_TYPEDOCKWIDGET_H
#define PENELOPE_TYPEDOCKWIDGET_H

#include <QWidget>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QStackedWidget;
class QToolBox;
class QTreeView;
class FootnoteConfigWidget;
class ItemSelectorBar;
class StyleManager;
class StylePropertiesEditor;
class StyleTreeModel;
class TableStylePropertiesEditor;
class ThemeComposer;
class TypeSetManager;

class TypeDockWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TypeDockWidget(TypeSetManager *typeSetManager,
                            ThemeComposer *themeComposer,
                            QWidget *parent = nullptr);

    StyleManager *currentStyleManager() const;
    void populateFromStyleManager(StyleManager *sm);
    void refreshTreeModel();

    void setCurrentTypeSetId(const QString &id);
    QString currentTypeSetId() const;

Q_SIGNALS:
    void styleOverrideChanged();
    void typeSetChanged(const QString &id);

private Q_SLOTS:
    void onStylePropertyChanged();
    void onTableStylePropertyChanged();
    void onFootnoteStyleChanged();
    void onTreeSelectionChanged();

    void onTypeSetSelectionChanged(const QString &id);
    void onDuplicateTypeSet();
    void onSaveTypeSet();
    void onDeleteTypeSet();
    void onFontRoleEdited();

private:
    void buildUI();
    void loadSelectedStyle();
    void loadSelectedTableStyle();
    void populateSelector();
    void loadTypeSetIntoFontCombos(const QString &id);
    void setFontCombosEnabled(bool enabled);

    TypeSetManager *m_typeSetManager = nullptr;
    ThemeComposer *m_themeComposer = nullptr;
    StyleManager *m_editingStyles = nullptr;

    // Type set selector
    ItemSelectorBar *m_selectorBar = nullptr;

    // Tool box (Quick Settings / Styles)
    QToolBox *m_toolBox = nullptr;
    QFontComboBox *m_bodyFontCombo = nullptr;
    QComboBox *m_bodyHersheyCombo = nullptr;
    QFontComboBox *m_headingFontCombo = nullptr;
    QComboBox *m_headingHersheyCombo = nullptr;
    QFontComboBox *m_monoFontCombo = nullptr;
    QComboBox *m_monoHersheyCombo = nullptr;

    // Style tree + editor
    QCheckBox *m_showPreviewsCheck = nullptr;
    QTreeView *m_styleTree = nullptr;
    StyleTreeModel *m_treeModel = nullptr;
    QStackedWidget *m_editorStack = nullptr;
    StylePropertiesEditor *m_propsEditor = nullptr;
    TableStylePropertiesEditor *m_tablePropsEditor = nullptr;
    FootnoteConfigWidget *m_footnoteConfig = nullptr;

    QString m_selectedStyleName;
    bool m_selectedIsParagraph = true;
    bool m_selectedIsTable = false;
    bool m_selectedIsFootnote = false;
};

#endif // PENELOPE_TYPEDOCKWIDGET_H
