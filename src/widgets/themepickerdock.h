#ifndef PENELOPE_THEMEPICKERDOCK_H
#define PENELOPE_THEMEPICKERDOCK_H

#include <QWidget>

#include "pagelayout.h"

class PageTemplateManager;
class PageTemplatePickerWidget;
class PaletteManager;
class PalettePickerWidget;
class ThemeComposer;
class ThemeManager;
class TypeSetManager;
class TypeSetPickerWidget;

class ThemePickerDock : public QWidget
{
    Q_OBJECT

public:
    explicit ThemePickerDock(ThemeManager *themeManager,
                             PaletteManager *paletteManager,
                             TypeSetManager *typeSetManager,
                             PageTemplateManager *pageTemplateManager,
                             ThemeComposer *themeComposer,
                             QWidget *parent = nullptr);

    // Sync picker highlights from composer state
    void syncPickersFromComposer();

    // Current selections (for save/restore)
    QString currentTypeSetId() const;
    QString currentColorSchemeId() const;
    QString currentTemplateId() const;
    void setCurrentTypeSetId(const QString &id);
    void setCurrentColorSchemeId(const QString &id);
    void setCurrentTemplateId(const QString &id);

    // Show/hide template section based on render mode
    void setRenderMode(bool printMode);

Q_SIGNALS:
    void compositionApplied(); // type set or palette changed, compose() done
    void templateApplied(const PageLayout &layout);
    void typeSetEditRequested(const QString &id);
    void paletteEditRequested(const QString &id);

private Q_SLOTS:
    void onTypeSetSelected(const QString &id);
    void onPaletteSelected(const QString &id);
    void onTemplateSelected(const QString &id);

private:
    void buildUI();
    void composeAndNotify();

    ThemeManager *m_themeManager;
    PaletteManager *m_paletteManager;
    TypeSetManager *m_typeSetManager;
    PageTemplateManager *m_pageTemplateManager;
    ThemeComposer *m_themeComposer;
    // Pickers
    TypeSetPickerWidget *m_typeSetPicker = nullptr;
    PalettePickerWidget *m_palettePicker = nullptr;
    PageTemplatePickerWidget *m_templatePicker = nullptr;
    QWidget *m_templateSection = nullptr;

    QString m_currentTemplateId;
};

#endif // PENELOPE_THEMEPICKERDOCK_H
