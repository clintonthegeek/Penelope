#include "mainwindow.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KAboutData>
#include <KHelpMenu>
#include <QDesktopServices>
#include <KLocalizedString>
#include <KRecentFilesAction>
#include <KStandardAction>
#include <KToolBar>
#include <KConfigGroup>
#include <KSharedConfig>

#include "pagelayout.h"
#include "colordockwidget.h"
#include "pagedockwidget.h"
#include "sidebar.h"
#include "toolview.h"
#include "documenttab.h"
#include "documentview.h"
#include "webviewitem.h"
#include "filebrowserdock.h"
#include "hyphenator.h"
#include "markdownhighlighter.h"
#include "metadatastore.h"
#include "contentrtfexporter.h"
#include "shortwords.h"
#include "tocwidget.h"
#include "printcontroller.h"
#include "headerfooterplan.h"
#include "stylemanager.h"
#include "typedockwidget.h"
#include "themepickerdock.h"
#include "typeset.h"
#include "typesetmanager.h"
#include "pagetemplatemanager.h"
#include "colorpalette.h"
#include "palettemanager.h"
#include "themecomposer.h"
#include "thememanager.h"
#include "footnotestyle.h"
#include "preferencesdialog.h"
#include "penelopesettings.h"
#include "languagepickerdialog.h"
#include "rtfcopyoptionsdialog.h"

// PDF rendering pipeline (Phase 4)
#include "contentbuilder.h"
#include "fontmanager.h"
#include "textshaper.h"
#include "layoutengine.h"
#include "pdfgenerator.h"
#include "pdfexportdialog.h"
#include "pdfexportoptions.h"
#include "contentfilter.h"
#include "pagerangeparser.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSet>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>
#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, false);

    // Central widget: splitter with sidebars + tabbed document area
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_tabWidget = new QTabWidget;
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, [this]() {
        if (m_compositionController) m_compositionController->cancel();
        m_renderTarget.clear();
        auto *view = currentDocumentView();
        if (view) {
            int zoom = view->zoomPercent();
            if (m_zoomSpinBox) {
                const QSignalBlocker blocker(m_zoomSpinBox);
                m_zoomSpinBox->setValue(zoom);
            }
            if (m_zoomSlider) {
                const QSignalBlocker blocker(m_zoomSlider);
                m_zoomSlider->setValue(zoom);
            }
        }

        // A2: Update file browser to show current file's directory
        // A6: Update status bar file path
        auto *tab = currentDocumentTab();
        if (tab && !tab->filePath().isEmpty()) {
            QFileInfo fi(tab->filePath());
            m_fileBrowserWidget->setRootPath(fi.absolutePath());
            if (m_filePathLabel)
                m_filePathLabel->setText(tab->filePath());
        } else {
            if (m_filePathLabel)
                m_filePathLabel->clear();
        }
        attachPageIndicator(tab);
        schedulePageOracle();
        refreshPageIndicator();

        // Sync view mode actions with current tab state
        if (tab && tab->isSourceMode()) {
            if (m_sourceViewAction)
                m_sourceViewAction->setChecked(true);
        } else {
            bool webMode = PenelopeSettings::self()->useWebView();
            if (webMode && m_webViewAction)
                m_webViewAction->setChecked(true);
            else if (!webMode && m_printViewAction)
                m_printViewAction->setChecked(true);
        }

        const bool printControls = tab && !tab->isSourceMode()
            && !PenelopeSettings::self()->useWebView();
        if (m_fitWidthAction) m_fitWidthAction->setEnabled(printControls);
        if (m_pageArrangementMenu) m_pageArrangementMenu->setEnabled(printControls);

        if (view) {
            const QStringList arrangements = {QStringLiteral("view_continuous"), QStringLiteral("view_single_page"),
                QStringLiteral("view_facing_pages"), QStringLiteral("view_facing_first_alone"),
                QStringLiteral("view_continuous_facing"), QStringLiteral("view_continuous_facing_first_alone")};
            for (int i = 0; i < arrangements.size(); ++i)
                if (auto *action = actionCollection()->action(arrangements[i])) action->setChecked(i == view->viewMode());
            if (auto *hand = actionCollection()->action(QStringLiteral("tool_hand")))
                hand->setChecked(view->cursorMode() == DocumentView::HandTool);
            if (auto *select = actionCollection()->action(QStringLiteral("tool_selection")))
                select->setChecked(view->cursorMode() == DocumentView::SelectionTool);
            if (auto *copy = actionCollection()->action(QStringLiteral("edit_copy")))
                copy->setEnabled(tab->isSourceMode() || view->cursorMode() == DocumentView::SelectionTool);
        }

        // Rebuild stale tabs (composition changed since last render)
        if (tab && tab->compositionGeneration() < m_compositionGeneration) {
            rebuildCurrentDocument();
        } else if (tab && tab->hasTocData()) {
            // Tab is current — just rebuild TOC from cached data
            m_tocWidget->buildFromContentModel(tab->cachedContentDoc(), tab->cachedSourceMap());
        } else if (!tab) {
            m_tocWidget->buildFromContentModel({}, {});
        }
    });

    m_themeManager = new ThemeManager(this);
    m_paletteManager = new PaletteManager(this);
    m_typeSetManager = new TypeSetManager(this);
    m_pageTemplateManager = new PageTemplateManager(this);
    m_themeComposer = new ThemeComposer(m_themeManager, this);
    m_metadataStore = new MetadataStore(this);

    // Typography engines
    m_hyphenator = new Hyphenator();
    m_shortWords = new ShortWords();

    // PDF rendering pipeline (created once, reused across rebuilds)
    m_fontManager = new FontManager();
    m_textShaper = new TextShaper(m_fontManager);

    // Load bundled symbol fallback font for glyphs missing in body fonts
    FontFace *fallback = m_fontManager->loadFontFromPath(
        QStringLiteral(":/fonts/PrettySymbolsFallback.ttf"));
    m_textShaper->setFallbackFont(fallback);

    m_pageOracle = new CompositionController(this);
    m_pageOracleDebounce = new QTimer(this);
    m_pageOracleDebounce->setSingleShot(true);
    m_pageOracleDebounce->setInterval(500);
    connect(m_pageOracleDebounce, &QTimer::timeout, this, &MainWindow::startPageOracle);
    connect(m_pageOracle, &CompositionController::ready, this,
            [this](quint64 id, CompositionResultPtr result) {
                if (id != m_pageOracleRequest || !result)
                    return;
                m_pageOracleTotal = result->paginated.pages.size();
                m_pageOracleMap = result->paginated.sourceMap;
                refreshPageIndicator();
            });
    connect(m_pageOracle, &CompositionController::failed, this, [this](quint64 id, const QString &) {
        if (id == m_pageOracleRequest) {
            m_pageOracleTotal = 0;
            m_pageOracleMap.clear();
            refreshPageIndicator();
        }
    });

    m_compositionController = new CompositionController(this);
    connect(m_compositionController, &CompositionController::ready,
            this, &MainWindow::installComposition);
    connect(m_compositionController, &CompositionController::failed, this,
            [this](quint64 id, const QString &error) {
        if (id == m_renderRequest && m_renderTarget == currentDocumentTab())
            statusBar()->showMessage(i18n("Rendering failed: %1", error), 8000);
    });

    // Apply settings
    auto *settings = PenelopeSettings::self();
    if (settings->hyphenationEnabled() || settings->hyphenateJustifiedText()) {
        m_hyphenator->loadDictionary(settings->hyphenationLanguage());
        m_hyphenator->setMinWordLength(settings->hyphenationMinWordLength());
    }
    if (settings->shortWordsEnabled()) {
        m_shortWords->setLanguage(settings->hyphenationLanguage());
    }

    setupSidebars();

    // Assemble splitter: left sidebar | tabs | right sidebar
    m_splitter->addWidget(m_leftSidebar);
    m_splitter->addWidget(m_tabWidget);
    m_splitter->addWidget(m_rightSidebar);

    // Set stretch factors: sidebars don't stretch, center does
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

    // Prevent user from collapsing widgets to zero via handle drag
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);
    m_splitter->setCollapsible(2, false);

    // Sidebars start collapsed — lock them to tab bar width
    m_leftSidebar->setCollapsed(true);
    m_rightSidebar->setCollapsed(true);

    setCentralWidget(m_splitter);

    setupActions();

    // A6: File path label (left-justified, auto-hides for temporary messages)
    m_filePathLabel = new QLabel;
    m_filePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_filePathLabel, 1);

    // Page position: the printed page the reader is on, in either view mode.
    m_pageIndicator = new QLabel;
    m_pageIndicator->setObjectName(QStringLiteral("pageIndicator"));
    m_pageIndicator->setToolTip(i18n("Position in the printed page sequence"));
    m_pageIndicator->setMinimumWidth(90);
    statusBar()->addPermanentWidget(m_pageIndicator);

    // A5: Zoom slider + spinbox on status bar (permanent, right side)
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(25, 400);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(120);
    m_zoomSlider->setToolTip(i18n("Zoom level"));
    statusBar()->addPermanentWidget(m_zoomSlider);

    m_zoomSpinBox = new QSpinBox;
    m_zoomSpinBox->setRange(25, 400);
    m_zoomSpinBox->setSuffix(QStringLiteral("%"));
    m_zoomSpinBox->setValue(100);
    m_zoomSpinBox->setFixedWidth(80);
    m_zoomSpinBox->setToolTip(i18n("Zoom level"));
    statusBar()->addPermanentWidget(m_zoomSpinBox);

    // Bidirectional sync between slider and spinbox (with signal blockers)
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        const QSignalBlocker b(m_zoomSpinBox);
        m_zoomSpinBox->setValue(value);
        auto *view = currentDocumentView();
        if (view) view->setZoomPercent(value);
    });
    connect(m_zoomSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        const QSignalBlocker b(m_zoomSlider);
        m_zoomSlider->setValue(value);
        auto *view = currentDocumentView();
        if (view) view->setZoomPercent(value);
    });

    statusBar()->showMessage(i18n("Ready"));

    setMinimumSize(800, 600);
    resize(1200, 800);

    // Load default typography theme + color palette so the style tree is populated
    {
        QStringList typeSets = m_typeSetManager->availableTypeSets();
        if (!typeSets.isEmpty()) {
            TypeSet ts = m_typeSetManager->typeSet(
                typeSets.contains(QStringLiteral("default")) ? QStringLiteral("default") : typeSets.first());
            m_themeComposer->setTypeSet(ts);
        }
        QStringList palettes = m_paletteManager->availablePalettes();
        if (!palettes.isEmpty()) {
            ColorPalette palette = m_paletteManager->palette(
                palettes.contains(QStringLiteral("default-light")) ? QStringLiteral("default-light") : palettes.first());
            m_themeComposer->setColorPalette(palette);
        }
        onCompositionApplied();
    }

    PageLayout defaults = m_pageDockWidget->currentPageLayout();
    defaults.pageSizeId = PageLayout::fromJson(
        {{QStringLiteral("pageSize"), settings->defaultPageSizeName()}}, {}).pageSizeId;
    m_pageDockWidget->setPageLayout(defaults);
    restoreSession();

    // A1: Fresh launch = TOC open by default (saved session state takes priority)
    if (!KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Session"))
             .hasKey("LeftSidebarCollapsed"))
        m_leftSidebar->showPanel(m_tocTabId);
}

MainWindow::~MainWindow()
{
    delete m_pageOracle;
    m_pageOracle = nullptr;
    delete m_compositionController;
    m_compositionController = nullptr;
    delete m_hyphenator;
    delete m_shortWords;
    delete m_textShaper;
    delete m_fontManager;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
        if (!confirmClose(tab)) { event->ignore(); return; }
    }
    saveSession();

    KConfigGroup recentGroup(KSharedConfig::openConfig(),
                             QStringLiteral("RecentFiles"));
    if (m_recentFilesAction)
        m_recentFilesAction->saveEntries(recentGroup);

    KXmlGuiWindow::closeEvent(event);
    qApp->quit();
}

void MainWindow::onTabCloseRequested(int index)
{
    auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(index));
    if (!tab || !confirmClose(tab)) return;
    saveTabViewState(tab);
    if (m_renderTarget == tab) { m_compositionController->cancel(); m_renderTarget.clear(); }
    m_tabWidget->removeTab(index);
    delete tab;

    if (m_tabWidget->count() == 0) {
        // Last tab closed — quit (closeEvent will save session)
        close();
    }
    // Remaining tabs: currentChanged signal already fires and updates sidebars
}

void MainWindow::setupSidebars()
{
    // Left sidebar: File Browser + TOC
    m_leftSidebar = new Sidebar(Sidebar::Left, this);

    m_fileBrowserWidget = new FileBrowserDock(this);
    auto *filesView = new ToolView(i18n("Files"), m_fileBrowserWidget);
    m_filesBrowserTabId = m_leftSidebar->addPanel(
        filesView, QIcon::fromTheme(QStringLiteral("folder")), i18n("Files"));

    connect(m_fileBrowserWidget, &FileBrowserDock::fileActivated,
            this, &MainWindow::openFile);

    m_tocWidget = new TocWidget(this);
    auto *tocView = new ToolView(i18n("Contents"), m_tocWidget);
    m_tocTabId = m_leftSidebar->addPanel(
        tocView, QIcon::fromTheme(QStringLiteral("format-list-ordered")), i18n("Contents"));

    connect(m_tocWidget, &TocWidget::headingNavigate,
            this, [this](int page, qreal yOffset, int sourceLine) {
        auto *view = currentDocumentView();
        if (!view)
            return;
        if (page < 0)
            view->followHeading(sourceLine);
        else
            view->scrollToPosition(page, yOffset);
    });

    // Source view: ToC click scrolls to source line
    connect(m_tocWidget, &TocWidget::headingSourceNavigate,
            this, [this](int sourceLine) {
        auto *tab = currentDocumentTab();
        if (!tab || !tab->isSourceMode() || sourceLine < 1)
            return;
        auto *editor = tab->sourceEditor();
        // sourceLine is 1-based, QTextBlock is 0-based
        QTextBlock block = editor->document()->findBlockByNumber(sourceLine - 1);
        if (block.isValid()) {
            QTextCursor cursor(block);
            editor->setTextCursor(cursor);
            editor->centerCursor();
        }
    });

    // Right sidebar: Theme + Type + Color + Page
    m_rightSidebar = new Sidebar(Sidebar::Right, this);

    // 1. Theme Picker (preview-only quick-picker grids)
    m_themePickerDock = new ThemePickerDock(
        m_themeManager, m_paletteManager, m_typeSetManager,
        m_pageTemplateManager, m_themeComposer, this);
    auto *themeView = new ToolView(i18n("Theme"), m_themePickerDock);
    m_themePickerTabId = m_rightSidebar->addPanel(
        themeView, QIcon::fromTheme(QStringLiteral("preferences-desktop-theme-global")), i18n("Theme"));

    // 2. Type (type set selector + font combos + style tree)
    m_typeDockWidget = new TypeDockWidget(m_typeSetManager, m_themeComposer, this);
    auto *typeView = new ToolView(i18n("Type"), m_typeDockWidget);
    m_typeTabId = m_rightSidebar->addPanel(
        typeView, QIcon::fromTheme(QStringLiteral("preferences-desktop-font")), i18n("Type"));

    // 3. Color (palette selector + color editors)
    m_colorDockWidget = new ColorDockWidget(m_paletteManager, m_themeComposer, this);
    auto *colorView = new ToolView(i18n("Color"), m_colorDockWidget);
    m_colorTabId = m_rightSidebar->addPanel(
        colorView, QIcon::fromTheme(QStringLiteral("color-management")), i18n("Color"));

    // 4. Page (template selector + page layout controls)
    m_pageDockWidget = new PageDockWidget(m_pageTemplateManager, this);
    auto *pageView = new ToolView(i18n("Page"), m_pageDockWidget);
    m_pageTabId = m_rightSidebar->addPanel(
        pageView, QIcon::fromTheme(QStringLiteral("document-properties")), i18n("Page"));

    // Wire Theme picker -> editing docks (grid click syncs dropdown)
    connect(m_themePickerDock, &ThemePickerDock::compositionApplied,
            this, &MainWindow::onCompositionApplied);
    connect(m_themePickerDock, &ThemePickerDock::templateApplied,
            this, [this](const PageLayout &templateLayout) {
        PageLayout pl = templateLayout;
        QColor pageBg = m_themeComposer->currentPalette().pageBackground();
        if (pageBg.isValid())
            pl.pageBackground = pageBg;
        m_pageDockWidget->setPageLayout(pl);
        auto *view = currentDocumentView();
        if (view)
            view->setPageLayout(pl);
        rebuildCurrentDocument();
    });

    // Double-click in Theme grid -> raise editing dock
    connect(m_themePickerDock, &ThemePickerDock::typeSetEditRequested,
            this, [this](const QString &id) {
        m_typeDockWidget->setCurrentTypeSetId(id);
        m_rightSidebar->showPanel(m_typeTabId);
    });
    connect(m_themePickerDock, &ThemePickerDock::paletteEditRequested,
            this, [this](const QString &id) {
        m_colorDockWidget->setCurrentPaletteId(id);
        m_rightSidebar->showPanel(m_colorTabId);
    });

    // Wire Type dock
    connect(m_typeDockWidget, &TypeDockWidget::styleOverrideChanged,
            this, &MainWindow::onStyleOverrideChanged);
    connect(m_typeDockWidget, &TypeDockWidget::typeSetChanged,
            this, [this](const QString &id) {
        m_themePickerDock->setCurrentTypeSetId(id);
        onCompositionApplied();
    });

    // Wire Color dock
    connect(m_colorDockWidget, &ColorDockWidget::paletteChanged,
            this, [this](const QString &id) {
        m_themePickerDock->setCurrentColorSchemeId(id);
        onCompositionApplied();
    });

    // Wire Page dock
    connect(m_pageDockWidget, &PageDockWidget::pageLayoutChanged,
            this, &MainWindow::onPageLayoutChanged);
    connect(m_pageDockWidget, &PageDockWidget::templateChanged,
            this, [this](const QString &id) {
        m_themePickerDock->setCurrentTemplateId(id);
    });

    // Cross-sync: Theme grid click -> editing dock dropdowns
    // Type set grid -> Type dock dropdown
    connect(m_themePickerDock, &ThemePickerDock::compositionApplied,
            this, [this]() {
        QString tsId = m_themePickerDock->currentTypeSetId();
        if (!tsId.isEmpty())
            m_typeDockWidget->setCurrentTypeSetId(tsId);
        QString palId = m_themePickerDock->currentColorSchemeId();
        if (!palId.isEmpty())
            m_colorDockWidget->setCurrentPaletteId(palId);
    });
}

void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    // Standard actions
    KStandardAction::quit(qApp, &QApplication::quit, ac);
    KStandardAction::preferences(this, &MainWindow::showPreferences, ac);

    // KXmlGuiWindow would add its own help menu, whose About KDE entry belongs
    // to another project; this application builds the menu it wants instead.
    setHelpMenuEnabled(false);

    // Help menu. KHelpMenu builds the About dialog from the KAboutData set in
    // main(), so the version shown there comes from CMake. Penelope is not a
    // KDE project: help and bug reports point at its own pages, and there is no
    // "About KDE" entry.
    auto *helpMenu = new KHelpMenu(this);

    // The About entry is ours rather than KStandardAction::aboutApp: that one
    // takes its text from the executable's name, which is lower case.
    auto *aboutAction = ac->addAction(QStringLiteral("help_about"));
    aboutAction->setText(i18n("About %1", KAboutData::applicationData().displayName()));
    aboutAction->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));
    connect(aboutAction, &QAction::triggered, helpMenu, &KHelpMenu::aboutApplication);

    KStandardAction::reportBug(helpMenu, &KHelpMenu::reportBug, ac);

    // KHelpMenu registers its own actions when it is constructed, "About KDE"
    // among them. The ui.rc no longer lists it, and a menu layout cached by an
    // older version must not be able to bring it back, so drop the action.
    if (auto *kdeAction = ac->action(QStringLiteral("help_about_kde"))) {
        ac->removeAction(kdeAction);
        kdeAction->deleteLater();
    }

    auto *websiteAction = ac->addAction(QStringLiteral("help_website"));
    websiteAction->setText(i18n("Penelope &Website"));
    websiteAction->setIcon(QIcon::fromTheme(QStringLiteral("internet-web-browser")));
    websiteAction->setData(QUrl(QStringLiteral("https://vibekoder.ca")));
    connect(websiteAction, &QAction::triggered, this, [websiteAction]() {
        QDesktopServices::openUrl(websiteAction->data().toUrl());
    });

    // File > Open
    auto *openAction = KStandardAction::open(this, &MainWindow::onFileOpen, ac);
    openAction->setPriority(QAction::LowPriority);

    // File > Open Recent
    m_recentFilesAction = KStandardAction::openRecent(
        this, &MainWindow::onFileOpenRecent, ac);
    KConfigGroup recentGroup(KSharedConfig::openConfig(),
                             QStringLiteral("RecentFiles"));
    m_recentFilesAction->loadEntries(recentGroup);

    // File > Export PDF
    auto *exportPdf = ac->addAction(QStringLiteral("file_export_pdf"));
    exportPdf->setText(i18n("Export as &PDF..."));
    exportPdf->setIcon(QIcon::fromTheme(QStringLiteral("document-export")));
    exportPdf->setPriority(QAction::LowPriority);
    connect(exportPdf, &QAction::triggered, this, &MainWindow::onFileExportPdf);

    // File > Export RTF
    auto *exportRtf = ac->addAction(QStringLiteral("file_export_rtf"));
    exportRtf->setText(i18n("Export as &RTF..."));
    exportRtf->setIcon(QIcon::fromTheme(QStringLiteral("document-export")));
    connect(exportRtf, &QAction::triggered, this, &MainWindow::onFileExportRtf);

    // File > Print
    auto *printAction = KStandardAction::print(this, &MainWindow::onFilePrint, ac);
    printAction->setPriority(QAction::LowPriority);

    KStandardAction::save(this, &MainWindow::onFileSave, ac);

    // File > Close
    auto *closeAction = KStandardAction::close(this, &MainWindow::onFileClose, ac);
    Q_UNUSED(closeAction);

    // View > Zoom
    auto *zoomInAction = KStandardAction::zoomIn(this, &MainWindow::onZoomIn, ac);
    zoomInAction->setPriority(QAction::LowPriority);
    auto *zoomOutAction = KStandardAction::zoomOut(this, &MainWindow::onZoomOut, ac);
    zoomOutAction->setPriority(QAction::LowPriority);

    m_fitWidthAction = ac->addAction(QStringLiteral("view_zoom_fit_width"));
    m_fitWidthAction->setText(i18n("Fit &Width"));
    m_fitWidthAction->setIcon(QIcon::fromTheme(QStringLiteral("zoom-fit-width")));
    m_fitWidthAction->setEnabled(!PenelopeSettings::self()->useWebView());
    connect(m_fitWidthAction, &QAction::triggered, this, &MainWindow::onFitWidth);

    auto *fitPage = ac->addAction(QStringLiteral("view_zoom_fit_page"));
    fitPage->setText(i18n("Fit &Page"));
    fitPage->setIcon(QIcon::fromTheme(QStringLiteral("zoom-fit-page")));
    connect(fitPage, &QAction::triggered, this, &MainWindow::onFitPage);

    // View > Render Mode (Web / Print / Source — exclusive group)
    auto *renderModeGroup = new QActionGroup(this);
    renderModeGroup->setExclusive(true);

    m_webViewAction = ac->addAction(QStringLiteral("view_web_mode"));
    m_webViewAction->setText(i18n("&Web View"));
    m_webViewAction->setIcon(QIcon::fromTheme(QStringLiteral("text-html")));
    m_webViewAction->setCheckable(true);
    m_webViewAction->setChecked(PenelopeSettings::self()->useWebView());
    m_webViewAction->setActionGroup(renderModeGroup);
    connect(m_webViewAction, &QAction::triggered, this, [this]() {
        // Exit source mode if active
        auto *tab = currentDocumentTab();
        if (tab && tab->isSourceMode())
            tab->setSourceMode(false);
        PenelopeSettings::self()->setUseWebView(true);
        PenelopeSettings::self()->save();
        onRenderModeChanged();
    });

    m_printViewAction = ac->addAction(QStringLiteral("view_print_mode"));
    m_printViewAction->setText(i18n("&Print View"));
    m_printViewAction->setIcon(QIcon::fromTheme(QStringLiteral("document-print-preview")));
    m_printViewAction->setCheckable(true);
    m_printViewAction->setChecked(!PenelopeSettings::self()->useWebView());
    m_printViewAction->setActionGroup(renderModeGroup);
    connect(m_printViewAction, &QAction::triggered, this, [this]() {
        auto *tab = currentDocumentTab();
        if (tab && tab->isSourceMode())
            tab->setSourceMode(false);
        PenelopeSettings::self()->setUseWebView(false);
        PenelopeSettings::self()->save();
        onRenderModeChanged();
    });

    m_sourceViewAction = ac->addAction(QStringLiteral("view_source_mode"));
    m_sourceViewAction->setText(i18n("&Source View"));
    m_sourceViewAction->setIcon(QIcon::fromTheme(QStringLiteral("text-x-script")));
    m_sourceViewAction->setCheckable(true);
    m_sourceViewAction->setActionGroup(renderModeGroup);
    ac->setDefaultShortcut(m_sourceViewAction, QKeySequence(Qt::CTRL | Qt::Key_U));
    connect(m_sourceViewAction, &QAction::triggered, this, [this]() {
        auto *tab = currentDocumentTab();
        if (!tab)
            return;
        tab->setSourceMode(true);
        m_compositionController->cancel();
        m_renderTarget.clear();
        // Fit Width and Page Arrangement only apply to Print view
        if (m_fitWidthAction)
            m_fitWidthAction->setEnabled(false);
        if (m_pageArrangementMenu)
            m_pageArrangementMenu->setEnabled(false);
        if (auto *copy = actionCollection()->action(QStringLiteral("edit_copy"))) copy->setEnabled(true);
        statusBar()->showMessage(i18n("Source view"), 2000);
    });

    // View > Mode (exclusive action group)
    auto *viewModeGroup = new QActionGroup(this);
    viewModeGroup->setExclusive(true);

    auto *continuous = ac->addAction(QStringLiteral("view_continuous"));
    continuous->setText(i18n("&Continuous Scroll"));
    continuous->setIcon(QIcon::fromTheme(QStringLiteral("view-pages-continuous")));
    continuous->setCheckable(true);
    continuous->setChecked(true);
    continuous->setActionGroup(viewModeGroup);
    connect(continuous, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->setViewMode(DocumentView::Continuous);
    });

    auto *singlePage = ac->addAction(QStringLiteral("view_single_page"));
    singlePage->setText(i18n("&Single Page"));
    singlePage->setIcon(QIcon::fromTheme(QStringLiteral("view-paged-symbolic")));
    singlePage->setCheckable(true);
    singlePage->setActionGroup(viewModeGroup);
    connect(singlePage, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->setViewMode(DocumentView::SinglePage);
    });

    auto *facingPages = ac->addAction(QStringLiteral("view_facing_pages"));
    facingPages->setText(i18n("&Facing Pages"));
    facingPages->setIcon(QIcon::fromTheme(QStringLiteral("view-pages-facing")));
    facingPages->setCheckable(true);
    facingPages->setActionGroup(viewModeGroup);
    connect(facingPages, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->setViewMode(DocumentView::FacingPages);
    });

    auto *facingFirstAlone = ac->addAction(QStringLiteral("view_facing_first_alone"));
    facingFirstAlone->setText(i18n("Facing Pages (First &Alone)"));
    facingFirstAlone->setIcon(QIcon::fromTheme(QStringLiteral("view-pages-facing-first-centered")));
    facingFirstAlone->setCheckable(true);
    facingFirstAlone->setActionGroup(viewModeGroup);
    connect(facingFirstAlone, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->setViewMode(DocumentView::FacingPagesFirstAlone);
    });

    auto *continuousFacing = ac->addAction(QStringLiteral("view_continuous_facing"));
    continuousFacing->setText(i18n("Continuous F&acing"));
    continuousFacing->setIcon(QIcon::fromTheme(QStringLiteral("view-pages-facing-symbolic")));
    continuousFacing->setCheckable(true);
    continuousFacing->setActionGroup(viewModeGroup);
    connect(continuousFacing, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->setViewMode(DocumentView::ContinuousFacing);
    });

    auto *continuousFacingFirstAlone = ac->addAction(QStringLiteral("view_continuous_facing_first_alone"));
    continuousFacingFirstAlone->setText(i18n("Continuous Facing (First A&lone)"));
    continuousFacingFirstAlone->setIcon(QIcon::fromTheme(QStringLiteral("view-pages-facing-first-centered")));
    continuousFacingFirstAlone->setCheckable(true);
    continuousFacingFirstAlone->setActionGroup(viewModeGroup);
    connect(continuousFacingFirstAlone, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->setViewMode(DocumentView::ContinuousFacingFirstAlone);
    });

    // A4: Page Arrangement submenu (collects the 6 view mode actions)
    auto *arrangementMenu = new KActionMenu(
        QIcon::fromTheme(QStringLiteral("view-list-details")),
        i18n("Page &Arrangement"), this);
    ac->addAction(QStringLiteral("view_page_arrangement"), arrangementMenu);
    arrangementMenu->setPriority(QAction::LowPriority);
    m_pageArrangementMenu = arrangementMenu;
    arrangementMenu->addAction(continuous);
    arrangementMenu->addAction(singlePage);
    arrangementMenu->addAction(facingPages);
    arrangementMenu->addAction(facingFirstAlone);
    arrangementMenu->addAction(continuousFacing);
    arrangementMenu->addAction(continuousFacingFirstAlone);

    // Go > Navigation
    auto *prevPage = ac->addAction(QStringLiteral("go_previous_page"));
    prevPage->setText(i18n("&Previous Page"));
    prevPage->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
    prevPage->setPriority(QAction::LowPriority);
    ac->setDefaultShortcut(prevPage, QKeySequence(Qt::Key_PageUp));
    connect(prevPage, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->previousPage();
    });

    auto *nextPage = ac->addAction(QStringLiteral("go_next_page"));
    nextPage->setText(i18n("&Next Page"));
    nextPage->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
    nextPage->setPriority(QAction::LowPriority);
    ac->setDefaultShortcut(nextPage, QKeySequence(Qt::Key_PageDown));
    connect(nextPage, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view)
            view->nextPage();
    });

    // Sidebar toggle actions
    auto *toggleFiles = ac->addAction(QStringLiteral("view_toggle_files"));
    toggleFiles->setText(i18n("&Files Panel"));
    toggleFiles->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
    toggleFiles->setCheckable(true);
    connect(toggleFiles, &QAction::triggered, this, [this](bool checked) {
        if (checked)
            m_leftSidebar->showPanel(m_filesBrowserTabId);
        else
            m_leftSidebar->hidePanel(m_filesBrowserTabId);
    });
    connect(m_leftSidebar, &Sidebar::panelVisibilityChanged,
            this, [this, toggleFiles](int tabId, bool visible) {
        if (tabId == m_filesBrowserTabId)
            toggleFiles->setChecked(visible);
    });

    auto *toggleToc = ac->addAction(QStringLiteral("view_toggle_toc"));
    toggleToc->setText(i18n("&Contents Panel"));
    toggleToc->setIcon(QIcon::fromTheme(QStringLiteral("format-list-ordered")));
    toggleToc->setCheckable(true);
    connect(toggleToc, &QAction::triggered, this, [this](bool checked) {
        if (checked)
            m_leftSidebar->showPanel(m_tocTabId);
        else
            m_leftSidebar->hidePanel(m_tocTabId);
    });
    connect(m_leftSidebar, &Sidebar::panelVisibilityChanged,
            this, [this, toggleToc](int tabId, bool visible) {
        if (tabId == m_tocTabId)
            toggleToc->setChecked(visible);
    });

    auto *toggleTheme = ac->addAction(QStringLiteral("view_toggle_theme"));
    toggleTheme->setText(i18n("&Theme Panel"));
    toggleTheme->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-theme-global")));
    toggleTheme->setCheckable(true);
    connect(toggleTheme, &QAction::triggered, this, [this](bool checked) {
        if (checked)
            m_rightSidebar->showPanel(m_themePickerTabId);
        else
            m_rightSidebar->hidePanel(m_themePickerTabId);
    });
    connect(m_rightSidebar, &Sidebar::panelVisibilityChanged,
            this, [this, toggleTheme](int tabId, bool visible) {
        if (tabId == m_themePickerTabId)
            toggleTheme->setChecked(visible);
    });

    auto *toggleType = ac->addAction(QStringLiteral("view_toggle_type"));
    toggleType->setText(i18n("T&ype Panel"));
    toggleType->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-font")));
    toggleType->setCheckable(true);
    connect(toggleType, &QAction::triggered, this, [this](bool checked) {
        if (checked)
            m_rightSidebar->showPanel(m_typeTabId);
        else
            m_rightSidebar->hidePanel(m_typeTabId);
    });
    connect(m_rightSidebar, &Sidebar::panelVisibilityChanged,
            this, [this, toggleType](int tabId, bool visible) {
        if (tabId == m_typeTabId)
            toggleType->setChecked(visible);
    });

    auto *toggleColor = ac->addAction(QStringLiteral("view_toggle_color"));
    toggleColor->setText(i18n("&Color Panel"));
    toggleColor->setIcon(QIcon::fromTheme(QStringLiteral("color-management")));
    toggleColor->setCheckable(true);
    connect(toggleColor, &QAction::triggered, this, [this](bool checked) {
        if (checked)
            m_rightSidebar->showPanel(m_colorTabId);
        else
            m_rightSidebar->hidePanel(m_colorTabId);
    });
    connect(m_rightSidebar, &Sidebar::panelVisibilityChanged,
            this, [this, toggleColor](int tabId, bool visible) {
        if (tabId == m_colorTabId)
            toggleColor->setChecked(visible);
    });

    // B1: Cursor mode toggle actions
    auto *handTool = ac->addAction(QStringLiteral("tool_hand"));
    handTool->setText(i18n("&Hand Tool"));
    handTool->setIcon(QIcon::fromTheme(QStringLiteral("transform-browse")));
    handTool->setCheckable(true);
    handTool->setChecked(true);
    handTool->setPriority(QAction::LowPriority);
    ac->setDefaultShortcut(handTool, QKeySequence(Qt::CTRL | Qt::Key_1));

    auto *selectTool = ac->addAction(QStringLiteral("tool_selection"));
    selectTool->setText(i18n("&Text Selection"));
    selectTool->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-text")));
    selectTool->setCheckable(true);
    selectTool->setPriority(QAction::LowPriority);
    ac->setDefaultShortcut(selectTool, QKeySequence(Qt::CTRL | Qt::Key_2));

    auto *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addAction(handTool);
    toolGroup->addAction(selectTool);

    connect(handTool, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view) view->setCursorMode(DocumentView::HandTool);
    });
    connect(selectTool, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view) view->setCursorMode(DocumentView::SelectionTool);
    });

    // B2: Copy action (Ctrl+C) — disabled when hand tool is active
    auto *copyAction = KStandardAction::copy(this, [this]() {
        auto *tab = currentDocumentTab();
        if (tab && tab->isSourceMode()) tab->sourceEditor()->copy();
        else if (auto *view = currentDocumentView()) view->copySelection();
    }, ac);
    copyAction->setPriority(QAction::LowPriority);
    copyAction->setEnabled(true); // Copy also applies to source-editor selection.

    connect(selectTool, &QAction::triggered, copyAction, [copyAction]() {
        copyAction->setEnabled(true);
    });
    connect(handTool, &QAction::triggered, copyAction, [copyAction]() {
        copyAction->setEnabled(false);
    });

    // Copy as Styled Text (RTF)
    auto *copyRtf = ac->addAction(QStringLiteral("edit_copy_rtf"));
    copyRtf->setText(i18n("Copy as &Styled Text"));
    copyRtf->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    ac->setDefaultShortcut(copyRtf, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(copyRtf, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view) view->copySelectionAsRtf();
    });

    // Copy with Style Options (filtered RTF)
    auto *copyComplex = ac->addAction(QStringLiteral("edit_copy_complex"));
    copyComplex->setText(i18n("Copy with Style &Options..."));
    copyComplex->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    ac->setDefaultShortcut(copyComplex, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    connect(copyComplex, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view) view->copySelectionAsComplexRtf();
    });

    // Copy as Markdown
    auto *copyMd = ac->addAction(QStringLiteral("edit_copy_markdown"));
    copyMd->setText(i18n("Copy as &Markdown"));
    copyMd->setIcon(QIcon::fromTheme(QStringLiteral("text-x-script")));
    connect(copyMd, &QAction::triggered, this, [this]() {
        auto *view = currentDocumentView();
        if (view) view->copySelectionAsMarkdown();
    });

    setupGUI(Default, QStringLiteral("penelopeui.rc"));

    // Show text labels by default; LowPriority actions get icon-only
    toolBar()->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void MainWindow::onFileOpen()
{
    const QUrl url = QFileDialog::getOpenFileUrl(
        this,
        i18n("Open Markdown File"),
        QUrl::fromLocalFile(QDir::homePath()),
        i18n("Markdown Files (*.md *.markdown *.mkd *.txt);;All Files (*)"));

    if (url.isValid()) {
        openFile(url);
    }
}

void MainWindow::onFileOpenRecent(const QUrl &url)
{
    openFile(url);
}

void MainWindow::onFileExportPdf()
{
    auto *view = currentDocumentView();
    if (!view) {
        statusBar()->showMessage(i18n("No document to export."), 3000);
        return;
    }

    auto *tab = currentDocumentTab();
    if (!tab)
        return;

    QString filePath = tab->filePath();
    const QString markdown = tab->sourceText();

    StyleManager *editingSm = m_typeDockWidget->currentStyleManager();
    std::unique_ptr<StyleManager> ownedStyle;
    StyleManager *styleManager;
    if (editingSm) {
        ownedStyle.reset(editingSm->clone());
        styleManager = ownedStyle.get();
    } else {
        ownedStyle = std::make_unique<StyleManager>();
        styleManager = ownedStyle.get();
        m_themeComposer->compose(styleManager);
    }

    QFileInfo fi(filePath);
    PageLayout pl = m_pageDockWidget->currentPageLayout();

    // Build content (needed for heading tree + page count)
    ContentBuilder contentBuilder;
    contentBuilder.setBasePath(fi.absolutePath());
    contentBuilder.setStyleManager(styleManager);
    if (PenelopeSettings::self()->hyphenationEnabled()
        || PenelopeSettings::self()->hyphenateJustifiedText())
        contentBuilder.setHyphenator(m_hyphenator);
    if (PenelopeSettings::self()->shortWordsEnabled())
        contentBuilder.setShortWords(m_shortWords);
    contentBuilder.setFootnoteStyle(styleManager->footnoteStyle());
    contentBuilder.setSmartPunctuation(PenelopeSettings::self()->smartPunctuationEnabled());
    Content::Document contentDoc = contentBuilder.build(markdown);
    view->applyLanguageOverrides(contentDoc);

    // Pre-layout to get page count for dialog
    m_fontManager->resetUsage();
    Layout::Engine preLayoutEngine(m_fontManager, m_textShaper);
    preLayoutEngine.setHyphenateJustifiedText(
        PenelopeSettings::self()->hyphenateJustifiedText());
    Layout::LayoutResult preLayout = preLayoutEngine.layout(contentDoc, pl);
    int pageCount = preLayout.pages.size();

    // Load saved options from KConfig
    PdfExportOptions opts;
    auto *settings = PenelopeSettings::self();
    opts.author = settings->pdfAuthor();
    opts.markdownCopy = settings->pdfMarkdownCopy();
    opts.unwrapParagraphs = settings->pdfUnwrapParagraphs();
    opts.includeBookmarks = settings->pdfIncludeBookmarks();
    opts.bookmarkMaxDepth = settings->pdfBookmarkMaxDepth();
    opts.initialView = static_cast<PdfExportOptions::InitialView>(
        settings->pdfInitialView());
    opts.pageLayout = static_cast<PdfExportOptions::PageLayout>(
        settings->pdfPageLayout());

    // Overlay per-document options from MetadataStore
    QJsonObject perDoc = m_metadataStore->load(filePath);
    if (perDoc.contains(QStringLiteral("pdfExportOptions"))) {
        QJsonObject saved = perDoc[QStringLiteral("pdfExportOptions")].toObject();
        if (saved.contains(QStringLiteral("title")))
            opts.title = saved[QStringLiteral("title")].toString();
        if (saved.contains(QStringLiteral("author")))
            opts.author = saved[QStringLiteral("author")].toString();
        if (saved.contains(QStringLiteral("subject")))
            opts.subject = saved[QStringLiteral("subject")].toString();
        if (saved.contains(QStringLiteral("keywords")))
            opts.keywords = saved[QStringLiteral("keywords")].toString();
        if (saved.contains(QStringLiteral("pageRangeExpr")))
            opts.pageRangeExpr = saved[QStringLiteral("pageRangeExpr")].toString();
        if (saved.contains(QStringLiteral("excludedHeadingIndices"))) {
            QJsonArray arr = saved[QStringLiteral("excludedHeadingIndices")].toArray();
            for (const auto &v : arr)
                opts.excludedHeadingIndices.insert(v.toInt());
        }
    }

    // Show export dialog
    PdfExportDialog dlg(contentDoc, pageCount, fi.baseName(), this);
    dlg.setOptions(opts);
    dlg.setHasNonWhiteBackgrounds(m_themeComposer->currentPalette().hasNonWhiteBackgrounds());
    if (dlg.exec() != QDialog::Accepted)
        return;

    opts = dlg.options();

    // Save global defaults to KConfig
    settings->setPdfAuthor(opts.author);
    settings->setPdfMarkdownCopy(opts.markdownCopy);
    settings->setPdfUnwrapParagraphs(opts.unwrapParagraphs);
    settings->setPdfIncludeBookmarks(opts.includeBookmarks);
    settings->setPdfBookmarkMaxDepth(opts.bookmarkMaxDepth);
    settings->setPdfInitialView(static_cast<int>(opts.initialView));
    settings->setPdfPageLayout(static_cast<int>(opts.pageLayout));
    settings->save();

    // Save per-document options to MetadataStore
    QJsonObject docOpts;
    docOpts[QStringLiteral("title")] = opts.title;
    docOpts[QStringLiteral("author")] = opts.author;
    docOpts[QStringLiteral("subject")] = opts.subject;
    docOpts[QStringLiteral("keywords")] = opts.keywords;
    docOpts[QStringLiteral("pageRangeExpr")] = opts.pageRangeExpr;
    QJsonArray excludedArr;
    for (int idx : opts.excludedHeadingIndices)
        excludedArr.append(idx);
    docOpts[QStringLiteral("excludedHeadingIndices")] = excludedArr;
    m_metadataStore->setValue(filePath, QStringLiteral("pdfExportOptions"), docOpts);

    // File save dialog
    QString path = QFileDialog::getSaveFileName(
        this, i18n("Export as PDF"),
        QString(), i18n("PDF Files (*.pdf)"));
    if (path.isEmpty())
        return;

    // Filter content by excluded sections
    Content::Document filteredDoc = contentDoc;
    if (opts.sectionsModified && !opts.excludedHeadingIndices.isEmpty())
        filteredDoc = ContentFilter::filterSections(contentDoc, opts.excludedHeadingIndices);

    // Substitute TTF font families with Hershey equivalents before layout
    if (opts.useHersheyFonts) {
        const TypeSet &typeSet = m_themeComposer->currentTypeSet();

        auto substituteStyle = [&typeSet](Content::TextStyle &style) {
            QString hershey = typeSet.hersheyFamilyFor(style.fontFamily);
            if (!hershey.isEmpty())
                style.fontFamily = hershey;
        };

        auto substituteInlines = [&substituteStyle](QList<Content::InlineNode> &inlines) {
            for (auto &node : inlines) {
                std::visit([&](auto &n) {
                    using T = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<T, Content::TextRun>)
                        substituteStyle(n.style);
                    else if constexpr (std::is_same_v<T, Content::InlineCode>)
                        substituteStyle(n.style);
                    else if constexpr (std::is_same_v<T, Content::Link>)
                        substituteStyle(n.style);
                    else if constexpr (std::is_same_v<T, Content::Math>)
                        substituteStyle(n.style);
                    else if constexpr (std::is_same_v<T, Content::FootnoteRef>)
                        substituteStyle(n.style);
                }, node);
            }
        };

        std::function<void(QList<Content::BlockNode> &)> substituteBlocks;
        substituteBlocks = [&](QList<Content::BlockNode> &blocks) {
            for (auto &block : blocks) {
                std::visit([&](auto &b) {
                    using T = std::decay_t<decltype(b)>;
                    if constexpr (std::is_same_v<T, Content::Paragraph>) {
                        substituteInlines(b.inlines);
                    } else if constexpr (std::is_same_v<T, Content::Heading>) {
                        substituteInlines(b.inlines);
                    } else if constexpr (std::is_same_v<T, Content::CodeBlock>) {
                        substituteStyle(b.style);
                    } else if constexpr (std::is_same_v<T, Content::BlockQuote>) {
                        substituteBlocks(b.children);
                    } else if constexpr (std::is_same_v<T, Content::List>) {
                        for (auto &item : b.items)
                            substituteBlocks(item.children);
                    } else if constexpr (std::is_same_v<T, Content::Table>) {
                        for (auto &row : b.rows)
                            for (auto &cell : row.cells) {
                                substituteStyle(cell.style);
                                substituteInlines(cell.inlines);
                            }
                    } else if constexpr (std::is_same_v<T, Content::FootnoteSection>) {
                        for (auto &fn : b.footnotes) {
                            substituteStyle(fn.numberStyle);
                            substituteStyle(fn.textStyle);
                            substituteBlocks(fn.blocks);
                        }
                    }
                }, block);
            }
        };

        substituteBlocks(filteredDoc.blocks);
    }

    // Layout with filtered content
    m_fontManager->resetUsage();
    Layout::Engine layoutEngine(m_fontManager, m_textShaper);
    layoutEngine.setHyphenateJustifiedText(
        PenelopeSettings::self()->hyphenateJustifiedText());
    if (opts.markdownCopy)
        layoutEngine.setMarkdownDecorations(true);
    Layout::LayoutResult layoutResult = layoutEngine.layout(filteredDoc, pl);

    // Filter pages by range
    if (opts.pageRangeModified && !opts.pageRangeExpr.isEmpty()) {
        auto rangeResult = PageRangeParser::parse(opts.pageRangeExpr, layoutResult.pages.size());
        if (rangeResult.valid && rangeResult.pages.size() < layoutResult.pages.size()) {
            QList<Layout::Page> filteredPages;
            for (int i = 0; i < layoutResult.pages.size(); ++i) {
                if (rangeResult.pages.contains(i + 1))  // 1-based
                    filteredPages.append(layoutResult.pages[i]);
            }
            layoutResult.pages = filteredPages;
            // Renumber pages
            for (int i = 0; i < layoutResult.pages.size(); ++i)
                layoutResult.pages[i].pageNumber = i;
        }
    }

    // Generate PDF with options
    PdfGenerator pdfGen(m_fontManager);
    pdfGen.setDocumentInfo(fi.fileName(), fi.baseName());
    pdfGen.setMaxJustifyGap(settings->maxJustifyGap());
    pdfGen.setExportOptions(opts);
    if (pdfGen.generateToFile(layoutResult, pl, fi.baseName(), path)) {
        statusBar()->showMessage(i18n("Exported to %1", path), 3000);
    } else {
        statusBar()->showMessage(i18n("Failed to export PDF."), 3000);
    }

}

void MainWindow::onFileExportRtf()
{
    auto *view = currentDocumentView();
    if (!view) {
        statusBar()->showMessage(i18n("No document to export."), 3000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, i18n("Export as RTF"),
        QString(), i18n("RTF Files (*.rtf)"));
    if (path.isEmpty())
        return;

    auto *tab = currentDocumentTab();
    bool success = false;
    if (tab) {
        ContentRtfExporter exporter;
        const auto content = buildCompositionContent(compositionRequest(tab));
        const QByteArray data = exporter.exportBlocks(content.blocks, RtfFilterOptions(),
                                                    content.footnoteStyle);
        QSaveFile file(path);
        success = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    }
    if (success) {
        statusBar()->showMessage(i18n("Exported to %1", path), 3000);
    } else {
        statusBar()->showMessage(i18n("Failed to export RTF."), 3000);
    }
}

void MainWindow::onFilePrint()
{
    auto *view = currentDocumentView();
    auto *tab = currentDocumentTab();
    if (!view || !tab) {
        statusBar()->showMessage(i18n("No document to print."), 3000);
        return;
    }

    PageLayout pl = m_pageDockWidget->currentPageLayout();
    auto makePdf = [this, tab](const PageLayout &layout) {
        m_fontManager->resetUsage();
        Layout::Engine engine(m_fontManager, m_textShaper);
        engine.setHyphenateJustifiedText(PenelopeSettings::self()->hyphenateJustifiedText());
        const auto laidOut = engine.layout(buildCompositionContent(compositionRequest(tab)), layout);
        PdfGenerator generator(m_fontManager);
        const QFileInfo fi(tab->filePath());
        generator.setDocumentInfo(fi.fileName(), fi.baseName());
        generator.setMaxJustifyGap(PenelopeSettings::self()->maxJustifyGap());
        return generator.generate(laidOut, layout, fi.baseName());
    };

    PrintController controller(this);
    controller.setPageLayout(pl);
    QString error;
    if (!controller.print(this, makePdf, &error)) {
        if (!error.isEmpty())
            statusBar()->showMessage(error, 4000);
        return;
    }
    statusBar()->showMessage(i18n("Sent to the printer."), 3000);
}

void MainWindow::onFileClose()
{
    int index = m_tabWidget->currentIndex();
    if (index >= 0) {
        onTabCloseRequested(index);
    }
}

void MainWindow::onCompositionApplied()
{
    ++m_compositionGeneration;

    // Compose a fresh StyleManager and seed the style dock with it
    auto *sm = new StyleManager(this);
    m_themeComposer->compose(sm);
    m_typeDockWidget->populateFromStyleManager(sm);
    delete sm;

    // Page layout is driven by template selection + manual PageLayoutWidget edits.
    // Here we only update the page background from the palette.
    QColor pageBg = m_themeComposer->currentPalette().pageBackground();
    if (pageBg.isValid()) {
        PageLayout pl = m_pageDockWidget->currentPageLayout();
        pl.pageBackground = pageBg;
        m_pageDockWidget->setPageLayout(pl);
        auto *view = currentDocumentView();
        if (view)
            view->setPageLayout(pl);
    }

    rebuildCurrentDocument();
}

void MainWindow::onStyleOverrideChanged()
{
    ++m_compositionGeneration;

    // Update page background from the current palette
    QColor pageBg = m_themeComposer->currentPalette().pageBackground();
    if (pageBg.isValid()) {
        PageLayout pl = m_pageDockWidget->currentPageLayout();
        pl.pageBackground = pageBg;
        m_pageDockWidget->setPageLayout(pl);
        auto *view = currentDocumentView();
        if (view)
            view->setPageLayout(pl);
    }

    rebuildCurrentDocument();
}


namespace {

// The page a source line falls on, from a paginated source map.
int pageForSourceLine(const QList<Layout::SourceMapEntry> &map, int line)
{
    if (line <= 0 || map.isEmpty())
        return 1;
    int page = map.first().pageNumber + 1;
    for (const auto &entry : map) {
        if (entry.startLine <= line && line <= entry.endLine)
            return entry.pageNumber + 1;
        if (entry.startLine <= line)
            page = entry.pageNumber + 1;
    }
    return page;
}

} // namespace

// Keep the page indicator tied to the current tab's scroll position.
void MainWindow::attachPageIndicator(DocumentTab *tab)
{
    if (m_pageIndicatorScroll)
        disconnect(m_pageIndicatorScroll);
    m_pageIndicatorScroll = {};
    if (!tab)
        return;
    auto *view = tab->documentView();
    if (!view)
        return;
    m_pageIndicatorScroll = connect(view->verticalScrollBar(), &QScrollBar::valueChanged,
                                    this, &MainWindow::refreshPageIndicator);
}

void MainWindow::refreshPageIndicator()
{
    if (!m_pageIndicator)
        return;
    auto *tab = currentDocumentTab();
    if (!tab || tab->isSourceMode()) {
        m_pageIndicator->clear();
        return;
    }
    auto *view = tab->documentView();
    if (!view) {
        m_pageIndicator->clear();
        return;
    }

    if (view->renderMode() == DocumentView::PrintMode) {
        const int page = qMax(1, view->currentPage() + 1);
        const int total = qMax(page, view->pageCount());
        m_pageIndicator->setText(i18n("Page %1 / %2", page, total));
        return;
    }

    // Continuous reading has no pages of its own; the printed sequence comes
    // from the background pagination.
    const PageLayout layout = m_pageDockWidget->currentPageLayout();
    if (!headerFooterUsesPageToken(layout)) {
        m_pageIndicator->clear();
        return;
    }
    const int page = pageForSourceLine(m_pageOracleMap, view->viewportAnchor().sourceLine);
    if (m_pageOracleTotal > 0)
        m_pageIndicator->setText(i18n("Page %1 / %2", page, m_pageOracleTotal));
    else
        m_pageIndicator->setText(i18n("Page %1", page));
}

void MainWindow::schedulePageOracle()
{
    if (m_pageOracleDebounce)
        m_pageOracleDebounce->start();
}

void MainWindow::startPageOracle()
{
    auto *tab = currentDocumentTab();
    if (!tab || tab->isSourceMode() || !m_pageOracle) {
        m_pageOracleTotal = 0;
        m_pageOracleMap.clear();
        refreshPageIndicator();
        return;
    }
    const PageLayout layout = m_pageDockWidget->currentPageLayout();
    if (!headerFooterUsesPageToken(layout)) {
        m_pageOracleTotal = 0;
        m_pageOracleMap.clear();
        refreshPageIndicator();
        return;
    }

    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(tab->filePath())
                            .arg(tab->sourceRevision())
                            .arg(m_compositionGeneration)
                            .arg(QString::number(layout.pageSizePoints().width(), 'f', 2)
                                 + QString::number(layout.margins.left(), 'f', 2)
                                 + QString::number(layout.margins.top(), 'f', 2)
                                 + QString::number(layout.margins.right(), 'f', 2)
                                 + QString::number(layout.margins.bottom(), 'f', 2)
                                 + QString::number(layout.masterPages.size()));
    if (key == m_pageOracleKey && m_pageOracleTotal > 0) {
        refreshPageIndicator();
        return;
    }

    CompositionRequest request = compositionRequest(tab);
    request.continuous = false;
    request.onePage = false;
    request.interactivePages = false;
    request.startBlock = 0;
    request.skipPdf = true;
    m_pageOracleKey = key;
    m_pageOracleTotal = 0;
    m_pageOracleMap.clear();
    m_pageOracleRequest = m_pageOracle->submit(request);
    refreshPageIndicator();
}

void MainWindow::onPageLayoutChanged()
{
    ++m_compositionGeneration;
    schedulePageOracle();

    PageLayout pl = m_pageDockWidget->currentPageLayout();
    auto *view = currentDocumentView();
    if (view)
        view->setPageLayout(pl);
    rebuildCurrentDocument();
}

void MainWindow::onZoomIn()
{
    auto *view = currentDocumentView();
    if (view)
        view->zoomIn();
}

void MainWindow::onZoomOut()
{
    auto *view = currentDocumentView();
    if (view)
        view->zoomOut();
}

void MainWindow::onFitWidth()
{
    auto *view = currentDocumentView();
    if (view)
        view->fitWidth();
}

void MainWindow::onFitPage()
{
    auto *view = currentDocumentView();
    if (view)
        view->fitPage();
}


DocumentView *MainWindow::currentDocumentView() const
{
    auto *tab = currentDocumentTab();
    return tab ? tab->documentView() : nullptr;
}

DocumentTab *MainWindow::currentDocumentTab() const
{
    int index = m_tabWidget->currentIndex();
    if (index < 0)
        return nullptr;
    return qobject_cast<DocumentTab *>(m_tabWidget->widget(index));
}

void MainWindow::showPreferences()
{
    if (KConfigDialog::showDialog(QStringLiteral("settings")))
        return;

    auto *dialog = new PenelopeConfigDialog(this);
    connect(dialog, &KConfigDialog::settingsChanged,
            this, &MainWindow::onSettingsChanged);
    dialog->show();
}

void MainWindow::onSettingsChanged()
{
    auto *settings = PenelopeSettings::self();

    // Reconfigure hyphenator
    if (settings->hyphenationEnabled() || settings->hyphenateJustifiedText()) {
        m_hyphenator->loadDictionary(settings->hyphenationLanguage());
        m_hyphenator->setMinWordLength(settings->hyphenationMinWordLength());
    }

    // Reconfigure short words
    if (settings->shortWordsEnabled())
        m_shortWords->setLanguage(settings->hyphenationLanguage());

    ++m_compositionGeneration;
    rebuildCurrentDocument();
}

void MainWindow::onRenderModeChanged()
{
    bool webMode = PenelopeSettings::self()->useWebView();
    bool printMode = !webMode;

    // Show/hide template picker based on render mode
    if (m_themePickerDock)
        m_themePickerDock->setRenderMode(printMode);

    // Fit Width and Page Arrangement only make sense in Print view
    if (m_fitWidthAction)
        m_fitWidthAction->setEnabled(printMode);
    if (m_pageArrangementMenu)
        m_pageArrangementMenu->setEnabled(printMode);

    auto *view = currentDocumentView();
    if (view)
        view->setRenderMode(webMode ? DocumentView::WebMode : DocumentView::PrintMode);

    ++m_compositionGeneration;
    rebuildCurrentDocument();
}

void MainWindow::saveSession()
{
    KConfigGroup group(KSharedConfig::openConfig(),
                       QStringLiteral("Session"));

    // Save sidebar state
    group.writeEntry("LeftSidebarCollapsed", m_leftSidebar->isCollapsed());
    group.writeEntry("RightSidebarCollapsed", m_rightSidebar->isCollapsed());
    // Save which left panel was active (Files=0, TOC=1) so we restore the right one
    if (!m_leftSidebar->isCollapsed()) {
        if (m_leftSidebar->isPanelVisible(m_tocTabId))
            group.writeEntry("LeftActivePanel", QStringLiteral("toc"));
        else
            group.writeEntry("LeftActivePanel", QStringLiteral("files"));
    }
    if (!m_rightSidebar->isCollapsed()) {
        if (m_rightSidebar->isPanelVisible(m_themePickerTabId))
            group.writeEntry("RightActivePanel", QStringLiteral("theme"));
        else if (m_rightSidebar->isPanelVisible(m_typeTabId))
            group.writeEntry("RightActivePanel", QStringLiteral("type"));
        else if (m_rightSidebar->isPanelVisible(m_colorTabId))
            group.writeEntry("RightActivePanel", QStringLiteral("color"));
        else
            group.writeEntry("RightActivePanel", QStringLiteral("page"));
    }
    if (m_splitter)
        group.writeEntry("SplitterSizes", m_splitter->sizes());

    // Save open files and per-tab view state
    QStringList openFiles;
    QList<int> zoomLevels;
    QList<int> scrollPages;
    QStringList scrollFractions;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
        if (!tab || tab->filePath().isEmpty())
            continue;
        saveTabViewState(tab);
        openFiles << tab->filePath();
        ViewState vs = tab->documentView()->saveViewState();
        zoomLevels << vs.zoomPercent;
        scrollPages << vs.currentPage;
        scrollFractions << QString::number(vs.scrollFraction, 'g', 10);
    }
    group.writeEntry("OpenFiles", openFiles);
    group.writeEntry("ActiveTab", m_tabWidget->currentIndex());
    group.writeEntry("ZoomLevels", zoomLevels);
    group.writeEntry("ScrollPages", scrollPages);
    group.writeEntry("ScrollFractions", scrollFractions);

    // Save current type set + color scheme + page template from editing docks
    group.writeEntry("TypeSet", m_typeDockWidget->currentTypeSetId());
    group.writeEntry("ColorScheme", m_colorDockWidget->currentPaletteId());
    group.writeEntry("PageTemplate", m_pageDockWidget->currentTemplateId());

    group.sync();
}

void MainWindow::activateWithFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile())
            openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
    }
    raise();
    activateWindow();
}

void MainWindow::restoreOpenFiles()
{
    KConfigGroup group(KSharedConfig::openConfig(),
                       QStringLiteral("Session"));

    QStringList openFiles = group.readEntry("OpenFiles", QStringList());
    if (openFiles.isEmpty())
        return;

    for (const QString &path : openFiles) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile())
            openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
    }

    // Restore active tab
    int activeTab = group.readEntry("ActiveTab", 0);
    const QString activePath = openFiles.value(activeTab);
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
        if (tab && tab->filePath() == activePath) m_tabWidget->setCurrentIndex(i);
    }

    // Defer view state restoration until layouts are computed
    QList<int> zoomLevels = group.readEntry("ZoomLevels", QList<int>());
    QList<int> scrollPages = group.readEntry("ScrollPages", QList<int>());
    QStringList fracStrings = group.readEntry("ScrollFractions", QStringList());

    QTimer::singleShot(0, this, [this, openFiles, zoomLevels, scrollPages, fracStrings]() {
        for (int i = 0; i < m_tabWidget->count() && i < zoomLevels.size(); ++i) {
            auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
            if (!tab)
                continue;
            int savedIndex = openFiles.indexOf(tab->filePath());
            if (savedIndex < 0) continue;
            ViewState vs;
            vs.zoomPercent = zoomLevels.value(savedIndex, 100);
            vs.currentPage = scrollPages.value(savedIndex, 0);
            vs.scrollFraction = fracStrings.value(savedIndex).toDouble();
            vs.valid = true;
            if (!tab->hasTocData()) tab->setInitialViewState(vs);
            tab->documentView()->restoreViewState(vs);
        }
    });
}

void MainWindow::restoreSession()
{
    KConfigGroup group(KSharedConfig::openConfig(),
                       QStringLiteral("Session"));

    // Restore sidebar state
    bool leftCollapsed = group.readEntry("LeftSidebarCollapsed", true);
    bool rightCollapsed = group.readEntry("RightSidebarCollapsed", true);

    // Expand sidebars that were open last session (unlocks width constraints)
    if (!leftCollapsed) {
        QString leftPanel = group.readEntry("LeftActivePanel", QStringLiteral("toc"));
        if (leftPanel == QLatin1String("files"))
            m_leftSidebar->showPanel(m_filesBrowserTabId);
        else
            m_leftSidebar->showPanel(m_tocTabId);
    }
    if (!rightCollapsed) {
        QString rightPanel = group.readEntry("RightActivePanel", QStringLiteral("type"));
        if (rightPanel == QLatin1String("theme"))
            m_rightSidebar->showPanel(m_themePickerTabId);
        else if (rightPanel == QLatin1String("color"))
            m_rightSidebar->showPanel(m_colorTabId);
        else if (rightPanel == QLatin1String("page"))
            m_rightSidebar->showPanel(m_pageTabId);
        else // "type" or legacy "style"
            m_rightSidebar->showPanel(m_typeTabId);
    }

    // Restore splitter proportions from last session
    QList<int> splitterSizes = group.readEntry("SplitterSizes", QList<int>());
    if (splitterSizes.size() == 3 && m_splitter) {
        // Validate: expanded sidebars need meaningful width
        if (!leftCollapsed && splitterSizes[0] < 100)
            splitterSizes[0] = 250;
        if (!rightCollapsed && splitterSizes[2] < 100)
            splitterSizes[2] = 250;
        m_splitter->setSizes(splitterSizes);
    }

    // Restore type set + color scheme (with backward compat for old session key)
    QString typeSetId = group.readEntry("TypeSet", QString());
    if (typeSetId.isEmpty())
        typeSetId = group.readEntry("TypographyTheme", QStringLiteral("default"));
    QString colorId = group.readEntry("ColorScheme", QStringLiteral("default-light"));

    bool changed = false;
    TypeSet ts = m_typeSetManager->typeSet(typeSetId);
    if (!ts.id.isEmpty()) {
        m_themeComposer->setTypeSet(ts);
        changed = true;
    }

    ColorPalette palette = m_paletteManager->palette(colorId);
    if (!palette.id.isEmpty()) {
        m_themeComposer->setColorPalette(palette);
        changed = true;
    }

    m_themePickerDock->syncPickersFromComposer();

    // Sync editing dock dropdowns
    if (!typeSetId.isEmpty())
        m_typeDockWidget->setCurrentTypeSetId(typeSetId);
    if (!colorId.isEmpty())
        m_colorDockWidget->setCurrentPaletteId(colorId);

    // Restore page template selection
    QString templateId = group.readEntry("PageTemplate", QString());
    if (!templateId.isEmpty()) {
        m_themePickerDock->setCurrentTemplateId(templateId);
        m_pageDockWidget->setCurrentTemplateId(templateId);
    }

    if (changed)
        onCompositionApplied();
}

namespace {

qreal compositionWidth(const DocumentView *view)
{
    const qreal zoom = qMax(0.01, view->zoomPercent() / 100.0);
    const auto *bar = view->verticalScrollBar();
    // The viewport shrinks when the bar is shown. Add that back, then reserve
    // the bar once, so showing it does not change the column width.
    qreal viewport = view->viewport()->width();
    const qreal reserve = bar ? bar->sizeHint().width() : 0;
    if (bar && bar->isVisible())
        viewport += bar->width();
    return qMax(qreal(200), (viewport - 2 * DocumentView::kSceneMargin - reserve) / zoom);
}

qreal viewportSceneHeight(const DocumentView *view)
{
    const qreal zoom = qMax(0.01, view->zoomPercent() / 100.0);
    return view->viewport()->height() / zoom;
}

bool pageGeometryDiffers(const PageLayout &a, const PageLayout &b)
{
    return a.pageSizeId != b.pageSizeId || a.orientation != b.orientation
        || a.margins != b.margins || a.headerEnabled != b.headerEnabled
        || a.footerEnabled != b.footerEnabled;
}

} // namespace

CompositionRequest MainWindow::compositionRequest(DocumentTab *tab) const
{
    CompositionRequest request;
    request.markdown = tab->sourceText();
    QFileInfo fi(tab->filePath());
    request.basePath = fi.absolutePath();
    request.filename = fi.fileName();
    request.title = fi.baseName();
    request.sourceRevision = tab->sourceRevision();
    request.semanticRevision = m_compositionGeneration;
    std::unique_ptr<StyleManager> styles;
    if (auto *editing = m_typeDockWidget->currentStyleManager()) styles.reset(editing->clone());
    else {
        styles = std::make_unique<StyleManager>();
        m_themeComposer->compose(styles.get());
    }
    request.paragraphs = styles->paragraphStyles();
    request.characters = styles->characterStyles();
    request.tables = styles->tableStyles();
    request.footnotes = styles->footnoteStyle();
    request.smartPunctuation = PenelopeSettings::self()->smartPunctuationEnabled();
    request.pageLayout = m_pageDockWidget->currentPageLayout();
    auto *view = tab->documentView();
    request.languageOverrides = view->codeBlockLanguageOverrides();
    request.width = compositionWidth(view);
    auto *settings = PenelopeSettings::self();
    request.continuous = settings->useWebView();
    request.renderImages = settings->renderImages();
    request.hyphenation = settings->hyphenationEnabled();
    request.hyphenateJustified = settings->hyphenateJustifiedText();
    request.shortWords = settings->shortWordsEnabled();
    request.language = settings->hyphenationLanguage();
    request.minWordLength = settings->hyphenationMinWordLength();
    request.optimalTables = settings->tableLayoutAlgorithm() == PenelopeSettings::EnumTableLayoutAlgorithm::Optimal;
    request.highlightCode = settings->syntaxHighlightingEnabled();
    request.codeTheme = settings->codeHighlightTheme();
    request.maxJustifyGap = settings->maxJustifyGap();
    return request;
}

void MainWindow::installComposition(quint64 id, CompositionResultPtr result)
{
    auto *tab = m_renderTarget.data();
    if (!tab || id != m_renderRequest || tab != currentDocumentTab() || tab->isSourceMode()) return;
    const auto &request = result->request;
    if (tab->sourceRevision() != request.sourceRevision
        || m_compositionGeneration != request.semanticRevision
        || PenelopeSettings::self()->useWebView() != request.continuous) return;
    auto *view = tab->documentView();
    if (request.continuous) {
        const qreal width = compositionWidth(view);
        if (qAbs(width - request.width) > 0.01) { rebuildCurrentDocument(); return; }
    }
    bool continueWeb = request.continuous && request.startBlock > 0
        && request.startBlock == view->layoutCursor().nextBlock && view->webViewItem();
    if (continueWeb) {
        QSet<quint64> placed;
        for (const auto &element : view->webViewItem()->layoutResult().elements) {
            std::visit([&](const auto &item) {
                if (item.blockId != 0)
                    placed.insert(item.blockId);
            }, element);
        }
        for (const auto &element : result->continuousLayout.elements) {
            quint64 id = 0;
            std::visit([&](const auto &item) { id = item.blockId; }, element);
            if (id != 0 && placed.contains(id)) {
                continueWeb = false;
                break;
            }
        }
    }
    // A partial chunk that does not extend the column must not replace it.
    // Installing that tail drops everything above it and leaves nothing to scroll.
    if (request.continuous && request.startBlock > 0 && !continueWeb) {
        rebuildCurrentDocument();
        return;
    }
    // Capture the latest scroll/zoom, not the state from when the job began.
    ViewState state = tab->takeInitialViewState().value_or(view->saveViewState());
    state.zoomPercent = view->zoomPercent();
    // The source line under the viewport survives re-layout; a bare scroll
    // fraction does not when the revision adds or removes text above it.
    const ScrollAnchor anchor = view->viewportAnchor();
    view->setPageLayout(request.pageLayout);
    const auto sourceMap = request.continuous ? result->continuousLayout.sourceMap : result->paginated.sourceMap;
    const auto regions = request.continuous ? result->continuousLayout.codeBlockRegions : result->paginated.codeBlockRegions;
    const bool continuePrint = !request.continuous && request.interactivePages && request.startBlock > 0
        && request.startBlock == view->layoutCursor().nextBlock && view->hasBoxPages();
    if (request.continuous) {
        view->setWebFontOwner(result->fonts);
        if (continueWeb)
            view->appendWebContent(std::move(result->continuousLayout));
        else
            view->setWebContent(std::move(result->continuousLayout));
        view->setRenderMode(DocumentView::WebMode);
    } else if (request.interactivePages) {
        QList<std::shared_ptr<const Layout::Page>> pages;
        for (const auto &page : result->paginated.pages)
            pages.append(std::make_shared<const Layout::Page>(page));
        view->setBoxPages(pages, result->fonts, request.pageLayout, continuePrint,
                          request.filename, request.title);
        view->setRenderMode(DocumentView::PrintMode);
    } else {
        view->setPdfData(result->pdf);
        view->setRenderMode(DocumentView::PrintMode);
    }
    if (continueWeb) {
        view->setSourceData(result->processedMarkdown, view->webViewItem()->layoutResult().sourceMap,
                            result->document, view->webViewItem()->layoutResult().codeBlockRegions);
    } else if (continuePrint) {
        auto map = view->sourceMap();
        map.append(sourceMap);
        auto regionList = regions;
        view->setSourceData(result->processedMarkdown, map, result->document, regionList);
    } else {
        view->setSourceData(result->processedMarkdown, sourceMap, result->document, regions);
    }
    // Headings use the column or pages installed above. A follow-up chunk's
    // own map starts at y = 0; the view's map has the absolute positions.
    const auto &placedMap = view->sourceMap();
    m_tocWidget->buildFromContentModel(result->document, placedMap);
    tab->setTocData(result->document, placedMap);
    QList<HeadingPosition> headings;
    QHash<quint64, Layout::SourceMapEntry> bySource;
    for (const auto &entry : placedMap) {
        const quint64 key = (quint64(entry.startLine) << 32) | quint32(entry.endLine);
        if (!bySource.contains(key)) bySource.insert(key, entry);
    }
    for (const auto &block : result->document.blocks) {
        const auto *heading = std::get_if<Content::Heading>(&block);
        if (!heading || heading->level < 1 || heading->level > 6) continue;
        const auto entry = bySource.constFind((quint64(heading->source.startLine) << 32)
                                             | quint32(heading->source.endLine));
        if (entry == bySource.cend()) continue;
        HeadingPosition hp;
        hp.sourceLine = heading->source.startLine;
        hp.page = entry->pageNumber;
        hp.yOffset = entry->rect.top();
        headings.append(hp);
    }
    view->integrateCursor(result->cursor);
    view->setHeadingPositions(headings);
    if (!continueWeb && !continuePrint)
        view->restoreViewState(state, anchor);
    tab->setCompositionGeneration(request.semanticRevision);
    const int shownPages = request.continuous ? 0 : view->pageCount();
    if (request.continuous)
        statusBar()->showMessage(i18n("Rendered %1", request.filename), 2000);
    else if (result->finished)
        statusBar()->showMessage(i18n("Rendered %1 (%2 pages)", request.filename, shownPages), 2000);
    else
        statusBar()->showMessage(i18n("Rendered %1 (%2 pages so far)", request.filename, shownPages), 2000);
    if (!result->finished && tab == currentDocumentTab())
        submitLayoutFollowUp();
    if (request.continuous && !result->finished)
        view->scheduleMoreLayout();
    schedulePageOracle();
    refreshPageIndicator();
    Q_EMIT documentRendered(tab);
}

void MainWindow::submitLayoutFollowUp()
{
    if (!m_compositionController || m_compositionController->isBusy() || m_compositionController->pendingCount())
        return;
    auto *tab = currentDocumentTab();
    if (!tab || tab->isSourceMode() || tab != m_renderTarget.data())
        return;
    auto *view = tab->documentView();
    if (view->layoutCursor().finished)
        return;
    CompositionRequest request = compositionRequest(tab);
    request.startBlock = view->layoutCursor().nextBlock;
    if (request.continuous) {
        request.maxHeight = viewportSceneHeight(view);
    } else {
        request.onePage = true;
        request.interactivePages = true;
        request.geometryChanged = false;
    }
    m_renderRequest = m_compositionController->submit(request);
}

void MainWindow::rebuildCurrentDocument()
{
    auto *tab = currentDocumentTab();
    if (!tab)
        return;

    // Apply palette colours to source editor
    const auto &palette = m_themeComposer->currentPalette();
    auto *editor = tab->sourceEditor();
    QPalette pal = editor->palette();
    pal.setColor(QPalette::Base, palette.pageBackground());
    pal.setColor(QPalette::Text, palette.text());
    if (editor->palette() != pal)
        editor->setPalette(pal);

    tab->markdownHighlighter()->setPaletteColors(
        palette.headingText(),
        palette.codeText(),
        palette.surfaceCode(),
        palette.surfaceInlineCode(),
        palette.borderInner());

    QString filePath = tab->filePath();
    if (filePath.isEmpty())
        return;

    if (tab->isSourceMode()) {
        tab->setCompositionGeneration(0);
        m_compositionController->cancel();
        m_renderTarget.clear();
        return;
    }
    m_renderTarget = tab;
    CompositionRequest request = compositionRequest(tab);
    if (request.continuous) {
        auto *view = tab->documentView();
        const auto *vp = view->viewport();
        // The window has not been laid out yet. A column measured at a few
        // dozen pixels is replaced as soon as the real size arrives, and the
        // two results flash on top of each other.
        if (view->isVisible() && vp && (vp->width() < 160 || vp->height() < 120)) {
            view->scheduleWebRelayout();
            return;
        }
        const qreal viewport = viewportSceneHeight(view);
        request.maxHeight = viewport * 2.0;
        request.startBlock = 0;
        m_webLayoutWidth = request.width;
    } else {
        request.onePage = true;
        request.interactivePages = true;
        request.geometryChanged = m_hasPageGeometry && pageGeometryDiffers(m_lastPageGeometry, request.pageLayout);
        m_lastPageGeometry = request.pageLayout;
        m_hasPageGeometry = true;
    }
    m_renderRequest = m_compositionController->submit(request);
    statusBar()->showMessage(i18n("Rendering %1…", QFileInfo(filePath).fileName()));

}

void MainWindow::openFile(const QUrl &url)
{
    if (!url.isLocalFile())
        return;

    const QFileInfo requestedFile(url.toLocalFile());
    const QString filePath = requestedFile.canonicalFilePath().isEmpty()
        ? requestedFile.absoluteFilePath() : requestedFile.canonicalFilePath();

    // Check if already open
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (m_tabWidget->tabToolTip(i) == filePath) {
            m_tabWidget->setCurrentIndex(i);
            return;
        }
    }

    // Read file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage(
            i18n("Failed to open %1", filePath), 5000);
        return;
    }
    const QString markdown = QString::fromUtf8(file.readAll());
    file.close();

    auto *tab = new DocumentTab(this);
    tab->setFilePath(filePath);
    tab->setSourceText(markdown);
    PageLayout openPl = m_pageDockWidget->currentPageLayout();
    tab->documentView()->setPageLayout(openPl);

    // Zoom is applied before web mode. In web mode a zoom change schedules a
    // new column, and doing that on top of the open rebuild flashes two copies.
    const int savedZoom = m_metadataStore->value(filePath, QStringLiteral("viewState")).toObject()
        .value(QStringLiteral("zoom")).toInt(qRound(PenelopeSettings::self()->defaultZoom() * 100));
    tab->documentView()->setZoomPercent(savedZoom);
    const bool webMode = PenelopeSettings::self()->useWebView();
    if (webMode)
        tab->documentView()->setRenderMode(DocumentView::WebMode);

    QFileInfo fi(filePath);

    // Load persisted code block language overrides from MetadataStore
    QJsonValue langVal = m_metadataStore->value(filePath, QStringLiteral("codeBlockLanguages"));
    if (langVal.isObject()) {
        QJsonObject langObj = langVal.toObject();
        QHash<QString, QString> overrides;
        for (auto it = langObj.begin(); it != langObj.end(); ++it)
            overrides.insert(it.key(), it.value().toString());
        tab->documentView()->setCodeBlockLanguageOverrides(overrides);
    }


    // Apply saved view mode from settings
    auto settingsViewMode = PenelopeSettings::self()->viewMode();
    DocumentView::ViewMode dvMode = DocumentView::Continuous;
    switch (settingsViewMode) {
    case PenelopeSettings::SinglePage:                dvMode = DocumentView::SinglePage; break;
    case PenelopeSettings::FacingPages:               dvMode = DocumentView::FacingPages; break;
    case PenelopeSettings::FacingPagesFirstAlone:     dvMode = DocumentView::FacingPagesFirstAlone; break;
    case PenelopeSettings::ContinuousFacing:          dvMode = DocumentView::ContinuousFacing; break;
    case PenelopeSettings::ContinuousFacingFirstAlone: dvMode = DocumentView::ContinuousFacingFirstAlone; break;
    default: break;
    }
    tab->documentView()->setViewMode(dvMode);

    // Connect zoom signal to status bar controls
    connect(tab->documentView(), &DocumentView::zoomChanged,
            this, [this, tab](int percent) {
        if (currentDocumentTab() != tab) return;
        if (m_zoomSpinBox) {
            const QSignalBlocker blocker(m_zoomSpinBox);
            m_zoomSpinBox->setValue(percent);
        }
        if (m_zoomSlider) {
            const QSignalBlocker blocker(m_zoomSlider);
            m_zoomSlider->setValue(percent);
        }
    });

    // A7: Connect hover hint signal to status bar
    connect(tab->documentView(), &DocumentView::statusHintChanged,
            this, [this, tab](const QString &hint) {
        if (currentDocumentTab() != tab) return;
        if (hint.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(hint);
    });

    // Code block language override: persist + rebuild
    connect(tab->documentView(), &DocumentView::codeBlockLanguageChanged,
            this, [this, filePath, tab]() {
        auto *view = tab->documentView();
        if (!view)
            return;
        // Persist overrides to MetadataStore as JSON
        QHash<QString, QString> overrides = view->codeBlockLanguageOverrides();
        QJsonObject langObj;
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            langObj.insert(it.key(), it.value());
        m_metadataStore->setValue(filePath, QStringLiteral("codeBlockLanguages"), langObj);
        tab->setCompositionGeneration(0);
        if (currentDocumentTab() == tab) rebuildCurrentDocument();
    });

    // Language picker dialog (decoupled from DocumentView → widgets)
    connect(tab->documentView(), &DocumentView::languageOverrideRequested,
            this, [this](const QString &codeKey, const QString &currentLang) {
        auto *view = currentDocumentView();
        if (!view)
            return;
        auto *dialog = new LanguagePickerDialog(currentLang, this);
        if (dialog->exec() == QDialog::Accepted) {
            QString lang = dialog->selectedLanguage();
            QHash<QString, QString> overrides = view->codeBlockLanguageOverrides();
            if (lang.isEmpty())
                overrides.remove(codeKey);
            else
                overrides.insert(codeKey, lang);
            view->setCodeBlockLanguageOverrides(overrides);
            Q_EMIT view->codeBlockLanguageChanged();
        }
        delete dialog;
    });

    // RTF copy options dialog (decoupled from DocumentView → widgets)
    connect(tab->documentView(), &DocumentView::rtfCopyOptionsRequested,
            this, [this]() {
        auto *view = currentDocumentView();
        if (!view)
            return;
        auto *dialog = new RtfCopyOptionsDialog(this);
        if (dialog->exec() == QDialog::Accepted) {
            RtfFilterOptions filter = dialog->filterOptions();
            view->copySelectionWithFilter(filter);
        }
        delete dialog;
    });

    // Source view reverse-sync: scrolling the editor highlights the current heading in ToC
    connect(tab->sourceEditor()->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() {
        auto *tab = currentDocumentTab();
        if (!tab || !tab->isSourceMode())
            return;
        auto *editor = tab->sourceEditor();
        // Find the first visible line (0-based block) → 1-based source line
        QTextCursor cursor = editor->cursorForPosition(QPoint(0, 0));
        int topLine = cursor.blockNumber() + 1; // 1-based
        auto *view = tab->documentView();
        const auto &hps = view->headingPositions();
        int bestSourceLine = -1;
        for (int i = hps.size() - 1; i >= 0; --i) {
            if (hps[i].sourceLine <= topLine) {
                bestSourceLine = hps[i].sourceLine;
                break;
            }
        }
        if (bestSourceLine < 0 && !hps.isEmpty())
            bestSourceLine = hps.first().sourceLine;
        if (bestSourceLine > 0)
            m_tocWidget->highlightHeading(bestSourceLine);
    });

    connect(tab->documentView(), &DocumentView::currentHeadingChanged, this, [this, tab](int line) {
        if (currentDocumentTab() == tab) m_tocWidget->highlightHeading(line);
    });
    auto *sourceTimer = new QTimer(tab);
    sourceTimer->setSingleShot(true);
    sourceTimer->setInterval(300);
    connect(tab, &DocumentTab::sourceChanged, this, [this, tab, sourceTimer]() {
        if (currentDocumentTab() == tab) m_compositionController->cancel();
        if (!tab->isSourceMode()) sourceTimer->start();
    });
    connect(sourceTimer, &QTimer::timeout, this, [this, tab]() {
        if (currentDocumentTab() == tab && !tab->isSourceMode()) rebuildCurrentDocument();
    });
    connect(tab->sourceEditor()->document(), &QTextDocument::modificationChanged,
            this, [this, tab, fi](bool modified) {
        int index = m_tabWidget->indexOf(tab);
        if (index >= 0) m_tabWidget->setTabText(index, fi.fileName() + (modified ? QStringLiteral(" *") : QString()));
    });
    connect(tab, &DocumentTab::reloadedFromDisk, this, [this, tab]() {
        statusBar()->showMessage(i18n("Reloaded %1", QFileInfo(tab->filePath()).fileName()), 4000);
    });
    connect(tab->documentView(), &DocumentView::webRelayoutRequested, this, [this, tab]() {
        if (currentDocumentTab() != tab || !PenelopeSettings::self()->useWebView()) {
            tab->setCompositionGeneration(0);
            return;
        }
        auto *view = tab->documentView();
        // Scrollbar show/hide resizes the viewport without changing the column
        // width. Rebuilding there replaces the column and snaps scroll to the top.
        if (view->webViewItem() && m_webLayoutWidth > 0
            && qAbs(compositionWidth(view) - m_webLayoutWidth) < 1.0)
            return;
        rebuildCurrentDocument();
    });
    connect(tab->documentView(), &DocumentView::moreLayoutRequested, this, [this, tab]() {
        if (currentDocumentTab() == tab)
            submitLayoutFollowUp();
    });

    int index = m_tabWidget->addTab(tab, fi.fileName());
    m_tabWidget->setTabToolTip(index, filePath);
    m_tabWidget->setCurrentIndex(index);


    QJsonObject savedView = m_metadataStore->value(filePath, QStringLiteral("viewState")).toObject();
    {
        ViewState vs;
        vs.valid = true;
        vs.zoomPercent = savedView.value(QStringLiteral("zoom")).toInt(qRound(PenelopeSettings::self()->defaultZoom() * 100));
        vs.currentPage = savedView.value(QStringLiteral("page")).toInt();
        vs.scrollFraction = savedView.value(QStringLiteral("scroll")).toDouble();
        tab->setInitialViewState(vs);
        tab->documentView()->restoreViewState(vs);
    }
    m_recentFilesAction->addUrl(QUrl::fromLocalFile(filePath));


    // Composition is stamped only when the asynchronous result installs.

    // A6: Update file path label in status bar
    if (m_filePathLabel)
        m_filePathLabel->setText(filePath);

    statusBar()->showMessage(i18n("Opened %1", fi.fileName()), 3000);
}

void MainWindow::saveTabViewState(DocumentTab *tab)
{
    if (!tab) return;
    ViewState state = tab->documentView()->saveViewState();
    QJsonObject obj;
    obj[QStringLiteral("zoom")] = state.zoomPercent;
    obj[QStringLiteral("page")] = state.currentPage;
    obj[QStringLiteral("scroll")] = state.scrollFraction;
    m_metadataStore->setValue(tab->filePath(), QStringLiteral("viewState"), obj);
}

bool MainWindow::saveTab(DocumentTab *tab)
{
    if (!tab) return false;
    if (tab->hasExternalChanges() && QMessageBox::question(this, i18n("File Changed"),
            i18n("%1 changed on disk. Replace it with your edits?", tab->filePath()),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save)
        return false;
    QString error;
    if (!tab->saveSource(&error)) {
        QMessageBox::warning(this, i18n("Save Failed"), error);
        return false;
    }
    return true;
}

void MainWindow::onFileSave()
{
    saveTab(currentDocumentTab());
}

bool MainWindow::confirmClose(DocumentTab *tab)
{
    if (!tab || !tab->isModified()) return true;
    auto answer = QMessageBox::warning(this, i18n("Unsaved Changes"),
        i18n("Save changes to %1?", tab->filePath()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || saveTab(tab);
}
