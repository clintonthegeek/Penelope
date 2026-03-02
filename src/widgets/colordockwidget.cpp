// SPDX-License-Identifier: GPL-2.0-or-later

#include "colordockwidget.h"
#include "colorselectorwidget.h"
#include "colorpalette.h"
#include "itemselectorbar.h"
#include "palettemanager.h"
#include "themecomposer.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Static color-role definitions
// ---------------------------------------------------------------------------

namespace {

struct RoleDef {
    const char *key;
    const char *name;
    const char *group;
};

// Order determines display order in the tree.
const RoleDef s_roles[] = {
    // Text
    {"text",              QT_TR_NOOP("Text"),             QT_TR_NOOP("Text Colors")},
    {"headingText",       QT_TR_NOOP("Heading"),          QT_TR_NOOP("Text Colors")},
    {"blockquoteText",    QT_TR_NOOP("Blockquote"),       QT_TR_NOOP("Text Colors")},
    {"linkText",          QT_TR_NOOP("Link"),             QT_TR_NOOP("Text Colors")},
    {"codeText",          QT_TR_NOOP("Code"),             QT_TR_NOOP("Text Colors")},
    // Surfaces
    {"pageBackground",    QT_TR_NOOP("Page Background"),  QT_TR_NOOP("Surface Colors")},
    {"surfaceCode",       QT_TR_NOOP("Code Block"),       QT_TR_NOOP("Surface Colors")},
    {"surfaceInlineCode", QT_TR_NOOP("Inline Code"),      QT_TR_NOOP("Surface Colors")},
    {"surfaceTableHeader",QT_TR_NOOP("Table Header"),     QT_TR_NOOP("Surface Colors")},
    {"surfaceTableAlt",   QT_TR_NOOP("Table Alt Row"),    QT_TR_NOOP("Surface Colors")},
    // Borders
    {"borderOuter",       QT_TR_NOOP("Outer"),            QT_TR_NOOP("Border Colors")},
    {"borderInner",       QT_TR_NOOP("Inner"),            QT_TR_NOOP("Border Colors")},
    {"borderHeaderBottom",QT_TR_NOOP("Header Bottom"),    QT_TR_NOOP("Border Colors")},
};

QIcon swatchIcon(const QColor &color)
{
    QPixmap pm(16, 16);
    pm.fill(color);
    QPainter p(&pm);
    p.setPen(Qt::darkGray);
    p.drawRect(0, 0, 15, 15);
    return QIcon(pm);
}

} // anonymous namespace

// ===========================================================================
// Construction
// ===========================================================================

ColorDockWidget::ColorDockWidget(PaletteManager *paletteManager,
                                 ThemeComposer *themeComposer,
                                 QWidget *parent)
    : QWidget(parent)
    , m_paletteManager(paletteManager)
    , m_themeComposer(themeComposer)
{
    buildUI();
    populateSelector();

    connect(m_paletteManager, &PaletteManager::palettesChanged,
            this, &ColorDockWidget::populateSelector);
}

// ===========================================================================
// UI construction
// ===========================================================================

void ColorDockWidget::buildUI()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    outerLayout->setSpacing(8);

    // --- Palette Selector ---
    m_selectorBar = new ItemSelectorBar(this);
    outerLayout->addWidget(m_selectorBar);

    connect(m_selectorBar, &ItemSelectorBar::currentItemChanged,
            this, &ColorDockWidget::onPaletteSelectionChanged);
    connect(m_selectorBar, &ItemSelectorBar::duplicateRequested,
            this, &ColorDockWidget::onDuplicate);
    connect(m_selectorBar, &ItemSelectorBar::saveRequested,
            this, &ColorDockWidget::onSave);
    connect(m_selectorBar, &ItemSelectorBar::deleteRequested,
            this, &ColorDockWidget::onDelete);

    // --- Color role tree (fills remaining space) ---
    m_roleTree = new QTreeWidget;
    m_roleTree->setHeaderHidden(true);
    m_roleTree->setRootIsDecorated(true);
    m_roleTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roleTree->setIndentation(16);

    QHash<QString, QTreeWidgetItem *> groups;
    for (const auto &role : s_roles) {
        QTreeWidgetItem *groupItem = groups.value(QLatin1String(role.group));
        if (!groupItem) {
            groupItem = new QTreeWidgetItem(m_roleTree);
            groupItem->setText(0, tr(role.group));
            groupItem->setFlags(Qt::ItemIsEnabled); // not selectable
            groupItem->setExpanded(true);
            groups.insert(QLatin1String(role.group), groupItem);
        }

        auto *item = new QTreeWidgetItem(groupItem);
        item->setText(0, tr(role.name));
        item->setData(0, Qt::UserRole, QLatin1String(role.key));
        item->setIcon(0, swatchIcon(Qt::gray));
        m_roleItems.insert(QLatin1String(role.key), item);
    }

    outerLayout->addWidget(m_roleTree, 1);

    // --- Color selector (square, full dock width) ---
    m_colorSelector = new ColorSelectorWidget;
    m_colorSelector->setEnabled(false);
    outerLayout->addWidget(m_colorSelector, 0);

    // --- Hex color row ---
    auto *hexRow = new QHBoxLayout;
    hexRow->setSpacing(4);

    auto *hashLabel = new QLabel(QStringLiteral("#"), this);
    hexRow->addWidget(hashLabel);

    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setMaxLength(6);
    m_hexEdit->setPlaceholderText(QStringLiteral("RRGGBB"));
    m_hexEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,6}")), m_hexEdit));
    m_hexEdit->setEnabled(false);
    hexRow->addWidget(m_hexEdit);

    auto *copyBtn = new QToolButton(this);
    copyBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copyBtn->setToolTip(tr("Copy hex color"));
    hexRow->addWidget(copyBtn);

    outerLayout->addLayout(hexRow);

    // --- Connections ---
    connect(m_roleTree, &QTreeWidget::currentItemChanged,
            this, &ColorDockWidget::onRoleSelected);
    connect(m_colorSelector, &ColorSelectorWidget::colorChanged,
            this, &ColorDockWidget::onColorPickerChanged);
    connect(m_colorSelector, &ColorSelectorWidget::colorPreview,
            this, &ColorDockWidget::onColorPreview);
    connect(m_hexEdit, &QLineEdit::editingFinished,
            this, &ColorDockWidget::onHexEdited);
    connect(copyBtn, &QToolButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(
            QStringLiteral("#") + m_hexEdit->text().toUpper());
    });
}

// ===========================================================================
// Palette selector population
// ===========================================================================

void ColorDockWidget::populateSelector()
{
    const QStringList ids = m_paletteManager->availablePalettes();
    QStringList names;
    QStringList builtinIds;
    for (const QString &id : ids) {
        names.append(m_paletteManager->paletteName(id));
        if (m_paletteManager->isBuiltin(id))
            builtinIds.append(id);
    }
    m_selectorBar->setItems(ids, names, builtinIds);
}

// ===========================================================================
// Public API
// ===========================================================================

void ColorDockWidget::setCurrentPaletteId(const QString &id)
{
    m_selectorBar->setCurrentId(id);
    loadPaletteIntoTree(id);
}

QString ColorDockWidget::currentPaletteId() const
{
    return m_selectorBar->currentId();
}

// ===========================================================================
// Palette selection
// ===========================================================================

void ColorDockWidget::onPaletteSelectionChanged(const QString &id)
{
    loadPaletteIntoTree(id);

    ColorPalette pal = m_paletteManager->palette(id);
    if (!pal.id.isEmpty()) {
        m_themeComposer->setColorPalette(pal);
        Q_EMIT paletteChanged(id);
    }
}

void ColorDockWidget::loadPaletteIntoTree(const QString &id)
{
    ColorPalette pal = m_paletteManager->palette(id);
    if (pal.id.isEmpty())
        return;

    m_workingColors = pal.colors;

    // Update all swatches
    for (auto it = m_roleItems.constBegin(); it != m_roleItems.constEnd(); ++it) {
        const QColor c = m_workingColors.value(it.key(), Qt::gray);
        it.value()->setIcon(0, swatchIcon(c));
    }

    const bool hasRole = !selectedRole().isEmpty();
    m_colorSelector->setEnabled(hasRole);
    m_hexEdit->setEnabled(hasRole);

    // Refresh color selector to show the currently-selected role
    onRoleSelected();
}

// ===========================================================================
// Role selection
// ===========================================================================

void ColorDockWidget::onRoleSelected()
{
    const QString role = selectedRole();
    if (role.isEmpty()) {
        m_colorSelector->setEnabled(false);
        m_hexEdit->setEnabled(false);
        const QSignalBlocker hb(m_hexEdit);
        m_hexEdit->clear();
        return;
    }

    m_colorSelector->setEnabled(true);
    m_hexEdit->setEnabled(true);

    const QColor c = m_workingColors.value(role, Qt::gray);
    const QSignalBlocker blocker(m_colorSelector);
    m_colorSelector->setColor(c);

    const QSignalBlocker hb(m_hexEdit);
    m_hexEdit->setText(c.name().mid(1).toUpper());
}

QString ColorDockWidget::selectedRole() const
{
    auto *item = m_roleTree->currentItem();
    if (!item)
        return {};
    return item->data(0, Qt::UserRole).toString();
}

// ===========================================================================
// Live color editing
// ===========================================================================

void ColorDockWidget::onColorPickerChanged(const QColor &color)
{
    const QString role = selectedRole();
    if (role.isEmpty())
        return;

    QString paletteId = m_selectorBar->currentId();
    if (paletteId.isEmpty())
        return;

    // Auto-duplicate built-in palettes on first edit
    if (m_paletteManager->isBuiltin(paletteId)) {
        ColorPalette pal = m_paletteManager->palette(paletteId);
        pal.id.clear();
        pal.name = tr("Copy of %1").arg(pal.name);
        pal.colors = m_workingColors;
        paletteId = m_paletteManager->savePalette(pal);
        m_selectorBar->setCurrentId(paletteId);
    }

    // Update working copy and tree swatch
    m_workingColors.insert(role, color);
    updateRoleSwatch(role, color);

    // Update hex field
    const QSignalBlocker hb(m_hexEdit);
    m_hexEdit->setText(color.name().mid(1).toUpper());

    // Push to composer for instant preview
    ColorPalette pal = m_paletteManager->palette(paletteId);
    pal.colors = m_workingColors;
    m_themeComposer->setColorPalette(pal);
    Q_EMIT paletteChanged(paletteId);
}

void ColorDockWidget::updateRoleSwatch(const QString &role, const QColor &color)
{
    auto *item = m_roleItems.value(role);
    if (item)
        item->setIcon(0, swatchIcon(color));
}

// ===========================================================================
// Hex input
// ===========================================================================

void ColorDockWidget::onColorPreview(const QColor &color)
{
    const QSignalBlocker hb(m_hexEdit);
    m_hexEdit->setText(color.name().mid(1).toUpper());
}

void ColorDockWidget::onHexEdited()
{
    const QString text = m_hexEdit->text();
    if (text.length() != 6)
        return;

    const QColor color(QStringLiteral("#") + text);
    if (!color.isValid())
        return;

    const QSignalBlocker blocker(m_colorSelector);
    m_colorSelector->setColor(color);

    onColorPickerChanged(color);
}

// ===========================================================================
// Duplicate / Save / Delete
// ===========================================================================

void ColorDockWidget::onDuplicate()
{
    const QString srcId = m_selectorBar->currentId();
    ColorPalette pal = m_paletteManager->palette(srcId);
    if (pal.id.isEmpty())
        return;

    pal.id.clear();
    pal.name = tr("Copy of %1").arg(pal.name);
    pal.colors = m_workingColors;
    const QString newId = m_paletteManager->savePalette(pal);
    m_selectorBar->setCurrentId(newId);
    loadPaletteIntoTree(newId);
}

void ColorDockWidget::onSave()
{
    const QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_paletteManager->isBuiltin(id))
        return;

    ColorPalette pal = m_paletteManager->palette(id);
    pal.colors = m_workingColors;
    m_paletteManager->savePalette(pal);

    m_themeComposer->setColorPalette(pal);
    Q_EMIT paletteChanged(id);
}

void ColorDockWidget::onDelete()
{
    const QString id = m_selectorBar->currentId();
    if (id.isEmpty() || m_paletteManager->isBuiltin(id))
        return;

    int ret = QMessageBox::question(this, tr("Delete Palette"),
                                    tr("Delete \"%1\"?").arg(m_paletteManager->paletteName(id)),
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    m_paletteManager->deletePalette(id);
    const QStringList ids = m_paletteManager->availablePalettes();
    if (!ids.isEmpty()) {
        m_selectorBar->setCurrentId(ids.first());
        onPaletteSelectionChanged(ids.first());
    }
}
