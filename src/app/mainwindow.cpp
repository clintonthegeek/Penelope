#include "mainwindow.h"

#include <KActionCollection>
#include <KActionMenu>
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
#include "codeblockhighlighter.h"
#include "documentbuilder.h"
#include "documenttab.h"
#include "documentview.h"
#include "filebrowserdock.h"
#include "hyphenator.h"
#include "markdownhighlighter.h"
#include "metadatastore.h"
#include "rtfexporter.h"
#include "shortwords.h"
#include "tocwidget.h"
#include "printcontroller.h"
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
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

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

        // Rebuild stale tabs (composition changed since last render)
        if (tab && tab->compositionGeneration() < m_compositionGeneration) {
            rebuildCurrentDocument();
        } else if (tab && tab->hasTocData()) {
            // Tab is current — just rebuild TOC from cached data
            m_tocWidget->buildFromContentModel(tab->cachedContentDoc(), tab->cachedSourceMap());
        } else if (tab && tab->documentView()->document()) {
            // Legacy path — rebuild TOC from QTextDocument
            m_tocWidget->buildFromDocument(tab->documentView()->document());
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

    restoreSession();

    // A1: Fresh launch = TOC open by default (saved session state takes priority)
    if (m_leftSidebar->isCollapsed())
        m_leftSidebar->showPanel(m_tocTabId);
}

MainWindow::~MainWindow()
{
    delete m_hyphenator;
    delete m_shortWords;
    delete m_textShaper;
    delete m_fontManager;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
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
    m_tabWidget->removeTab(index);

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

    connect(m_tocWidget, &TocWidget::headingClicked,
            this, [this](int blockNumber) {
        auto *view = currentDocumentView();
        if (!view || !view->document())
            return;
        QTextBlock block = view->document()->findBlockByNumber(blockNumber);
        if (block.isValid()) {
            QSizeF pageSize = view->document()->pageSize();
            if (pageSize.height() > 0) {
                auto *layout = view->document()->documentLayout();
                QRectF blockRect = layout->blockBoundingRect(block);
                int page = static_cast<int>(blockRect.top() / pageSize.height());
                view->goToPage(page);
            }
        }
    });

    connect(m_tocWidget, &TocWidget::headingNavigate,
            this, [this](int page, qreal yOffset) {
        auto *view = currentDocumentView();
        if (view)
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
        // Fit Width and Page Arrangement only apply to Print view
        if (m_fitWidthAction)
            m_fitWidthAction->setEnabled(false);
        if (m_pageArrangementMenu)
            m_pageArrangementMenu->setEnabled(false);
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
        auto *view = currentDocumentView();
        if (view) view->copySelection();
    }, ac);
    copyAction->setPriority(QAction::LowPriority);
    copyAction->setEnabled(false); // hand tool is default

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

    if (view->isPdfMode()) {
        auto *tab = currentDocumentTab();
        if (!tab)
            return;

        QString filePath = tab->filePath();
        QString markdown;
        if (tab->isSourceMode()) {
            markdown = tab->sourceText();
        } else {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return;
            markdown = QString::fromUtf8(file.readAll());
            file.close();
        }

        StyleManager *editingSm = m_typeDockWidget->currentStyleManager();
        StyleManager *styleManager;
        if (editingSm) {
            styleManager = editingSm->clone(this);
        } else {
            styleManager = new StyleManager(this);
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
                                substituteInlines(fn.content);
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
        pdfGen.setMaxJustifyGap(settings->maxJustifyGap());
        pdfGen.setExportOptions(opts);
        if (pdfGen.generateToFile(layoutResult, pl, fi.baseName(), path)) {
            statusBar()->showMessage(i18n("Exported to %1", path), 3000);
        } else {
            statusBar()->showMessage(i18n("Failed to export PDF."), 3000);
        }
    } else {
        // Legacy pipeline
        if (!view->document()) {
            statusBar()->showMessage(i18n("No document to export."), 3000);
            return;
        }
        auto *controller = new PrintController(view->document(), this);
        PageLayout pl = m_pageDockWidget->currentPageLayout();
        controller->setPageLayout(pl);
        QString title = m_tabWidget->tabText(m_tabWidget->currentIndex());
        controller->setFileName(title);
        controller->exportPdf(QString(), this);
        delete controller;
    }
}

void MainWindow::onFileExportRtf()
{
    auto *view = currentDocumentView();
    if (!view || !view->document()) {
        statusBar()->showMessage(i18n("No document to export."), 3000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, i18n("Export as RTF"),
        QString(), i18n("RTF Files (*.rtf)"));
    if (path.isEmpty())
        return;

    RtfExporter exporter;
    if (exporter.exportToFile(view->document(), path)) {
        statusBar()->showMessage(i18n("Exported to %1", path), 3000);
    } else {
        statusBar()->showMessage(i18n("Failed to export RTF."), 3000);
    }
}

void MainWindow::onFilePrint()
{
    auto *view = currentDocumentView();
    if (!view) {
        statusBar()->showMessage(i18n("No document to print."), 3000);
        return;
    }

    if (view->isPdfMode()) {
        // New pipeline: use PrintController with Poppler-based printing
        auto *controller = new PrintController(nullptr, this);
        controller->setPdfData(view->pdfData());
        PageLayout pl = m_pageDockWidget->currentPageLayout();
        controller->setPageLayout(pl);
        QString title = m_tabWidget->tabText(m_tabWidget->currentIndex());
        controller->setFileName(title);
        controller->print(this);
        delete controller;
    } else {
        // Legacy pipeline
        if (!view->document()) {
            statusBar()->showMessage(i18n("No document to print."), 3000);
            return;
        }
        auto *controller = new PrintController(view->document(), this);
        PageLayout pl = m_pageDockWidget->currentPageLayout();
        controller->setPageLayout(pl);
        QString title = m_tabWidget->tabText(m_tabWidget->currentIndex());
        controller->setFileName(title);
        controller->print(this);
        delete controller;
    }
}

void MainWindow::onFileClose()
{
    int index = m_tabWidget->currentIndex();
    if (index >= 0) {
        m_tabWidget->removeTab(index);
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

void MainWindow::onPageLayoutChanged()
{
    ++m_compositionGeneration;

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
    if (activeTab >= 0 && activeTab < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(activeTab);

    // Defer view state restoration until layouts are computed
    QList<int> zoomLevels = group.readEntry("ZoomLevels", QList<int>());
    QList<int> scrollPages = group.readEntry("ScrollPages", QList<int>());
    QStringList fracStrings = group.readEntry("ScrollFractions", QStringList());

    QTimer::singleShot(0, this, [this, zoomLevels, scrollPages, fracStrings]() {
        for (int i = 0; i < m_tabWidget->count() && i < zoomLevels.size(); ++i) {
            auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
            if (!tab)
                continue;
            ViewState vs;
            vs.zoomPercent = zoomLevels.value(i, 100);
            vs.currentPage = scrollPages.value(i, 0);
            vs.scrollFraction = (i < fracStrings.size()) ? fracStrings[i].toDouble() : 0.0;
            vs.valid = true;
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

    // Use source text from editor if in source mode, otherwise read from file
    QString markdown;
    if (tab->isSourceMode()) {
        markdown = tab->sourceText();
    } else {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        markdown = QString::fromUtf8(file.readAll());
        file.close();
    }

    // Use the editing copy from the style dock, or load fresh if none
    StyleManager *editingSm = m_typeDockWidget->currentStyleManager();
    StyleManager *styleManager;
    if (editingSm) {
        styleManager = editingSm->clone(this);
    } else {
        styleManager = new StyleManager(this);
        m_themeComposer->compose(styleManager);
    }

    auto *view = tab->documentView();
    if (!view)
        return;

    ViewState state = view->saveViewState();
    PageLayout pl = m_pageDockWidget->currentPageLayout();
    QFileInfo fi(filePath);

    if (PenelopeSettings::self()->usePdfRenderer()) {
        // --- New rendering pipeline (shared content building) ---
        ContentBuilder contentBuilder;
        contentBuilder.setBasePath(fi.absolutePath());
        contentBuilder.setStyleManager(styleManager);
        if (PenelopeSettings::self()->hyphenationEnabled()
            || PenelopeSettings::self()->hyphenateJustifiedText())
            contentBuilder.setHyphenator(m_hyphenator);
        if (PenelopeSettings::self()->shortWordsEnabled())
            contentBuilder.setShortWords(m_shortWords);
        contentBuilder.setFootnoteStyle(styleManager->footnoteStyle());
        Content::Document contentDoc = contentBuilder.build(markdown);

        view->applyLanguageOverrides(contentDoc);

        m_fontManager->resetUsage();

        Layout::Engine layoutEngine(m_fontManager, m_textShaper);
        layoutEngine.setHyphenateJustifiedText(
            PenelopeSettings::self()->hyphenateJustifiedText());

        if (PenelopeSettings::self()->useWebView()) {
            // --- Web view pipeline ---
            qreal availWidth = view->viewport()->width() - 2 * DocumentView::kSceneMargin;
            qreal zoomFactor = view->zoomPercent() / 100.0;
            if (zoomFactor > 0)
                availWidth /= zoomFactor;
            if (availWidth < 200)
                availWidth = 200;

            Layout::ContinuousLayoutResult webResult =
                layoutEngine.layoutContinuous(contentDoc, availWidth);

            // Build heading positions (absolute y from source map)
            QList<HeadingPosition> headingPositions;
            for (const auto &block : contentDoc.blocks) {
                const auto *heading = std::get_if<Content::Heading>(&block);
                if (!heading || heading->level < 1 || heading->level > 6)
                    continue;
                HeadingPosition hp;
                hp.page = 0;
                hp.sourceLine = heading->source.startLine;
                if (heading->source.startLine > 0) {
                    for (const auto &entry : webResult.sourceMap) {
                        if (entry.startLine == heading->source.startLine
                            && entry.endLine == heading->source.endLine) {
                            hp.yOffset = entry.rect.top();
                            break;
                        }
                    }
                }
                headingPositions.append(hp);
            }

            // TOC from content model
            m_tocWidget->buildFromContentModel(contentDoc, webResult.sourceMap);
            tab->setTocData(contentDoc, webResult.sourceMap);

            view->setWebFontManager(m_fontManager);
            view->setHeadingPositions(headingPositions);
            view->setSourceData(contentBuilder.processedMarkdown(), webResult.sourceMap,
                                contentDoc, webResult.codeBlockRegions);
            view->setWebContent(std::move(webResult));
            view->setRenderMode(DocumentView::WebMode);
            view->restoreViewState(state);
            view->setDocumentInfo(fi.fileName(), fi.baseName());
            connect(view, &DocumentView::currentHeadingChanged,
                    m_tocWidget, &TocWidget::highlightHeading,
                    Qt::UniqueConnection);

            // Wire debounced relayout
            connect(view, &DocumentView::webRelayoutRequested,
                    this, &MainWindow::rebuildCurrentDocument,
                    Qt::UniqueConnection);
        } else {
            // --- PDF rendering pipeline ---
            Layout::LayoutResult layoutResult = layoutEngine.layout(contentDoc, pl);

            PdfGenerator pdfGen(m_fontManager);
            pdfGen.setMaxJustifyGap(PenelopeSettings::self()->maxJustifyGap());
            QByteArray pdf = pdfGen.generate(layoutResult, pl, fi.baseName());

            // Clear legacy document if switching pipelines
            QTextDocument *oldDoc = view->document();
            if (oldDoc) {
                view->setDocument(nullptr);
                delete oldDoc;
            }

            view->setPdfData(pdf);
            view->setSourceData(contentBuilder.processedMarkdown(), layoutResult.sourceMap, contentDoc,
                                layoutResult.codeBlockRegions);
            view->setRenderMode(DocumentView::PrintMode);
            view->restoreViewState(state);
            view->setDocumentInfo(fi.fileName(), fi.baseName());

            // Build TOC directly from content model + source map
            m_tocWidget->buildFromContentModel(contentDoc, layoutResult.sourceMap);
            tab->setTocData(contentDoc, layoutResult.sourceMap);

            // Pass heading positions to view for scroll-sync
            {
                QList<HeadingPosition> headingPositions;
                for (const auto &block : contentDoc.blocks) {
                    const auto *heading = std::get_if<Content::Heading>(&block);
                    if (!heading || heading->level < 1 || heading->level > 6)
                        continue;
                    HeadingPosition hp;
                    hp.sourceLine = heading->source.startLine;
                    if (heading->source.startLine > 0) {
                        for (const auto &entry : layoutResult.sourceMap) {
                            if (entry.startLine == heading->source.startLine
                                && entry.endLine == heading->source.endLine) {
                                hp.page = entry.pageNumber;
                                hp.yOffset = entry.rect.top();
                                break;
                            }
                        }
                    }
                    headingPositions.append(hp);
                }
                view->setHeadingPositions(headingPositions);
                connect(view, &DocumentView::currentHeadingChanged,
                        m_tocWidget, &TocWidget::highlightHeading,
                        Qt::UniqueConnection);
            }
        }
    } else {
        // --- Legacy QTextDocument pipeline ---
        auto *doc = new QTextDocument(this);
        auto *builder = new DocumentBuilder(doc, this);
        builder->setBasePath(fi.absolutePath());
        builder->setStyleManager(styleManager);
        if (PenelopeSettings::self()->hyphenationEnabled()
            || PenelopeSettings::self()->hyphenateJustifiedText())
            builder->setHyphenator(m_hyphenator);
        if (PenelopeSettings::self()->shortWordsEnabled())
            builder->setShortWords(m_shortWords);
        builder->setFootnoteStyle(styleManager->footnoteStyle());
        builder->build(markdown);

        CodeBlockHighlighter rebuildHighlighter;
        rebuildHighlighter.highlight(doc);

        QTextDocument *oldDoc = view->document();
        view->setDocument(doc);
        delete oldDoc;
        view->restoreViewState(state);
        view->setDocumentInfo(fi.fileName(), fi.baseName());

        m_tocWidget->buildFromDocument(doc);
    }

    tab->setCompositionGeneration(m_compositionGeneration);
    statusBar()->showMessage(i18n("Theme applied"), 2000);
}

void MainWindow::openFile(const QUrl &url)
{
    if (!url.isLocalFile())
        return;

    const QString filePath = url.toLocalFile();

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

    // Build document with style manager (use editing copy if available)
    StyleManager *editingSm = m_typeDockWidget->currentStyleManager();
    StyleManager *styleManager;
    if (editingSm) {
        styleManager = editingSm->clone(this);
    } else {
        styleManager = new StyleManager(this);
        m_themeComposer->compose(styleManager);
    }

    auto *tab = new DocumentTab(this);
    tab->setFilePath(filePath);
    tab->setSourceText(markdown);
    PageLayout openPl = m_pageDockWidget->currentPageLayout();
    tab->documentView()->setPageLayout(openPl);

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

    if (webMode) {
        // Web mode: defer to rebuildCurrentDocument() after tab is current
    } else if (PenelopeSettings::self()->usePdfRenderer()) {
        // --- New PDF rendering pipeline ---
        ContentBuilder contentBuilder;
        contentBuilder.setBasePath(fi.absolutePath());
        contentBuilder.setStyleManager(styleManager);
        if (PenelopeSettings::self()->hyphenationEnabled()
            || PenelopeSettings::self()->hyphenateJustifiedText())
            contentBuilder.setHyphenator(m_hyphenator);
        if (PenelopeSettings::self()->shortWordsEnabled())
            contentBuilder.setShortWords(m_shortWords);
        contentBuilder.setFootnoteStyle(styleManager->footnoteStyle());
        Content::Document contentDoc = contentBuilder.build(markdown);

        tab->documentView()->applyLanguageOverrides(contentDoc);

        m_fontManager->resetUsage();

        Layout::Engine layoutEngine(m_fontManager, m_textShaper);
        layoutEngine.setHyphenateJustifiedText(
            PenelopeSettings::self()->hyphenateJustifiedText());
        Layout::LayoutResult layoutResult = layoutEngine.layout(contentDoc, openPl);

        PdfGenerator pdfGen(m_fontManager);
        pdfGen.setMaxJustifyGap(PenelopeSettings::self()->maxJustifyGap());
        QByteArray pdf = pdfGen.generate(layoutResult, openPl, fi.baseName());

        tab->documentView()->setPdfData(pdf);
        tab->documentView()->setSourceData(
            contentBuilder.processedMarkdown(), layoutResult.sourceMap, contentDoc,
            layoutResult.codeBlockRegions);

        // Build TOC from content model (PDF mode)
        m_tocWidget->buildFromContentModel(contentDoc, layoutResult.sourceMap);
        tab->setTocData(contentDoc, layoutResult.sourceMap);

        // Pass heading positions to view for scroll-sync
        {
            QList<HeadingPosition> headingPositions;
            for (const auto &block : contentDoc.blocks) {
                const auto *heading = std::get_if<Content::Heading>(&block);
                if (!heading || heading->level < 1 || heading->level > 6)
                    continue;
                HeadingPosition hp;
                hp.sourceLine = heading->source.startLine;
                if (heading->source.startLine > 0) {
                    for (const auto &entry : layoutResult.sourceMap) {
                        if (entry.startLine == heading->source.startLine
                            && entry.endLine == heading->source.endLine) {
                            hp.page = entry.pageNumber;
                            hp.yOffset = entry.rect.top();
                            break;
                        }
                    }
                }
                headingPositions.append(hp);
            }
            tab->documentView()->setHeadingPositions(headingPositions);
            connect(tab->documentView(), &DocumentView::currentHeadingChanged,
                    m_tocWidget, &TocWidget::highlightHeading,
                    Qt::UniqueConnection);
        }
    } else {
        // --- Legacy QTextDocument pipeline ---
        auto *doc = new QTextDocument(this);
        auto *builder = new DocumentBuilder(doc, this);
        builder->setBasePath(fi.absolutePath());
        builder->setStyleManager(styleManager);
        if (PenelopeSettings::self()->hyphenationEnabled()
            || PenelopeSettings::self()->hyphenateJustifiedText())
            builder->setHyphenator(m_hyphenator);
        if (PenelopeSettings::self()->shortWordsEnabled())
            builder->setShortWords(m_shortWords);
        builder->setFootnoteStyle(styleManager->footnoteStyle());
        builder->build(markdown);

        CodeBlockHighlighter highlighter;
        highlighter.highlight(doc);

        tab->documentView()->setDocument(doc);
    }

    tab->documentView()->setDocumentInfo(fi.fileName(), fi.baseName());

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
            this, [this](int percent) {
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
            this, [this](const QString &hint) {
        if (hint.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(hint);
    });

    // Code block language override: persist + rebuild
    connect(tab->documentView(), &DocumentView::codeBlockLanguageChanged,
            this, [this, filePath]() {
        auto *view = currentDocumentView();
        if (!view)
            return;
        // Persist overrides to MetadataStore as JSON
        QHash<QString, QString> overrides = view->codeBlockLanguageOverrides();
        QJsonObject langObj;
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            langObj.insert(it.key(), it.value());
        m_metadataStore->setValue(filePath, QStringLiteral("codeBlockLanguages"), langObj);
        rebuildCurrentDocument();
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

    int index = m_tabWidget->addTab(tab, fi.fileName());
    m_tabWidget->setTabToolTip(index, filePath);
    m_tabWidget->setCurrentIndex(index);

    // In web mode, build now that the tab is current (needs viewport width)
    if (webMode)
        rebuildCurrentDocument();

    m_recentFilesAction->addUrl(url);

    // Update TOC
    if (!tab->documentView()->isPdfMode()) {
        m_tocWidget->buildFromDocument(tab->documentView()->document());
    }

    // Stamp composition generation so tab-switch knows it's up to date
    tab->setCompositionGeneration(m_compositionGeneration);

    // A6: Update file path label in status bar
    if (m_filePathLabel)
        m_filePathLabel->setText(filePath);

    statusBar()->showMessage(i18n("Opened %1", fi.fileName()), 3000);
}
