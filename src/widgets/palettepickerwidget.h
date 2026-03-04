// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_PALETTEPICKERWIDGET_H
#define PENELOPE_PALETTEPICKERWIDGET_H

#include "resourcepickerwidget.h"

class PaletteManager;

class PalettePickerWidget : public ResourcePickerWidget
{
    Q_OBJECT

public:
    explicit PalettePickerWidget(PaletteManager *manager, QWidget *parent = nullptr);

protected:
    void populateGrid() override;
    QSize cellSize() const override { return {75, 52}; }

private:
    PaletteManager *m_manager = nullptr;
};

#endif // PENELOPE_PALETTEPICKERWIDGET_H
