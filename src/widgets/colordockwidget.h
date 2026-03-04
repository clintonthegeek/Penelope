// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_COLORDOCKWIDGET_H
#define PENELOPE_COLORDOCKWIDGET_H

#include <QHash>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class ColorSelectorWidget;
class ItemSelectorBar;
class PaletteManager;
class ThemeComposer;

class ColorDockWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ColorDockWidget(PaletteManager *paletteManager,
                             ThemeComposer *themeComposer,
                             QWidget *parent = nullptr);

    void setCurrentPaletteId(const QString &id);
    QString currentPaletteId() const;

Q_SIGNALS:
    void paletteChanged(const QString &id);

private Q_SLOTS:
    void onPaletteSelectionChanged(const QString &id);
    void onDuplicate();
    void onSave();
    void onDelete();
    void onRoleSelected();
    void onColorPickerChanged(const QColor &color);
    void onHexEdited();
    void onColorPreview(const QColor &color);

private:
    void buildUI();
    void populateSelector();
    void loadPaletteIntoTree(const QString &id);
    void updateRoleSwatch(const QString &role, const QColor &color);
    QString selectedRole() const;

    PaletteManager *m_paletteManager = nullptr;
    ThemeComposer *m_themeComposer = nullptr;

    ItemSelectorBar *m_selectorBar = nullptr;
    QTreeWidget *m_roleTree = nullptr;
    ColorSelectorWidget *m_colorSelector = nullptr;
    QLineEdit *m_hexEdit = nullptr;

    QHash<QString, QTreeWidgetItem *> m_roleItems;
    QHash<QString, QColor> m_workingColors;
};

#endif // PENELOPE_COLORDOCKWIDGET_H
