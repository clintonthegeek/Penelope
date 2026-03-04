// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_PAGETEMPLATEPICKERWIDGET_H
#define PENELOPE_PAGETEMPLATEPICKERWIDGET_H

#include "resourcepickerwidget.h"

class PageTemplateManager;

class PageTemplatePickerWidget : public ResourcePickerWidget
{
    Q_OBJECT

public:
    explicit PageTemplatePickerWidget(PageTemplateManager *manager, QWidget *parent = nullptr);

protected:
    int gridColumns() const override { return 2; }
    void populateGrid() override;
    QSize cellSize() const override { return {120, 50}; }

private:
    PageTemplateManager *m_manager = nullptr;
};

#endif // PENELOPE_PAGETEMPLATEPICKERWIDGET_H
