// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_RESOURCEPICKERWIDGET_H
#define PENELOPE_RESOURCEPICKERWIDGET_H

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QWidget>

class QGridLayout;

// ---------------------------------------------------------------------------
// ResourcePickerCellBase — base for grid-picker cells with selection border
// ---------------------------------------------------------------------------

class ResourcePickerCellBase : public QWidget
{
    Q_OBJECT

public:
    ResourcePickerCellBase(const QString &id, bool selected, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_cellId(id)
        , m_selected(selected)
    {
        setCursor(Qt::PointingHandCursor);
    }

    void setSelected(bool selected)
    {
        if (m_selected != selected) {
            m_selected = selected;
            update();
        }
    }

    QString cellId() const { return m_cellId; }

Q_SIGNALS:
    void clicked(const QString &id);
    void doubleClicked(const QString &id);

protected:
    void drawSelectionBorder(QPainter &p)
    {
        const QRect r = rect();
        if (m_selected) {
            QPen pen(palette().color(QPalette::Highlight), 2);
            p.setPen(pen);
            p.drawRect(r.adjusted(1, 1, -1, -1));
        } else {
            QPen pen(palette().color(QPalette::Mid), 1);
            p.setPen(pen);
            p.drawRect(r.adjusted(0, 0, -1, -1));
        }
    }

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

    QString m_cellId;
    bool m_selected = false;
};

// ---------------------------------------------------------------------------
// ResourcePickerWidget — base for grid-based resource pickers
// ---------------------------------------------------------------------------

class ResourcePickerWidget : public QWidget
{
    Q_OBJECT

public:
    void setCurrentId(const QString &id);
    void refresh();

Q_SIGNALS:
    void resourceSelected(const QString &id);
    void resourceDoubleClicked(const QString &id);

protected:
    explicit ResourcePickerWidget(const QString &headerText, QWidget *parent = nullptr);

    void rebuildGrid();

    /// Override to return the number of grid columns (default 3).
    virtual int gridColumns() const { return 3; }

    /// Override to populate the grid with cells. Called after the grid is cleared.
    virtual void populateGrid() = 0;

    /// Override to return the fixed cell size for minimum-width calculation.
    virtual QSize cellSize() const = 0;

    /// Helper: create a cell, connect its clicked signal, and add it to the grid.
    void addCell(ResourcePickerCellBase *cell);

    QString m_currentId;
    QGridLayout *m_gridLayout = nullptr;

private:
    int m_row = 0;
    int m_col = 0;
    QWidget *m_gridContainer = nullptr;
};

#endif // PENELOPE_RESOURCEPICKERWIDGET_H
