// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_TYPESETPICKERWIDGET_H
#define PENELOPE_TYPESETPICKERWIDGET_H

#include "resourcepickerwidget.h"

class TypeSetManager;

class TypeSetPickerWidget : public ResourcePickerWidget
{
    Q_OBJECT

public:
    explicit TypeSetPickerWidget(TypeSetManager *manager, QWidget *parent = nullptr);

protected:
    int gridColumns() const override { return 2; }
    void populateGrid() override;
    QSize cellSize() const override { return {120, 62}; }

private:
    TypeSetManager *m_manager = nullptr;
};

#endif // PENELOPE_TYPESETPICKERWIDGET_H
