#ifndef PENELOPE_MAINWINDOW_H
#define PENELOPE_MAINWINDOW_H

#include <KXmlGuiWindow>

class QAction;
class QCloseEvent;
class QLabel;
class QSlider;
class QSplitter;
class QSpinBox;
class QTabWidget;
class KActionMenu;
class KRecentFilesAction;
class FileBrowserDock;
class FontManager;
class PageTemplateManager;
class TypeSetManager;
class MetadataStore;
class ColorDockWidget;
class PageDockWidget;
class PaletteManager;
class Sidebar;
class TextShaper;
class ThemeComposer;
class TocWidget;
class ThemeManager;
class TypeDockWidget;
class ThemePickerDock;
class DocumentTab;
class DocumentView;
class Hyphenator;
class ShortWords;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QUrl &url);
    void restoreOpenFiles();
    void activateWithFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onFileOpen();
    void onFileOpenRecent(const QUrl &url);
    void onFileExportPdf();
    void onFileExportRtf();
    void onFilePrint();
    void onFileClose();
    void onTabCloseRequested(int index);
    // onThemeChanged removed — composition is handled by onCompositionApplied()
    void onCompositionApplied();
    void onStyleOverrideChanged();
    void onPageLayoutChanged();
    void onZoomIn();
    void onZoomOut();
    void onFitWidth();
    void onFitPage();
    void showPreferences();
    void onSettingsChanged();
    void onRenderModeChanged();

private:
    void setupActions();
    void setupSidebars();
    void rebuildCurrentDocument();
    void saveSession();
    void restoreSession();
    DocumentView *currentDocumentView() const;
    DocumentTab *currentDocumentTab() const;

    QSplitter *m_splitter = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    // Sidebars
    Sidebar *m_leftSidebar = nullptr;
    Sidebar *m_rightSidebar = nullptr;
    int m_filesBrowserTabId = -1;
    int m_tocTabId = -1;
    int m_typeTabId = -1;
    int m_colorTabId = -1;

    ThemePickerDock *m_themePickerDock = nullptr;
    int m_themePickerTabId = -1;
    TypeDockWidget *m_typeDockWidget = nullptr;
    ColorDockWidget *m_colorDockWidget = nullptr;
    PageDockWidget *m_pageDockWidget = nullptr;
    FileBrowserDock *m_fileBrowserWidget = nullptr;
    TocWidget *m_tocWidget = nullptr;
    int m_pageTabId = -1;
    KRecentFilesAction *m_recentFilesAction = nullptr;
    ThemeManager *m_themeManager = nullptr;
    PaletteManager *m_paletteManager = nullptr;
    TypeSetManager *m_typeSetManager = nullptr;
    PageTemplateManager *m_pageTemplateManager = nullptr;
    ThemeComposer *m_themeComposer = nullptr;
    MetadataStore *m_metadataStore = nullptr;
    Hyphenator *m_hyphenator = nullptr;
    ShortWords *m_shortWords = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QSpinBox *m_zoomSpinBox = nullptr;
    QLabel *m_filePathLabel = nullptr;

    // PDF rendering pipeline (Phase 4)
    FontManager *m_fontManager = nullptr;
    TextShaper *m_textShaper = nullptr;

    // Render mode (Web / Print / Source)
    QAction *m_webViewAction = nullptr;
    QAction *m_printViewAction = nullptr;
    QAction *m_sourceViewAction = nullptr;
    QAction *m_fitWidthAction = nullptr;
    KActionMenu *m_pageArrangementMenu = nullptr;

    // Composition generation counter — incremented on any theme/style/layout change
    quint64 m_compositionGeneration = 1;
};

#endif // PENELOPE_MAINWINDOW_H
