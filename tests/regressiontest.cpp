#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFile>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QTabWidget>
#include <QToolButton>
#include <KActionCollection>
#include <KConfigGroup>
#include <KMessageWidget>
#include <KSharedConfig>
#include <QAction>
#include "pdfpageitem.h"
#include "boxpageitem.h"
#include <KLocalizedString>
#include <poppler-qt6.h>
#include "contentbuilder.h"
#include "contentplaintext.h"
#include "contentfilter.h"
#include "contentrtfexporter.h"
#include "documenttab.h"
#include "tocwidget.h"
#include "documentview.h"
#include "fontmanager.h"
#include "footnoteparser.h"
#include "layoutengine.h"
#include "mainwindow.h"
#include "metadatastore.h"
#include "pagerangeparser.h"
#include "pdfgenerator.h"
#include "pdfexportdialog.h"
#include "codespancollector.h"
#include "penelopesettings.h"
#include "rendercache.h"
#include "stylepropertieseditor.h"
#include "resourcestore.h"
#include "characterstyle.h"
#include "textshaper.h"
#include "webviewitem.h"
#include "compositioncontroller.h"
#include "linebreaker.h"
#include "fragmentcache.h"
#include "blockidentity.h"
#include "stylerunindex.h"
#include "boxpageitem.h"
#include "qtboxrenderer.h"
#include "pagelayoutwidget.h"
#include "hitindex.h"
#include "pagefingerprint.h"
#include "headerfooterplan.h"
#include "printcontroller.h"
#include "themecomposer.h"
#include "thememanager.h"
#include "stylemanager.h"
#include "colorpalette.h"
#include "typeset.h"
#include "paragraphstyle.h"
#include "footnotestyle.h"
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTreeWidget>

class RegressionTest : public QObject {
    Q_OBJECT
private:
    static bool writeFile(const QString &path, const QByteArray &data) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    }
    static QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private Q_SLOTS:
    void ranges_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QSet<int>>("pages");
        QTest::newRow("all") << QString() << true << QSet<int>({1,2,3,4,5});
        QTest::newRow("mixed") << QString("1-3, last") << true << QSet<int>({1,2,3,5});
        QTest::newRow("arithmetic") << QString("last-2") << true << QSet<int>({3});
        QTest::newRow("paren-range") << QString("(last-2)-last") << true << QSet<int>({3,4,5});
        QTest::newRow("reversed") << QString("4-2") << false << QSet<int>();
        QTest::newRow("empty-item") << QString(",") << false << QSet<int>();
        QTest::newRow("trailing-comma") << QString("1,") << false << QSet<int>();
        QTest::newRow("overflow") << QString("(last-999999999999999999999)") << false << QSet<int>();
        QTest::newRow("zero") << QString("0") << false << QSet<int>();
    }
    void ranges() {
        QFETCH(QString, expression); QFETCH(bool, valid); QFETCH(QSet<int>, pages);
        auto result = PageRangeParser::parse(expression, 5);
        QCOMPARE(result.valid, valid);
        if (valid) QCOMPARE(result.pages, pages);
        else QVERIFY(!result.errorMessage.isEmpty());
    }
    void noPages() { QVERIFY(!PageRangeParser::parse("last", 0).valid); }
    void footnotesPreserveTextAndLines() {
        const QString input = "Text[^a]\n\n[^a]: Note\n    continued\n\n  ordinary paragraph\n\n# Heading\n";
        FootnoteParser parser;
        const auto output = parser.process(input);
        QCOMPARE(output.count('\n'), input.count('\n'));
        QVERIFY(output.contains("  ordinary paragraph"));
        QCOMPARE(output.split('\n').indexOf("# Heading"), input.split('\n').indexOf("# Heading"));
        QCOMPARE(parser.footnotes().size(), 1);
        QCOMPARE(parser.footnotes().first().content, QString("Note continued"));
    }
    void footnoteLiterals() {
        FootnoteParser parser;
        QString input = "```md\n[^fake]: literal\n```\n\n`[^b]` and Text[^a] and Text[^b]\n\n[^a]: A\n[^b]: B\n";
        auto output = parser.process(input);
        QVERIFY(output.contains("[^fake]: literal"));
        QCOMPARE(parser.footnotes().size(), 2);
        QCOMPARE(parser.footnotes()[0].label, QString("a"));
        QCOMPARE(parser.footnotes()[1].label, QString("b"));
    }
    void blockWidows() {
        Layout::BlockBox block;
        for (int i = 0; i < 5; ++i) { Layout::LineBox line; line.height = 10; block.lines.append(line); }
        block.height = 50;
        auto split = Layout::splitBlockBox(block, 40);
        QVERIFY(split.has_value());
        QCOMPARE(split->first.lines.size(), 3);
        QCOMPARE(split->second.lines.size(), 2);
    }
    void tableAcrossPages() {
        QString markdown = "| Column |\n| --- |\n";
        for (int i = 0; i < 150; ++i) markdown += QString("| Row %1 |\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts; TextShaper shaper(&fonts); Layout::Engine engine(&fonts, &shaper);
        PageLayout pl;
        auto result = engine.layout(doc, pl);
        QVERIFY(result.pages.size() > 1);
        int bodyRows = 0;
        for (const auto &page : result.pages) {
            QVERIFY(page.contentHeight <= pl.contentSizePoints().height() + 0.01);
            for (const auto &element : page.elements) {
                auto *table = std::get_if<Layout::TableBox>(&element);
                QVERIFY(table);
                QCOMPARE(table->headerRowCount, 1);
                QVERIFY(table->source.startLine > 0);
                bodyRows += table->rows.size() - table->headerRowCount;
            }
        }
        QCOMPARE(bodyRows, 150);
        QCOMPARE(result.sourceMap.size(), result.pages.size());
        PdfGenerator generator(&fonts);
        auto pdf = generator.generate(result, pl, "Table");
        auto poppler = Poppler::Document::loadFromData(pdf);
        QVERIFY(poppler);
        QCOMPARE(poppler->numPages(), result.pages.size());
        QString text;
        for (int i = 0; i < poppler->numPages(); ++i) text += poppler->page(i)->text(QRectF());
        QVERIFY(text.contains("Row 149"));
    }
    void listCodeRetained() {
        ContentBuilder builder;
        auto doc = builder.build("- item\n\n  ```cpp\n  retained_code();\n  ```\n");
        FontManager fonts; TextShaper shaper(&fonts); Layout::Engine engine(&fonts, &shaper);
        auto result = engine.layoutContinuous(doc, 400);
        bool code = false;
        for (const auto &element : result.elements) {
            if (auto *block = std::get_if<Layout::BlockBox>(&element))
                code |= block->type == Layout::BlockBox::CodeBlockType;
        }
        QVERIFY(code);
    }
    void sourceSaveAndWatch() {
        QTemporaryDir dir; const QString path = dir.filePath("document.md");
        QVERIFY(writeFile(path, "# Original\n"));
        DocumentTab tab;
        tab.setFilePath(path); tab.setSourceText("# Original\n");
        QSignalSpy spy(&tab, &DocumentTab::sourceChanged);
        QVERIFY(writeFile(path, "# Reloaded\n"));
        QTRY_COMPARE_WITH_TIMEOUT(tab.sourceText(), QString("# Reloaded\n"), 3000);
        QVERIFY(!tab.isModified());
        QVERIFY(!spy.isEmpty());
        tab.sourceEditor()->insertPlainText("edit");
        QVERIFY(tab.isModified());
        QVERIFY(writeFile(path, "# External\n"));
        QTRY_VERIFY_WITH_TIMEOUT(tab.hasExternalChanges(), 3000);
        QVERIFY(tab.sourceText().contains("edit"));
        QString error;
        QVERIFY2(tab.saveSource(&error), qPrintable(error));
        QCOMPARE(readFile(path), tab.sourceText().toUtf8());
        QVERIFY(!tab.isModified());
        QVERIFY(!tab.hasExternalChanges());
        QVERIFY(writeFile(path, "# Watched again\n"));
        QTRY_COMPARE_WITH_TIMEOUT(tab.sourceText(), QString("# Watched again\n"), 3000);
    }
    void externalChangePromptsWhenAutoReloadOff() {
        QTemporaryDir dir;
        const QString path = dir.filePath("notice.md");
        QVERIFY(writeFile(path, "# Original\n"));
        auto *settings = PenelopeSettings::self();
        const bool previousAutoReload = settings->autoReloadOnChange();
        settings->setAutoReloadOnChange(false);

        DocumentTab tab;
        tab.setFilePath(path);
        tab.setSourceText("# Original\n");
        auto *bar = tab.findChild<KMessageWidget *>();
        QVERIFY(bar);
        QVERIFY(bar->isHidden());

        QSignalSpy reloaded(&tab, &DocumentTab::reloadedFromDisk);
        QVERIFY(writeFile(path, "# External\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!bar->isHidden(), 3000);
        QVERIFY2(bar->text().contains(QStringLiteral("changed on disk")), qPrintable(bar->text()));
        QCOMPARE(tab.sourceText(), QStringLiteral("# Original\n"));
        QCOMPARE(reloaded.count(), 0);

        // Editing while the notice is visible must upgrade the warning.
        QTextCursor cursor = tab.sourceEditor()->textCursor();
        cursor.movePosition(QTextCursor::End);
        tab.sourceEditor()->setTextCursor(cursor);
        tab.sourceEditor()->insertPlainText("edit");
        QVERIFY(tab.isModified());
        QTRY_VERIFY_WITH_TIMEOUT(bar->text().contains(QStringLiteral("unsaved edits")), 1000);
        bool discardAction = false;
        for (QAction *action : bar->actions())
            discardAction |= action->text().contains(QStringLiteral("Discard Edits"));
        QVERIFY(discardAction);

        QAction *reload = nullptr;
        for (QAction *action : bar->actions()) {
            if (action->text().contains(QStringLiteral("Reload")))
                reload = action;
        }
        QVERIFY(reload);
        reload->trigger();
        QCOMPARE(tab.sourceText(), QStringLiteral("# External\n"));
        QVERIFY(bar->isHidden());
        QCOMPARE(reloaded.count(), 1);
        QVERIFY(!tab.hasExternalChanges());

        settings->setAutoReloadOnChange(previousAutoReload);
    }
    void deletedFileIsReported() {
        QTemporaryDir dir;
        const QString path = dir.filePath("deleted.md");
        QVERIFY(writeFile(path, "# Original\n"));
        DocumentTab tab;
        tab.setFilePath(path);
        tab.setSourceText("# Original\n");
        auto *bar = tab.findChild<KMessageWidget *>();
        QVERIFY(bar);
        QVERIFY(bar->isHidden());

        QVERIFY(QFile::remove(path));
        QTRY_VERIFY_WITH_TIMEOUT(!bar->isHidden(), 3000);
        QVERIFY2(bar->text().contains(QStringLiteral("deleted")), qPrintable(bar->text()));
        QVERIFY(tab.hasExternalChanges());
        QCOMPARE(tab.sourceText(), QStringLiteral("# Original\n"));

        // Recreating the file with the open content resolves the notice.
        QVERIFY(writeFile(path, "# Original\n"));
        QTRY_VERIFY_WITH_TIMEOUT(bar->isHidden(), 3000);
    }
    void externalChangeKeepsUnsavedEdits() {
        QTemporaryDir dir;
        const QString path = dir.filePath("edited.md");
        QVERIFY(writeFile(path, "# Original\n"));
        auto *settings = PenelopeSettings::self();
        const bool previousAutoReload = settings->autoReloadOnChange();
        settings->setAutoReloadOnChange(true);

        DocumentTab tab;
        tab.setFilePath(path);
        tab.setSourceText("# Original\n");
        auto *bar = tab.findChild<KMessageWidget *>();
        QVERIFY(bar);
        QTextCursor cursor = tab.sourceEditor()->textCursor();
        cursor.movePosition(QTextCursor::End);
        tab.sourceEditor()->setTextCursor(cursor);
        tab.sourceEditor()->insertPlainText("my edits");
        QVERIFY(tab.isModified());

        QVERIFY(writeFile(path, "# External\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!bar->isHidden(), 3000);
        QVERIFY(tab.sourceText().contains(QStringLiteral("my edits")));
        QVERIFY(tab.hasExternalChanges());
        QVERIFY2(bar->text().contains(QStringLiteral("unsaved edits")), qPrintable(bar->text()));

        QAction *reload = nullptr;
        for (QAction *action : bar->actions()) {
            if (action->text().contains(QStringLiteral("Reload")))
                reload = action;
        }
        QVERIFY(reload);
        reload->trigger();
        QCOMPARE(tab.sourceText(), QStringLiteral("# External\n"));
        QVERIFY(!tab.isModified());
        QVERIFY(!tab.hasExternalChanges());
        QVERIFY(bar->isHidden());

        settings->setAutoReloadOnChange(previousAutoReload);
    }
    void reloadKeepsEditorPosition() {
        QTemporaryDir dir;
        const QString path = dir.filePath("position.md");
        QString original;
        for (int i = 0; i < 60; ++i)
            original += QStringLiteral("Line %1\n").arg(i);
        QVERIFY(writeFile(path, original.toUtf8()));

        DocumentTab tab;
        tab.setSourceMode(true);
        tab.setFilePath(path);
        tab.setSourceText(original);
        tab.resize(500, 400);
        tab.show();
        QTest::qWait(50);
        auto *editor = tab.sourceEditor();
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, 40);
        cursor.movePosition(QTextCursor::EndOfBlock);
        editor->setTextCursor(cursor);
        QVERIFY(editor->verticalScrollBar()->maximum() > 35);
        editor->verticalScrollBar()->setValue(35);

        QString changed = original;
        changed.replace(QStringLiteral("Line 59"), QStringLiteral("Line 59 changed on disk"));
        QSignalSpy reloaded(&tab, &DocumentTab::reloadedFromDisk);
        QVERIFY(writeFile(path, changed.toUtf8()));
        QTRY_COMPARE_WITH_TIMEOUT(reloaded.count(), 1, 3000);
        QCOMPARE(editor->textCursor().blockNumber(), 40);
        QCOMPARE(editor->verticalScrollBar()->value(), 35);
        QCOMPARE(tab.sourceText(), changed);
    }
    void scrollAnchorFollowsShiftedContent() {
        auto makeMarkdown = [](const QString &insert) {
            QString markdown = QStringLiteral("# Title\n\n");
            markdown += insert;
            for (int i = 0; i < 30; ++i)
                markdown += QStringLiteral("Paragraph %1. The quick brown fox.\n\n").arg(i);
            return markdown;
        };
        ContentBuilder builder;
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto layoutDoc = [&engine](const Content::Document &doc) {
            Layout::LayoutCursor cursor;
            Layout::LayoutBudget budget;
            budget.maxBlocks = 1000;
            return engine.layoutContinuousFrom(doc, 400, &cursor, budget);
        };

        DocumentView view;
        view.resize(500, 220);
        view.setRenderMode(DocumentView::WebMode);
        view.setWebFontManager(&fonts);

        const QString firstText = makeMarkdown({});
        const Content::Document firstDoc = builder.build(firstText);
        view.setWebContent(layoutDoc(firstDoc));
        view.setSourceData(firstText, view.webViewItem()->layoutResult().sourceMap, firstDoc);
        view.show();
        QTest::qWait(20);

        const int line = 3 + 2 * 12;
        qreal y = -1;
        for (const auto &entry : view.sourceMap()) {
            if (line >= entry.startLine && line <= entry.endLine) {
                y = entry.rect.top();
                break;
            }
        }
        QVERIFY2(y >= 0, qPrintable(QString::number(view.sourceMap().size())));
        view.scrollToPosition(0, y);
        QTest::qWait(20);
        const ScrollAnchor anchor = view.viewportAnchor();
        QVERIFY(anchor.isValid());
        QVERIFY(anchor.context.contains(QStringLiteral("Paragraph 1")));
        const ViewState state = view.saveViewState();

        // Two paragraphs are inserted above the reading position; the line
        // numbers shift, so only the block text identifies the place.
        const QString shiftedText = makeMarkdown(QStringLiteral("Inserted A.\n\nInserted B.\n\n"));
        const Content::Document shiftedDoc = builder.build(shiftedText);
        view.setWebContent(layoutDoc(shiftedDoc));
        view.setSourceData(shiftedText, view.webViewItem()->layoutResult().sourceMap, shiftedDoc);
        view.restoreViewState(state, anchor);
        QTest::qWait(50);
        const ScrollAnchor restored = view.viewportAnchor();
        QVERIFY(restored.isValid());
        QVERIFY2(restored.context == anchor.context, qPrintable(restored.context));
        QVERIFY(qAbs(restored.fraction - anchor.fraction) < 0.05);
    }
    void reloadKeepsReaderPositionAcrossLineShift() {
        QTemporaryDir dir;
        QString markdown = QStringLiteral("# Title\n\n");
        for (int i = 0; i < 50; ++i)
            markdown += QStringLiteral("Paragraph %1. The quick brown fox jumps over the lazy dog.\n\n").arg(i);
        const QString file = dir.filePath("reader.md");
        QVERIFY(writeFile(file, markdown.toUtf8()));

        PenelopeSettings::self()->setUseWebView(true);
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.openFile(QUrl::fromLocalFile(file));
        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        auto *tab = qobject_cast<DocumentTab *>(tabs->currentWidget());
        QVERIFY(tab);
        auto *view = tab->documentView();
        QTRY_VERIFY_WITH_TIMEOUT(view->webViewItem() && view->layoutCursor().finished, 20000);

        const int line = 3 + 2 * 25;
        qreal y = -1;
        for (const auto &entry : view->sourceMap()) {
            if (line >= entry.startLine && line <= entry.endLine) {
                y = entry.rect.top();
                break;
            }
        }
        QVERIFY(y >= 0);
        view->scrollToPosition(0, y);
        QTest::qWait(50);
        const ScrollAnchor anchor = view->viewportAnchor();
        QVERIFY(anchor.isValid());
        QVERIFY(anchor.context.contains(QStringLiteral("Paragraph 2")));

        QSignalSpy rendered(&window, &MainWindow::documentRendered);
        QSignalSpy reloaded(tab, &DocumentTab::reloadedFromDisk);
        QString shifted = markdown;
        shifted.insert(QStringLiteral("# Title\n\n").size(),
                       QStringLiteral("Inserted A.\n\nInserted B.\n\n"));
        QVERIFY(writeFile(file, shifted.toUtf8()));
        QTRY_COMPARE_WITH_TIMEOUT(reloaded.count(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rendered.count() > 0 && view->layoutCursor().finished, 20000);

        const ScrollAnchor restored = view->viewportAnchor();
        QVERIFY(restored.isValid());
        // Two inserted paragraphs shift every line below them by four. The
        // top block may land one block earlier or later after alignment.
        const int expectedLine = anchor.sourceLine + 4;
        QVERIFY2(qAbs(restored.sourceLine - expectedLine) <= 3,
                 qPrintable(QStringLiteral("restored %1 (expected ~%2): %3")
                                .arg(restored.sourceLine)
                                .arg(expectedLine)
                                .arg(restored.context)));
    }
    void cacheDensityAndInvalidation() {
        ContentBuilder builder; auto doc = builder.build("# Cache\n\nContent.");
        FontManager fonts; TextShaper shaper(&fonts); Layout::Engine engine(&fonts, &shaper);
        PageLayout pl; auto layout = engine.layout(doc, pl);
        PdfGenerator generator(&fonts); auto pdf = generator.generate(layout, pl, "Cache");
        auto poppler = Poppler::Document::loadFromData(pdf); QVERIFY(poppler);
        RenderCache cache; cache.setDocument(poppler.get());
        RenderCache::Request req; req.width = 200; req.height = 300;
        cache.requestPixmap(req);
        QTRY_VERIFY(!cache.cachedPixmap(0,200,300).isNull());
        QVERIFY(cache.cachedPixmap(0,200,300,2).isNull());
        req.dpr = 2; cache.requestPixmap(req);
        QTRY_VERIFY(!cache.cachedPixmap(0,200,300,2).isNull());
        QCOMPARE(cache.cachedPixmap(0,200,300,2).width(), 400);
        cache.invalidateAll();
        QVERIFY(cache.cachedPixmap(0,200,300).isNull());
        cache.setDocument(nullptr);
    }
    void sourceEditSignalIgnoresPresentationChanges() {
        DocumentTab tab;
        tab.setSourceText(QStringLiteral("# Title\n\nBody text.\n"));
        auto *editor = tab.sourceEditor();
        QVERIFY(editor);

        QSignalSpy userEdits(&tab, &DocumentTab::sourceChanged);

        QPalette pal = editor->palette();
        pal.setColor(QPalette::Base, QColor(10, 20, 30));
        pal.setColor(QPalette::Text, QColor(200, 210, 220));
        editor->setPalette(pal);
        QCOMPARE(userEdits.count(), 0);

        editor->setFont(QFont(editor->font().family(), editor->font().pointSize() + 2));
        QCOMPARE(userEdits.count(), 0);

        editor->insertPlainText(QStringLiteral("added "));
        QCOMPARE(userEdits.count(), 1);

        tab.setSourceText(QStringLiteral("replaced"));
        QCOMPARE(userEdits.count(), 1);
        QCOMPARE(tab.sourceText(), QStringLiteral("replaced"));
        QVERIFY(!tab.isModified());
    }
    void windowModeSwitchAndTabLifetime() {
        // A stale preference from older releases cannot re-enable the removed renderer.
        KConfigGroup rendering(KSharedConfig::openConfig(), QStringLiteral("Rendering"));
        rendering.writeEntry("UsePdfRenderer", false);
        rendering.sync();
        QTemporaryDir dir;
        const QString a = dir.filePath("a.md"), b = dir.filePath("b.md");
        QVERIFY(writeFile(a,"# Alpha\n\nText")); QVERIFY(writeFile(b,"# Beta\n\nText"));
        PenelopeSettings::self()->setUseWebView(false);
        MainWindow window;
        window.openFile(QUrl::fromLocalFile(a));
        auto *tabs = window.findChild<QTabWidget *>(); QVERIFY(tabs);
        auto *tab = qobject_cast<DocumentTab *>(tabs->currentWidget()); QVERIFY(tab);
        QTRY_VERIFY_WITH_TIMEOUT(tab->documentView()->hasPageItems() && tab->hasTocData(), 10000);
        window.actionCollection()->action("view_source_mode")->trigger();
        tab->sourceEditor()->insertPlainText("new ");
        window.actionCollection()->action("view_web_mode")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!tab->documentView()->isPdfMode() && tab->compositionGeneration() > 0, 10000);
        QVERIFY(tab->documentView()->pdfData().isEmpty());
        auto rtf = ContentRtfExporter().exportBlocks(tab->cachedContentDoc().blocks);
        QVERIFY(rtf.contains("new"));
        window.actionCollection()->action("view_print_mode")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(tab->documentView()->hasPageItems(), 10000);
        QVERIFY(tab->saveSource());
        window.openFile(QUrl::fromLocalFile(b));
        QCOMPARE(tabs->count(),2);
        QPointer<DocumentTab> closed(qobject_cast<DocumentTab *>(tabs->currentWidget()));
        window.actionCollection()->action("file_close")->trigger();
        QCOMPARE(tabs->count(),1);
        QVERIFY(closed.isNull());
        // Canonical paths prevent aliases from opening duplicate tabs.
        window.openFile(QUrl::fromLocalFile(dir.path() + "/./a.md"));
        QCOMPARE(tabs->count(),1);
        rendering.deleteEntry("UsePdfRenderer");
        rendering.sync();
    }
    void webSelectionCopiesMarkdownAndRtf() {
        FontManager fonts; TextShaper shaper(&fonts); Layout::Engine engine(&fonts, &shaper);
        ContentBuilder builder;
        QString source = "# Selected heading\n\nA **bold** paragraph.\n";
        auto doc = builder.build(source);
        auto layout = engine.layoutContinuous(doc, 400);
        QVERIFY(!layout.sourceMap.isEmpty());
        const auto rect = layout.sourceMap.first().rect;
        DocumentView view;
        view.resize(600,400);
        view.setRenderMode(DocumentView::WebMode);
        view.setWebFontManager(&fonts);
        view.setSourceData(builder.processedMarkdown(),layout.sourceMap,doc,layout.codeBlockRegions);
        view.setWebContent(std::move(layout));
        view.setCursorMode(DocumentView::SelectionTool);
        view.show();
        QTest::qWait(20);
        auto start = view.mapFromScene(rect.topLeft() + QPointF(1,1));
        auto end = view.mapFromScene(rect.bottomRight() - QPointF(1,1));
        QTest::mousePress(view.viewport(),Qt::LeftButton,Qt::NoModifier,start);
        QMouseEvent move(QEvent::MouseMove,end,view.viewport()->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view.viewport(),&move);
        QTest::mouseRelease(view.viewport(),Qt::LeftButton,Qt::NoModifier,end);
        view.copySelection();
        auto *mime = QApplication::clipboard()->mimeData();
        QVERIFY(mime->text().contains("Selected heading"));
        QVERIFY(!mime->text().startsWith('#'));
        QVERIFY(mime->hasFormat("text/markdown"));
        QVERIFY(mime->hasFormat("text/rtf"));
    }
    void landscapePdfAndFootnotes() {
        ContentBuilder builder;
        auto doc = builder.build("# Landscape\n\nNote[^a].\n\n[^a]: Text of note.\n");
        FontManager fonts; TextShaper shaper(&fonts); Layout::Engine engine(&fonts,&shaper);
        PageLayout pl; pl.orientation = QPageLayout::Landscape;
        pl.footerRight = "{page} / {pages} {filename}";
        auto layout = engine.layout(doc,pl);
        PdfGenerator generator(&fonts);
        generator.setDocumentInfo("landscape.md","Landscape");
        auto pdf = generator.generate(layout,pl,"Landscape");
        auto poppler = Poppler::Document::loadFromData(pdf); QVERIFY(poppler);
        auto page = poppler->page(0); QVERIFY(page);
        QCOMPARE(page->pageSizeF(),pl.pageSizePoints());
        const auto text = page->text(QRectF());
        QVERIFY2(text.contains("Landscape"),qPrintable(text));
        QVERIFY2(text.contains("1 / 1 landscape.md"),qPrintable(text));
        QVERIFY2(text.contains("1. Text of note"),qPrintable(text));
    }
    void mediaAndImagePreference() {
        QTemporaryDir dir;
        QImage image(40,40,QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        image.setPixelColor(20,20,Qt::red);
        QVERIFY(image.save(dir.filePath("asset.png")));
        ContentBuilder builder; builder.setBasePath(dir.path());
        auto doc = builder.build("Before ![Alt](asset.png) after.\n");
        FontManager fonts; TextShaper shaper(&fonts); Layout::Engine engine(&fonts,&shaper);
        PageLayout pl; pl.pageBackground = QColor(0,200,0);
        auto layout = engine.layout(doc,pl);
        QCOMPARE(layout.pages.first().elements.size(),3);
        const auto &box = std::get<Layout::BlockBox>(layout.pages.first().elements[1]);
        QCOMPARE(box.type,Layout::BlockBox::ImageBlock);
        QVERIFY(box.image.hasAlphaChannel());
        QVERIFY(box.source.startLine > 0);
        PdfGenerator generator(&fonts);
        auto pdf = generator.generate(layout,pl,"Image");
        QVERIFY(pdf.contains("/SMask"));
        auto poppler = Poppler::Document::loadFromData(pdf); QVERIFY(poppler);
        auto raster = poppler->page(0)->renderToImage(72,72);
        auto pixel = raster.pixelColor(qRound(pl.marginsPoints().left()+box.x+1),
                                     qRound(pl.marginsPoints().top()+box.y+1));
        QVERIFY2(pixel.green() > 150 && pixel.red() < 30,qPrintable(pixel.name()));
        PenelopeSettings::self()->setRenderImages(false);
        auto hidden = builder.build("![Alt](asset.png)\n");
        PenelopeSettings::self()->setRenderImages(true);
        auto para = std::get<Content::Paragraph>(hidden.blocks.first());
        QVERIFY(std::holds_alternative<Content::TextRun>(para.inlines.first()));
        QCOMPARE(std::get<Content::TextRun>(para.inlines.first()).text,QString("Alt"));
    }
    void syntaxHighlightPreference() {
        PenelopeSettings::self()->setSyntaxHighlightingEnabled(false);
        CodeSpanCollector disabled;
        QVERIFY(disabled.highlight("int main() { return 0; }","C++").isEmpty());
        PenelopeSettings::self()->setSyntaxHighlightingEnabled(true);
        PenelopeSettings::self()->setCodeHighlightTheme("Breeze Dark");
        CodeSpanCollector enabled;
        QVERIFY(!enabled.highlight("int main() { return 0; }","C++").isEmpty());
        QCOMPARE(enabled.theme().name(),QString("Breeze Dark"));
        PenelopeSettings::self()->setCodeHighlightTheme(QString());
    }
    void exportDialogRejectsInvalidRange() {
        ContentBuilder builder; auto doc = builder.build("# Heading\n");
        PdfExportDialog dialog(doc,5,"Title");
        PdfExportOptions options; options.pageRangeExpr = "1,";
        dialog.setOptions(options);
        QVERIFY(!dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
        options.pageRangeExpr = "last-2";
        dialog.setOptions(options);
        QVERIFY(dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
    }
    void previewCompressionLevelKeepsTextAndPages() {
        QString markdown = QStringLiteral("# Compression\n\nA paragraph with *emphasis*, "
                                          "`code` and a [link](https://example.org).\n\n"
                                          "| Column | Value |\n| --- | --- |\n"
                                          "| alpha | 12345 |\n| beta | 67890 |\n\n"
                                          "## Second heading\n\nMore prose to lay out across a "
                                          "page boundary so pagination is exercised.\n");
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        PageLayout pl;

        auto render = [&](int level) {
            FontManager fonts;
            TextShaper shaper(&fonts);
            Layout::Engine engine(&fonts, &shaper);
            fonts.resetUsage();
            auto laid = engine.layout(doc, pl);
            PdfGenerator gen(&fonts);
            gen.setCompressionLevel(level);
            return gen.generate(laid, pl, QStringLiteral("Compression"));
        };

        const QByteArray deflated = render(-1);
        const QByteArray fast = render(1);
        QVERIFY(!deflated.isEmpty());
        QVERIFY(!fast.isEmpty());

        // The preview trades file size for speed, so the bytes are expected to
        // differ; what must not differ is what a viewer sees.
        auto inspect = [](const QByteArray &pdf, int *pages, QString *text) {
            auto doc = Poppler::Document::loadFromData(pdf);
            QVERIFY(doc);
            *pages = doc->numPages();
            for (int i = 0; i < doc->numPages(); ++i)
                *text += doc->page(i)->text(QRectF());
        };

        int defaultPages = 0, fastPages = 0;
        QString defaultText, fastText;
        inspect(deflated, &defaultPages, &defaultText);
        inspect(fast, &fastPages, &fastText);

        QCOMPARE(fastPages, defaultPages);
        QCOMPARE(fastText, defaultText);
        QVERIFY(defaultText.contains(QStringLiteral("Compression")));
        QVERIFY(fast.size() > deflated.size());
    }
    void runTextOperatorsMatchPerGlyphPositioning() {
        QString markdown = QStringLiteral("# Metrics\n\n"
            "Typography measures advances, kerning pairs like AV, To, Wa and "
            " LY, and a justification test that stretches the line so extra "
            "word space has to appear between every word on the page.\n\n"
            "Second paragraph: AVATAR To Wa Ty. Mixed 0123456789 punctuation, "
            "commas, semicolons, and a hyphen-ated word.\n\n"
            "| Column | Value |\n| --- | --- |\n| AV | 12345 |\n| To | 67890 |\n\n"
            "> A quotation with AV kerning and a dash - inside.\n\n"
            "- list item one with To\n- list item two with Wa\n\n"
            "* item\n  * nested AV\n\n"
            "1. numbered AV\n2. numbered To\n\n"
            "Unicode: \u00e9\u00e8\u00ea accents, \u2014 em dash, \u201cquotes\u201d, "
            "\u65e5\u672c\u8a9e CJK, \u05e9\u05dc\u05d5\u05dd Hebrew.\n\n"
            "```\nfenced_code(AV, To);\n```\n");

        ContentBuilder builder;
        auto doc = builder.build(markdown);
        PageLayout pl;

        auto render = [&](bool runText) {
            FontManager fonts;
            TextShaper shaper(&fonts);
            Layout::Engine engine(&fonts, &shaper);
            engine.setHyphenateJustifiedText(true);
            fonts.resetUsage();
            auto laid = engine.layout(doc, pl);
            PdfGenerator gen(&fonts);
            gen.setRunTextOperators(runText);
            return gen.generate(laid, pl, QStringLiteral("Metrics"));
        };

        const QByteArray perGlyph = render(false);
        const QByteArray runOps = render(true);
        QVERIFY(!perGlyph.isEmpty());
        QVERIFY(!runOps.isEmpty());
        QVERIFY2(runOps.size() < perGlyph.size(),
                 qPrintable(QStringLiteral("run operators should be smaller: %1 vs %2")
                                .arg(runOps.size()).arg(perGlyph.size())));

        // Collect what a viewer sees: every character's text and box, per page.
        auto charBoxes = [](const QByteArray &pdf) {
            QList<QPair<QString, QRectF>> out;
            auto d = Poppler::Document::loadFromData(pdf);
            if (!d)
                return out;
            for (int p = 0; p < d->numPages(); ++p) {
                std::unique_ptr<Poppler::Page> page(d->page(p));
                if (!page)
                    continue;
                const std::vector<std::unique_ptr<Poppler::TextBox>> words = page->textList();
                for (const std::unique_ptr<Poppler::TextBox> &w : words) {
                    for (int i = 0; i < w->text().size(); ++i) {
                        const QRectF box = w->charBoundingBox(i);
                        // Quantise to a tenth of a point: sub-0.1pt differences
                        // come from the adjustment numbers, not from placement.
                        out.append({w->text().mid(i, 1),
                                    QRectF(qRound(box.left() * 10) / 10.0,
                                           qRound(box.top() * 10) / 10.0,
                                           qRound(box.width() * 10) / 10.0,
                                           qRound(box.height() * 10) / 10.0)});
                    }
                }
            }
            return out;
        };

        const auto a = charBoxes(perGlyph);
        const auto b = charBoxes(runOps);
        QVERIFY2(!a.isEmpty(), "no characters extracted from the per-glyph PDF");
        QCOMPARE(b.size(), a.size());

        qreal worst = 0;
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(b[i].first, a[i].first);
            const qreal dx = qAbs(b[i].second.left() - a[i].second.left());
            const qreal dy = qAbs(b[i].second.top() - a[i].second.top());
            const qreal dw = qAbs(b[i].second.width() - a[i].second.width());
            const qreal dh = qAbs(b[i].second.height() - a[i].second.height());
            worst = qMax(worst, qMax(qMax(dx, dy), qMax(dw, dh)));
            QVERIFY2(dy <= 0.15 && dw <= 0.15 && dh <= 0.15,
                     "character's vertical position or dimensions changed");
            QVERIFY2(dx <= 0.15,
                     qPrintable(QStringLiteral("char %1 '%2' moved %3pt (page-run %4 vs per-glyph %5)")
                                    .arg(i).arg(a[i].first).arg(dx)
                                    .arg(b[i].second.left()).arg(a[i].second.left())));
        }
        QVERIFY2(worst <= 0.15, qPrintable(QStringLiteral("worst drift %1pt").arg(worst)));
    }
    void runTextOperatorsPreserveVerticalOffsets() {
        ContentBuilder builder;
        const auto doc = builder.build(QStringLiteral("AVATAR Typography\n"));
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout pl;
        fonts.resetUsage();
        auto laid = engine.layout(doc, pl);
        bool changed = false;
        for (auto &page : laid.pages) {
            for (auto &element : page.elements) {
                auto *block = std::get_if<Layout::BlockBox>(&element);
                if (!block) continue;
                for (auto &line : block->lines) {
                    for (auto &run : line.glyphs) {
                        if (run.glyphs.size() < 2) continue;
                        // Exercise the positioning contract independently of
                        // the installed font's mark-positioning tables.
                        run.glyphs[1].yOffset = 4.0;
                        changed = true;
                    }
                }
            }
        }
        QVERIFY(changed);
        auto raster = [&](bool useRuns) {
            PdfGenerator gen(&fonts);
            gen.setRunTextOperators(useRuns);
            auto pdf = Poppler::Document::loadFromData(gen.generate(laid, pl, "Offsets"));
            if (!pdf) return QImage();
            std::unique_ptr<Poppler::Page> page(pdf->page(0));
            return page ? page->renderToImage(144, 144) : QImage();
        };
        const QImage absolute = raster(false);
        QVERIFY(!absolute.isNull());
        QCOMPARE(raster(true), absolute);
    }
    void continuousReaderCullsOutsideViewport() {
        class ObservedItem : public WebViewItem {
        public:
            using WebViewItem::WebViewItem;
            QRectF exposed;
            void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w) override {
                exposed = o->exposedRect;
                WebViewItem::paint(p, o, w);
            }
        };
        QString markdown;
        for (int i = 0; i < 200; ++i)
            markdown += QStringLiteral("[Link %1](https://example.org/%1) text.\n\n").arg(i);
        ContentBuilder builder;
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto result = engine.layoutContinuous(builder.build(markdown), 500);
        const qreal height = result.totalHeight;
        QGraphicsScene scene;
        auto *item = new ObservedItem(&fonts);
        item->setLayoutResult(std::move(result));
        scene.addItem(item);
        QGraphicsView view(&scene);
        view.resize(550, 350);
        view.show();
        view.ensureVisible(QRectF(0, 0, 1, 1), 0, 0);
        QApplication::processEvents();
        QImage image(view.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        view.viewport()->render(&painter);
        painter.end();
        QVERIFY(!item->exposed.isEmpty());
        QVERIFY2(item->exposed.height() < height / 4,
                 "Qt supplied the whole document as exposedRect; viewport culling is inactive");
        QVERIFY(!item->linkHitRects().isEmpty());
        QVERIFY(item->linkHitRects().size() < 50);
        const auto oldLink = item->linkHitRects().first();
        QCOMPARE(item->linkAt(oldLink.rect.center()), oldLink.href);
        // Simulate a narrow dirty region elsewhere. Existing visible links
        // must remain clickable even though they are not repainted.
        QStyleOptionGraphicsItem partial;
        partial.exposedRect = QRectF(0, height - 100, 500, 100);
        QPainter stripePainter(&image);
        item->paint(&stripePainter, &partial, nullptr);
        stripePainter.end();
        QCOMPARE(item->linkAt(oldLink.rect.center()), oldLink.href);

    }
    void shapingCacheReplaysUsageAndBoundsMemory() {
        FontManager fonts;
        TextShaper shaper(&fonts);
        StyleRun style;
        style.length = 6; style.fontFamily = "Noto Serif";
        const auto first = shaper.shape("AVATAR", {style});
        QVERIFY(!first.isEmpty());
        const auto misses = shaper.cacheMisses();
        fonts.resetUsage();
        const auto repeated = shaper.shape("AVATAR", {style});
        QCOMPARE(shaper.cacheMisses(), misses);
        QCOMPARE(shaper.cacheHits(), quint64(1));
        QCOMPARE(repeated.first().glyphs.size(), first.first().glyphs.size());
        for (const auto &run : repeated)
            for (const auto &glyph : run.glyphs) QVERIFY(run.font->usedGlyphs.contains(glyph.glyphId));
        style.fontSize += 1;
        shaper.shape("AVATAR", {style});
        QCOMPARE(shaper.cacheMisses(), misses + 1);
        style.fontFeatures = {"-kern"};
        shaper.shape("AVATAR", {style});
        QCOMPARE(shaper.cacheMisses(), misses + 2);
        shaper.setCacheLimit(256);
        QVERIFY(shaper.cacheCost() <= 256);
        shaper.setCacheLimit(0);
        QCOMPARE(shaper.cacheCost(), 0);
    }
    void asynchronousCompositionCoalescesAndReusesContent() {
        CompositionController controller;
        QSignalSpy ready(&controller, &CompositionController::ready);
        QSignalSpy failed(&controller, &CompositionController::failed);
        QSignalSpy started(&controller, &CompositionController::started);
        CompositionRequest request;
        request.continuous = true;
        request.hyphenateJustified = false;
        request.semanticRevision = 1;
        request.markdown = "# Old\n\n";
        for (int i = 0; i < 1000; ++i) request.markdown += "A paragraph to shape.\n\n";
        controller.submit(request);
        for (int i = 0; i < 20; ++i) {
            request.markdown = QStringLiteral("# Latest %1\n\nAVATAR shaped text.\n").arg(i);
            controller.submit(request);
            QVERIFY(controller.pendingCount() <= 1);
        }
        QCOMPARE(ready.size(), 0); // no synchronous reader composition
        QCOMPARE(started.size(), 1); // pending work did not spawn more threads/jobs
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        QCOMPARE(started.size(), 2);
        QCOMPARE(failed.size(), 0);
        auto result = qvariant_cast<CompositionResultPtr>(ready.takeFirst().at(1));
        QCOMPARE(result->request.markdown, request.markdown);
        QVERIFY(!result->reusedContent);
        QVERIFY(!result->continuousLayout.elements.isEmpty());
        request.width += 80;
        controller.submit(request);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        auto resized = qvariant_cast<CompositionResultPtr>(ready.takeFirst().at(1));
        QVERIFY(resized->reusedContent);
        QVERIFY(resized->shapeHits > 0);
        QCOMPARE(resized->shapeMisses, quint64(0));
        // Replay with resetUsage also produces a valid PDF and embedded text.
        request.continuous = false;
        controller.submit(request);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        auto pdfResult = qvariant_cast<CompositionResultPtr>(ready.takeFirst().at(1));
        QVERIFY(pdfResult->reusedContent);
        auto pdf = Poppler::Document::loadFromData(pdfResult->pdf);
        QVERIFY(pdf);
        std::unique_ptr<Poppler::Page> page(pdf->page(0));
        QVERIFY(page->text(QRectF()).contains("AVATAR"));
        // An explicit cancellation delivers neither a stale result nor error.
        controller.submit(request);
        controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(failed.size(), 0);
    }
    void asynchronousReaderDefersSourceAndSurvivesClosing() {
        QTemporaryDir dir;
        const auto file = dir.filePath("async.md");
        QVERIFY(writeFile(file, "# Initial\n\nBody.\n"));
        PenelopeSettings::self()->setUseWebView(true);
        MainWindow window;
        QSignalSpy rendered(&window, &MainWindow::documentRendered);
        auto *controller = window.findChild<CompositionController *>(); QVERIFY(controller);
        QSignalSpy started(controller, &CompositionController::started);
        window.openFile(QUrl::fromLocalFile(file));
        auto *tabs = window.findChild<QTabWidget *>(); QVERIFY(tabs);
        auto *tab = qobject_cast<DocumentTab *>(tabs->currentWidget()); QVERIFY(tab);
        QTRY_VERIFY_WITH_TIMEOUT(tab->hasTocData(), 10000);
        window.actionCollection()->action("view_source_mode")->trigger();
        const int count = started.size();
        tab->sourceEditor()->insertPlainText("Fresh ");
        QTest::qWait(400);
        QCOMPARE(started.size(), count);
        QCOMPARE(tab->compositionGeneration(), quint64(0));
        window.actionCollection()->action("view_web_mode")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(tab->compositionGeneration() > 0, 10000);
        QVERIFY(ContentRtfExporter().exportBlocks(tab->cachedContentDoc().blocks).contains("Fresh"));
        QVERIFY(tab->saveSource());
        // Superseded results may not install after switching to source.
        window.actionCollection()->action("view_print_mode")->trigger();
        window.actionCollection()->action("view_source_mode")->trigger();
        const int renderedCount = rendered.size();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isBusy(), 10000);
        QCOMPARE(rendered.size(), renderedCount);
        // Pending target destruction must not dereference the tab.
        window.actionCollection()->action("view_print_mode")->trigger();
        QPointer<DocumentTab> closed(tab);
        window.actionCollection()->action("file_close")->trigger();
        QVERIFY(closed.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isBusy(), 10000);
    }
    void asynchronousOpenRestoresSavedPage() {
        QTemporaryDir dir;
        const auto file = dir.filePath("saved-page.md");
        QByteArray source("# Saved page\n\n");
        for (int i = 0; i < 100; ++i) source += "A paragraph with enough text to produce several pages.\n\n";
        QVERIFY(writeFile(file, source));
        auto *settings = PenelopeSettings::self();
        settings->setUseWebView(false);
        settings->setViewMode(PenelopeSettings::SinglePage);
        settings->setRememberPerFileSettings(true);
        MetadataStore metadata;
        metadata.setValue(file, "viewState", QJsonObject{{"page", 2}, {"zoom", 130}, {"scroll", 0.5}});
        MainWindow window;
        window.openFile(QUrl::fromLocalFile(file));
        auto *tabs = window.findChild<QTabWidget *>(); QVERIFY(tabs);
        auto *tab = qobject_cast<DocumentTab *>(tabs->currentWidget()); QVERIFY(tab);
        QTRY_VERIFY_WITH_TIMEOUT(tab->compositionGeneration() > 0, 10000);
        QCOMPARE(tab->documentView()->zoomPercent(), 130);
        auto *view = tab->documentView();
        QTRY_VERIFY_WITH_TIMEOUT(view->pageCount() >= 3, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(view->saveViewState().currentPage, 2, 10000);
        for (auto mode : {DocumentView::SinglePage, DocumentView::FacingPages,
                          DocumentView::FacingPagesFirstAlone}) {
            view->setViewMode(mode);
            view->goToPage(0);
            view->goToPage(2);
            bool hasTargetPage = false;
            for (auto *item : view->scene()->items()) {
                if (auto *page = dynamic_cast<PdfPageItem *>(item))
                    hasTargetPage |= page->pageNumber() == 2;
                if (auto *page = dynamic_cast<BoxPageItem *>(item))
                    hasTargetPage |= page->pageNumber() == 2;
            }
            QVERIFY(hasTargetPage);
            QCOMPARE(view->currentPage(), 2);
        }
        settings->setViewMode(PenelopeSettings::Continuous);
    }
    void continuousSnapshotOutlivesController() {
        CompositionResultPtr result;
        {
            CompositionController controller;
            QSignalSpy ready(&controller, &CompositionController::ready);
            CompositionRequest request;
            request.markdown = "# Lifetime\n\nAVATAR.\n";
            request.continuous = true;
            controller.submit(request);
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
            result = qvariant_cast<CompositionResultPtr>(ready.takeFirst().at(1));
        }
        DocumentView view;
        view.resize(600, 400);
        view.setWebFontOwner(result->fonts);
        view.setSourceData(result->processedMarkdown, result->continuousLayout.sourceMap,
                           result->document, result->continuousLayout.codeBlockRegions);
        view.setWebContent(std::move(result->continuousLayout));
        result.reset();
        view.setRenderMode(DocumentView::WebMode);
        view.show();
        QApplication::processEvents();
        const QImage image = view.viewport()->grab().toImage();
        QVERIFY(!image.isNull());
    }
    void contentPlainTextPreservesVisibleContent() {
        ContentBuilder builder;
        builder.setRenderImages(false);
        const auto doc = builder.build(QStringLiteral(
            "# Heading\n\nA **bold** [link](https://example.com) &amp; `code`.\n\n"
            "> Quoted\n\n- Item\n  - Nested\n\n"
            "| Name | Value |\n| --- | --- |\n| First | Second |\n\n"
            "```cpp\nx < y;\n```\n\n![Image description](missing.png)\n\n"
            "Note[^a].\n\n[^a]: Footnote text.\n"));
        const auto text = Content::toPlainText(doc);
        QVERIFY(text.startsWith(QStringLiteral("Heading\nA bold link & code.")));
        QVERIFY(text.contains(QStringLiteral("Quoted\nItem\nNested")));
        QVERIFY(text.contains(QStringLiteral("Name\tValue\nFirst\tSecond")));
        QVERIFY(text.contains(QStringLiteral("x < y;\nImage description")));
        QVERIFY(text.contains(QStringLiteral("Footnote text.")));
        QVERIFY(!text.contains(QStringLiteral("https://example.com")));
        QVERIFY(!text.contains(QStringLiteral("[^a]")));
        Content::Document typography;
        Content::Paragraph paragraph;
        paragraph.inlines.append(Content::TextRun{QStringLiteral("soft\u00adhyphen non\u00a0breaking"), {}});
        typography.blocks.append(paragraph);
        QCOMPARE(Content::toPlainText(typography), QStringLiteral("softhyphen non breaking"));
    }
    void numericEntityUnicode() {
        const char32_t cp = 0x1F600;
        ContentBuilder builder;
        auto doc = builder.build("&#x1F600;\n");
        auto para = std::get<Content::Paragraph>(doc.blocks.first());
        QCOMPARE(std::get<Content::TextRun>(para.inlines.first()).text,QString::fromUcs4(&cp,1));
    }
    void metadataPreferenceAndExpiry() {
        MetadataStore store;
        PenelopeSettings::self()->setRememberPerFileSettings(false);
        store.setValue("/disabled.md","value",123);
        QVERIFY(store.load("/disabled.md").isEmpty());
        PenelopeSettings::self()->setRememberPerFileSettings(true);
        QVERIFY(store.load("/disabled.md").isEmpty());
        store.setValue("/expire.md","value",123);
        auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/metadata";
        for (const auto &path : QDir(directory).entryList({"*.json"},QDir::Files)) {
            QFile file(directory + "/" + path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-100),QFileDevice::FileModificationTime));
        }
        QVERIFY(store.load("/expire.md").isEmpty());
    }
    void resourceSaveIntegrity() {
        QTemporaryDir dir;
        ResourceStore store;
        store.discover("palettes",[](const QJsonObject &) { return true; },{dir.path()});
        QVERIFY(store.availableIds().contains("default-light"));
        QJsonObject json{{"name","Default Light"},{"colors",QJsonObject()}};
        auto id = store.save(QString(),"Default Light",json,"palette");
        QVERIFY(!id.isEmpty());
        QVERIFY(id != "default-light");
        QVERIFY(!store.isBuiltin(id));
        ResourceStore reloaded;
        reloaded.discover("palettes",[](const QJsonObject &) { return true; },{dir.path()});
        QVERIFY(reloaded.availableIds().contains(id));
        QCOMPARE(reloaded.loadJson(id).value("id").toString(),id);
        QVERIFY(store.save("../escape","Invalid",json,"palette").isEmpty());
        QVERIFY(QFile::remove(dir.filePath(id + ".json")));
        QVERIFY(!store.remove(id,"test"));
        QVERIFY(store.availableIds().contains(id));
    }
    void characterDecorationControls() {
        StylePropertiesEditor editor;
        CharacterStyle style("Decoration");
        editor.loadCharacterStyle(style,style,{});
        auto buttons = editor.findChildren<QToolButton *>();
        bool found = false;
        for (auto *button : buttons) {
            if (button->text() == "U") {
                QVERIFY(button->isEnabled());
                button->click();
                found = true;
            }
        }
        QVERIFY(found);
        editor.applyToCharacterStyle(style);
        QVERIFY(style.hasFontUnderline());
        QVERIFY(style.fontUnderline());
    }
    void nestedQuoteAndListStructure() {
        ContentBuilder builder;
        auto doc = builder.build("- outer\n\n  > quoted\n\n  after\n");
        QCOMPARE(doc.blocks.size(),1);
        const auto &list = std::get<Content::List>(doc.blocks.first());
        QCOMPARE(list.items.size(),1);
        QCOMPARE(list.items.first().children.size(),3);
        const auto &quote = std::get<Content::BlockQuote>(list.items.first().children[1]);
        QCOMPARE(quote.children.size(),1);
        const auto &para = std::get<Content::Paragraph>(quote.children.first());
        QCOMPARE(std::get<Content::TextRun>(para.inlines.first()).text,QString("quoted"));
        QCOMPARE(quote.source.startLine,3);
        QCOMPARE(quote.source.endLine,3);
        doc = builder.build("> - tight\n>   ```\n>   int x;\n>   ```\n> - next\n");
        auto &outerQuote = std::get<Content::BlockQuote>(doc.blocks.first());
        auto &innerList = std::get<Content::List>(outerQuote.children.first());
        QCOMPARE(innerList.items.size(),2);
        QCOMPARE(innerList.items.first().children.size(),2);
        QCOMPARE(std::get<Content::Paragraph>(innerList.items.first().children.first()).source.startLine,1);
        auto &code = std::get<Content::CodeBlock>(innerList.items.first().children[1]);
        DocumentView view;
        view.setCodeBlockLanguageOverrides({{code.code.trimmed(),QStringLiteral("C++")}});
        view.applyLanguageOverrides(doc);
        QCOMPARE(code.language,QStringLiteral("C++"));
    }
    void metadataRoundTrip() {
        MetadataStore store;
        store.setValue("/example.md", "zoom", 125);
        store.setValue("/example.md", "other", "kept");
        QCOMPARE(store.value("/example.md", "zoom").toInt(),125);
        QCOMPARE(store.value("/example.md", "other").toString(),QString("kept"));
    }

    static QString glyphText(const Layout::PageElement &element) {
        QString text;
        if (const auto *block = std::get_if<Layout::BlockBox>(&element)) {
            for (const auto &line : block->lines)
                for (const auto &glyph : line.glyphs)
                    text += glyph.text;
        }
        return text;
    }
    static int lastSourceLine(const Layout::LayoutResult &result) {
        int line = -1;
        if (result.pages.isEmpty()) return line;
        for (const auto &element : result.pages.last().elements) {
            std::visit([&](const auto &item) {
                if constexpr (std::is_same_v<std::decay_t<decltype(item)>, Layout::BlockBox>
                              || std::is_same_v<std::decay_t<decltype(item)>, Layout::TableBox>)
                    line = qMax(line, item.source.endLine);
            }, element);
        }
        return line;
    }
    static CompositionResultPtr awaitReady(CompositionController &controller, const CompositionRequest &request) {
        QSignalSpy ready(&controller, &CompositionController::ready);
        QSignalSpy failed(&controller, &CompositionController::failed);
        controller.submit(request);
        const bool arrived = QTest::qWaitFor([&] { return ready.size() >= 1 || failed.size() >= 1; }, 10000);
        if (!arrived || failed.size() != 0 || ready.size() != 1)
            return {};
        return qvariant_cast<CompositionResultPtr>(ready.takeFirst().at(1));
    }

    void ebook01FindBreaksObservesCancel() {
        QList<LineBreaking::Item> items;
        for (int i = 0; i < 2000; ++i) {
            items.append(LineBreaking::Item::makeBox(10, i));
            items.append(LineBreaking::Item::makeGlue(4, 2, 1));
        }
        int checks = 0;
        auto result = LineBreaking::findBreaks(items, {200}, {}, [&] {
            return ++checks > 2;
        });
        QVERIFY(result.cancelled);
        QVERIFY(result.breaks.isEmpty());
        auto again = LineBreaking::findBreaks(items, {200});
        QVERIFY(!again.cancelled);
        QVERIFY(!again.breaks.isEmpty());
    }
    void ebook01LayoutAbortsOneParagraph() {
        const QString sentence = QStringLiteral("The quick brown fox jumps. ");
        QString body;
        for (int i = 0; i < 4000; ++i) body += sentence;
        ContentBuilder builder;
        auto doc = builder.build(body);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout layout;
        engine.setCancellationCheck([] { return true; });
        QCOMPARE(engine.layout(doc, layout).pages.size(), 0);
        QCOMPARE(engine.layoutContinuous(doc, 400).elements.size(), 0);
        engine.setCancellationCheck({});
        QVERIFY(!engine.layout(doc, layout).pages.isEmpty());
        QVERIFY(!engine.layout(doc, layout).pages.first().elements.isEmpty());
    }
    void ebook01ShapeDoesNotCachePartial() {
        FontManager fonts;
        TextShaper shaper(&fonts);
        const QString text = QStringLiteral("Alpha Beta");
        StyleRun first{0, 5, QStringLiteral("Noto Serif"), 400, false, 12};
        StyleRun second{6, 4, QStringLiteral("Noto Sans"), 700, true, 12};
        shaper.setCancellationCheck([] { return true; });
        QVERIFY(shaper.shape(text, {first, second}).isEmpty());
        const auto misses = shaper.cacheMisses();
        shaper.setCancellationCheck({});
        QVERIFY(!shaper.shape(text, {first, second}).isEmpty());
        QVERIFY(shaper.cacheMisses() > misses);
        const auto hits = shaper.cacheHits();
        shaper.shape(text, {first, second});
        QVERIFY(shaper.cacheHits() > hits);
    }
    void ebook01SupersededHugeParagraphDeliversShort() {
        CompositionController controller;
        QSignalSpy ready(&controller, &CompositionController::ready);
        QSignalSpy failed(&controller, &CompositionController::failed);
        QString huge;
        const QString sentence = QStringLiteral("The quick brown fox jumps. ");
        for (int i = 0; i < 4000; ++i) huge += sentence;
        CompositionRequest first;
        first.markdown = huge;
        first.continuous = true;
        controller.submit(first);
        CompositionRequest second;
        second.markdown = QStringLiteral("# Ok\n\nShort.\n");
        second.continuous = true;
        const quint64 id = controller.submit(second);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        QCOMPARE(failed.size(), 0);
        auto result = qvariant_cast<CompositionResultPtr>(ready.first().at(1));
        QCOMPARE(ready.first().at(0).toULongLong(), id);
        QCOMPARE(result->request.markdown, second.markdown);
        QVERIFY(!result->continuousLayout.elements.isEmpty());
    }
    void ebook02StyleRunIndexMatchesEdges() {
        QList<StyleRun> runs{{0, 10, {}, 400, false, 11}, {10, 5, {}, 400, false, 11}, {20, 1, {}, 400, false, 11}};
        QCOMPARE(styleRunIndexAt(0, runs), 0);
        QCOMPARE(styleRunIndexAt(9, runs), 0);
        QCOMPARE(styleRunIndexAt(10, runs), 1);
        QCOMPARE(styleRunIndexAt(14, runs), 1);
        QCOMPARE(styleRunIndexAt(20, runs), 2);
        QCOMPARE(styleRunIndexAt(19, runs), -1);
        QCOMPARE(styleRunIndexAt(21, runs), -1);
    }
    void ebook02CodeLineCount() {
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("```cpp\nint a = 1;\nint b = 2;\n\nint c = 3;\n```\n"));
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        engine.setCodeHighlighting(QStringLiteral("Breeze Light"), true);
        auto layout = engine.layoutContinuous(doc, 420);
        const Layout::BlockBox *code = nullptr;
        for (const auto &element : layout.elements) {
            if (const auto *block = std::get_if<Layout::BlockBox>(&element)) {
                if (block->type == Layout::BlockBox::CodeBlockType)
                    code = block;
            }
        }
        QVERIFY(code);
        QCOMPARE(code->lines.size(), 5);
        bool colored = false;
        for (const auto &line : code->lines)
            for (const auto &glyph : line.glyphs)
                colored |= glyph.style.foreground.isValid() && glyph.style.foreground != code->lines.first().glyphs.value(0).style.foreground;
        Q_UNUSED(colored);
        bool marked = false;
        for (const auto &line : code->lines)
            for (const auto &glyph : line.glyphs)
                marked |= glyph.style.foreground.isValid();
        QVERIFY(marked);
    }
    void ebook02CancelBetweenCodeLines() {
        QString code = QStringLiteral("```cpp\n");
        for (int i = 0; i < 2000; ++i) code += QStringLiteral("int x%1 = %1;\n").arg(i);
        code += QStringLiteral("```\n");
        ContentBuilder builder;
        auto doc = builder.build(code);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        engine.setCancellationCheck([] { return true; });
        QCOMPARE(engine.layoutContinuous(doc, 400).elements.size(), 0);
    }
    void ebook02ClipSkipsLines() {
        QString code = QStringLiteral("```\n");
        for (int i = 0; i < 80; ++i) code += QStringLiteral("line %1\n").arg(i);
        code += QStringLiteral("```\n");
        ContentBuilder builder;
        auto doc = builder.build(code);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto layout = engine.layoutContinuous(doc, 400);
        const Layout::BlockBox *codeBox = nullptr;
        for (const auto &element : layout.elements) {
            if (const auto *block = std::get_if<Layout::BlockBox>(&element))
                if (block->type == Layout::BlockBox::CodeBlockType)
                    codeBox = block;
        }
        QVERIFY(codeBox);
        QImage image(20, 20, QImage::Format_RGB32);
        QPainter painter(&image);
        QtBoxRenderer renderer(&fonts);
        renderer.setPainter(&painter);
        renderer.setLineClip(QRectF(0, codeBox->y, 200, 30));
        renderer.renderBlockBox(*codeBox);
        const int clipped = renderer.lastPaintedLineCount();
        QVERIFY(clipped > 0);
        QVERIFY(clipped < codeBox->lines.size());
        renderer.setLineClip(QRectF());
        renderer.renderBlockBox(*codeBox);
        QCOMPARE(renderer.lastPaintedLineCount(), codeBox->lines.size());
    }
    void ebook03BuildAssignsUniqueIds() {
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("# Title\n\nParagraph.\n\n- outer\n  - nested\n\n> quoted\n\n| a |\n| --- |\n| b |\n\n```\ncode\n```\n\nNote[^n].\n\n[^n]: footnote\n"));
        QSet<quint64> ids;
        auto walk = [&](auto &&self, const QList<Content::BlockNode> &blocks) -> void {
            for (const auto &block : blocks) {
                const quint64 id = std::visit([](const auto &item) { return item.blockId; }, block);
                QVERIFY(id != 0);
                QVERIFY(!ids.contains(id));
                ids.insert(id);
                if (const auto *quote = std::get_if<Content::BlockQuote>(&block))
                    self(self, quote->children);
                if (const auto *list = std::get_if<Content::List>(&block)) {
                    for (const auto &item : list->items)
                        self(self, item.children);
                }
            }
        };
        walk(walk, doc.blocks);
        QVERIFY(ids.size() > 5);
    }
    void ebook03RestabilizeKeepsNeighbors() {
        ContentBuilder builder;
        auto before = builder.build(QStringLiteral("# Title\n\nAlpha.\n\nBeta.\n"));
        auto after = builder.build(QStringLiteral("# Title\n\nAlpha.\n\nInserted.\n\nBeta.\n"));
        const quint64 title = std::get<Content::Heading>(before.blocks.at(0)).blockId;
        const quint64 alpha = std::get<Content::Paragraph>(before.blocks.at(1)).blockId;
        const quint64 beta = std::get<Content::Paragraph>(before.blocks.at(2)).blockId;
        restabilizeBlockIds(before.blocks, after.blocks);
        QCOMPARE(std::get<Content::Heading>(after.blocks.at(0)).blockId, title);
        QCOMPARE(std::get<Content::Paragraph>(after.blocks.at(1)).blockId, alpha);
        QCOMPARE(std::get<Content::Paragraph>(after.blocks.at(3)).blockId, beta);
        QVERIFY(std::get<Content::Paragraph>(after.blocks.at(2)).blockId != alpha);
        QVERIFY(std::get<Content::Paragraph>(after.blocks.at(2)).blockId != beta);
        auto twins = builder.build(QStringLiteral("Same.\n\nSame.\n"));
        auto edited = builder.build(QStringLiteral("Same!\n\nSame.\n"));
        const quint64 first = std::get<Content::Paragraph>(twins.blocks.at(0)).blockId;
        const quint64 second = std::get<Content::Paragraph>(twins.blocks.at(1)).blockId;
        restabilizeBlockIds(twins.blocks, edited.blocks);
        QVERIFY(std::get<Content::Paragraph>(edited.blocks.at(0)).blockId != first);
        QCOMPARE(std::get<Content::Paragraph>(edited.blocks.at(1)).blockId, second);
    }
    void ebook03WorkerKeepsIdsAcrossInsert() {
        CompositionController controller;
        CompositionRequest request;
        request.markdown = QStringLiteral("# Title\n\nAlpha.\n\nBeta.\n");
        request.continuous = true;
        auto first = awaitReady(controller, request);
        const quint64 alpha = std::get<Content::Paragraph>(first->document.blocks.at(1)).blockId;
        const quint64 beta = std::get<Content::Paragraph>(first->document.blocks.at(2)).blockId;
        request.markdown = QStringLiteral("# Title\n\nAlpha.\n\nInserted.\n\nBeta.\n");
        auto second = awaitReady(controller, request);
        QCOMPARE(std::get<Content::Paragraph>(second->document.blocks.at(1)).blockId, alpha);
        QCOMPARE(std::get<Content::Paragraph>(second->document.blocks.at(3)).blockId, beta);
        QVERIFY(std::get<Content::Paragraph>(second->document.blocks.at(2)).blockId != alpha);
    }
    void ebook04FragmentCacheKeys() {
        FragmentCache cache;
        cache.setMaxCost(100000);
        Layout::LineBox line;
        line.width = 40;
        line.height = 12;
        const int before = cache.totalCost();
        QVERIFY(cache.storeParagraph(1, 2, QStringLiteral("Noto Serif"), 1100, 400, false, 200, false, {line}));
        QList<Layout::LineBox> loaded;
        QVERIFY(cache.loadParagraphBreaks(1, 2, QStringLiteral("Noto Serif"), 1100, 400, false, 200, false, &loaded));
        QCOMPARE(loaded.size(), 1);
        QVERIFY(!cache.loadParagraphBreaks(1, 2, QStringLiteral("Noto Serif"), 1100, 400, false, 240, false, &loaded));
        QVERIFY(cache.loadParagraphShapes(1, 2, QStringLiteral("Noto Serif"), 1100, 400, false, &loaded));
        QCOMPARE(loaded.size(), 1);
        const int cost = cache.totalCost();
        QVERIFY(cost > before);
        QVERIFY(!cache.storeParagraph(9, 9, QStringLiteral("Noto Serif"), 1100, 400, false, 200, false, {line}, {}, cache.maxCost() + 1));
        QCOMPARE(cache.totalCost(), cost);
    }
    void ebook04ParagraphCacheRoundTrip() {
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("One.\n\nTwo.\n\nThree.\n"));
        FontManager fonts;
        TextShaper shaper(&fonts);
        FragmentCache cache;
        Layout::Engine engine(&fonts, &shaper);
        engine.setFragmentCache(&cache);
        auto first = engine.layoutContinuous(doc, 360);
        auto second = engine.layoutContinuous(doc, 360);
        QCOMPARE(second.elements.size(), first.elements.size());
        QCOMPARE(second.sourceMap.size(), first.sourceMap.size());
        for (int i = 0; i < first.sourceMap.size(); ++i) {
            QCOMPARE(second.sourceMap.at(i).startLine, first.sourceMap.at(i).startLine);
            QCOMPARE(second.sourceMap.at(i).endLine, first.sourceMap.at(i).endLine);
        }
        auto wider = engine.layoutContinuous(doc, 400);
        QCOMPARE(wider.elements.size(), first.elements.size());
        for (int i = 0; i < first.elements.size(); ++i)
            QCOMPARE(glyphText(wider.elements.at(i)), glyphText(first.elements.at(i)));
        Layout::Engine plain(&fonts, &shaper);
        auto third = plain.layoutContinuous(doc, 360);
        QCOMPARE(third.elements.size(), first.elements.size());
        for (int i = 0; i < first.elements.size(); ++i) {
            const auto *a = std::get_if<Layout::BlockBox>(&first.elements.at(i));
            const auto *b = std::get_if<Layout::BlockBox>(&third.elements.at(i));
            QVERIFY(a && b);
            QCOMPARE(b->lines.size(), a->lines.size());
        }
    }
    void ebook04CodeLineCacheHits() {
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("```cpp\nint value0 = 0;\n```\n"));
        auto &block = std::get<Content::CodeBlock>(doc.blocks.first());
        QStringList codeLines;
        for (int i = 0; i < 30; ++i)
            codeLines.append(QStringLiteral("int value%1 = %1;").arg(i));
        block.code = codeLines.join(u'\n');
        FontManager fonts;
        TextShaper shaper(&fonts);
        FragmentCache cache;
        Layout::Engine engine(&fonts, &shaper);
        engine.setCodeHighlighting(QStringLiteral("Breeze Light"), true);
        engine.setFragmentCache(&cache);
        engine.layoutContinuous(doc, 400);
        cache.breakHits = 0;
        cache.breakMisses = 0;
        engine.layoutContinuous(doc, 400);
        QCOMPARE(cache.breakHits, quint64(30));
        auto edited = doc;
        auto &editedBlock = std::get<Content::CodeBlock>(edited.blocks.first());
        const QStringList lines = editedBlock.code.split(u'\n');
        QStringList changed = lines;
        changed[2] = QStringLiteral("int changed = 3;");
        editedBlock.code = changed.join(u'\n');
        cache.breakHits = 0;
        cache.breakMisses = 0;
        engine.layoutContinuous(edited, 400);
        QCOMPARE(cache.breakHits, quint64(29));
        QCOMPARE(cache.breakMisses, quint64(1));
    }
    void ebook05ContinuousCursorMatchesFull() {
        QString markdown;
        for (int i = 0; i < 12; ++i) markdown += QStringLiteral("Paragraph %1.\n\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto full = engine.layoutContinuous(doc, 380);
        Layout::LayoutCursor cursor;
        Layout::LayoutBudget budget;
        budget.maxBlocks = 5;
        QList<Layout::PageElement> combined;
        QList<int> lines;
        while (!cursor.finished) {
            auto part = engine.layoutContinuousFrom(doc, 380, &cursor, budget);
            combined.append(part.elements);
            for (const auto &entry : part.sourceMap) lines.append(entry.startLine);
            if (part.elements.isEmpty()) break;
        }
        QCOMPARE(combined.size(), full.elements.size());
        QList<int> fullLines;
        for (const auto &entry : full.sourceMap) fullLines.append(entry.startLine);
        QCOMPARE(lines, fullLines);
        Layout::LayoutCursor once;
        auto all = engine.layoutContinuousFrom(doc, 380, &once, {});
        QCOMPARE(all.elements.size(), full.elements.size());
        QVERIFY(once.finished);
    }
    void ebook05PagedCursorMatchesFull() {
        QString markdown = QStringLiteral("| Column |\n| --- |\n");
        for (int i = 0; i < 40; ++i) markdown += QStringLiteral("| Row %1 |\n").arg(i);
        for (int i = 0; i < 20; ++i) markdown += QStringLiteral("Paragraph %1.\n\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout layout;
        auto full = engine.layout(doc, layout);
        Layout::LayoutCursor cursor;
        Layout::LayoutResult paged;
        paged.pageSize = layout.pageSizePoints();
        Layout::Page page;
        while (engine.layoutNextPage(doc, layout, &cursor, &page)) {
            paged.pages.append(page);
            page = {};
        }
        engine.recordSourceMap(paged, layout);
        QCOMPARE(paged.pages.size(), full.pages.size());
        for (int i = 0; i < full.pages.size(); ++i)
            QCOMPARE(paged.pages.at(i).elements.size(), full.pages.at(i).elements.size());
        QCOMPARE(paged.sourceMap.size(), full.sourceMap.size());
        for (int i = 0; i < full.sourceMap.size(); ++i) {
            QCOMPARE(paged.sourceMap.at(i).startLine, full.sourceMap.at(i).startLine);
            QCOMPARE(paged.sourceMap.at(i).endLine, full.sourceMap.at(i).endLine);
        }
    }
    void ebook05WorkerHonorsBlockBudget() {
        CompositionController controller;
        QString markdown;
        for (int i = 0; i < 30; ++i) markdown += QStringLiteral("Paragraph %1.\n\n").arg(i);
        CompositionRequest request;
        request.markdown = markdown;
        request.continuous = true;
        request.maxBlocks = 4;
        request.width = 400;
        auto partial = awaitReady(controller, request);
        QVERIFY(!partial->finished);
        QVERIFY(!partial->continuousLayout.elements.isEmpty());
        QVERIFY(partial->continuousLayout.elements.size() < 30);
        request.maxBlocks = 0;
        auto full = awaitReady(controller, request);
        QVERIFY(full->finished);
        QCOMPARE(full->continuousLayout.elements.size(), 30);
    }
    void ebook06AppendDoesNotResetScroll() {
        QString markdown;
        for (int i = 0; i < 30; ++i) markdown += QStringLiteral("Paragraph %1 has several words.\n\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        Layout::LayoutCursor cursor;
        Layout::LayoutBudget budget;
        budget.maxBlocks = 20;
        auto head = engine.layoutContinuousFrom(doc, 400, &cursor, budget);
        auto tail = engine.layoutContinuousFrom(doc, 400, &cursor, budget);
        DocumentView view;
        view.resize(500, 220);
        view.setRenderMode(DocumentView::WebMode);
        view.setWebFontManager(&fonts);
        view.setWebContent(std::move(head));
        view.show();
        QTest::qWait(20);
        auto *bar = view.verticalScrollBar();
        QVERIFY(bar->maximum() > 0);
        bar->setValue(qMax(1, bar->maximum() / 3));
        const int value = bar->value();
        const qreal before = view.sceneRect().height();
        view.appendWebContent(std::move(tail));
        QCOMPARE(bar->value(), value);
        QVERIFY(view.sceneRect().height() > before);
    }
    void ebook06PrefixDoesNotLayoutEveryBlock() {
        QString markdown;
        for (int i = 0; i < 200; ++i) markdown += QStringLiteral("Paragraph %1.\n\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto full = engine.layoutContinuous(doc, 500);
        CompositionController controller;
        CompositionRequest request;
        request.markdown = markdown;
        request.continuous = true;
        request.width = 500;
        request.maxHeight = 1600;
        auto first = awaitReady(controller, request);
        QVERIFY(!first->finished);
        QVERIFY(first->continuousLayout.elements.size() < 200);
        int count = first->continuousLayout.elements.size();
        auto cursor = first->cursor;
        int guard = 0;
        while (!cursor.finished && guard++ < 30) {
            request.startBlock = cursor.nextBlock;
            request.maxHeight = 800;
            auto next = awaitReady(controller, request);
            count += next->continuousLayout.elements.size();
            cursor = next->cursor;
        }
        QVERIFY(cursor.finished);
        QCOMPARE(count, full.elements.size());
    }
    void ebook06NearEndRequestsMore() {
        QString markdown;
        for (int i = 0; i < 25; ++i) markdown += QStringLiteral("Paragraph %1 has enough text to grow.\n\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto layout = engine.layoutContinuous(doc, 360);
        DocumentView view;
        view.resize(420, 180);
        view.setRenderMode(DocumentView::WebMode);
        view.setWebFontManager(&fonts);
        view.setWebContent(std::move(layout));
        Layout::LayoutCursor cursor;
        cursor.finished = false;
        cursor.nextBlock = 10;
        view.setLayoutCursor(cursor);
        view.show();
        QTest::qWait(20);
        auto *bar = view.verticalScrollBar();
        QVERIFY(bar->maximum() > 0);
        QSignalSpy spy(&view, &DocumentView::moreLayoutRequested);
        bar->setValue(qMax(0, bar->maximum() - 1));
        bar->setValue(bar->maximum());
        QCOMPARE(spy.size(), 1);
        bar->setValue(qMax(0, bar->maximum() - 1));
        bar->setValue(bar->maximum());
        QCOMPARE(spy.size(), 1);
        view.layoutExtended();
        bar->setValue(qMax(0, bar->maximum() - 1));
        bar->setValue(bar->maximum());
        QCOMPARE(spy.size(), 2);
    }
    void ebook06WidthChangeReusesShapes() {
        QString markdown;
        for (int i = 0; i < 40; ++i)
            markdown += QStringLiteral("Varied paragraph %1 uses words %2.\n\n").arg(i).arg(i * 17);
        CompositionController controller;
        CompositionRequest request;
        request.markdown = markdown;
        request.continuous = true;
        request.width = 400;
        awaitReady(controller, request);
        request.width = 460;
        request.maxBlocks = 40;
        auto second = awaitReady(controller, request);
        QVERIFY(second->shapeHits > 0);
        QVERIFY(second->reusedContent);
        QVERIFY(second->finished);
    }
    void ebook07MarginEditsCoalesce() {
        PageLayoutWidget widget;
        QSignalSpy spy(&widget, &PageLayoutWidget::pageLayoutChanged);
        auto spins = widget.findChildren<QDoubleSpinBox *>();
        QVERIFY(!spins.isEmpty());
        auto *spin = spins.first();
        spin->setValue(spin->value() + 1);
        QTest::qWait(10);
        spin->setValue(spin->value() + 1);
        QTest::qWait(10);
        spin->setValue(spin->value() + 1);
        QCOMPARE(spy.size(), 0);
        QTest::qWait(200);
        QCOMPARE(spy.size(), 1);
    }
    void ebook07FirstPageBeforeLast() {
        QString markdown;
        for (int i = 0; i < 40; ++i)
            markdown += QString(QStringLiteral("Paragraph %1 is a sentence. ").arg(i).repeated(40)) + QStringLiteral("\n\n");
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout layout;
        CompositionController controller;
        CompositionRequest request;
        request.markdown = markdown;
        request.continuous = false;
        request.onePage = true;
        request.hyphenateJustified = false;
        auto page = awaitReady(controller, request);
        const int fullPages = engine.layout(page->document, layout).pages.size();
        QCOMPARE(page->paginated.pages.size(), 1);
        QVERIFY(!page->finished);
        int count = 1;
        auto cursor = page->cursor;
        int guard = 0;
        while (!cursor.finished && guard++ < 40) {
            request.startBlock = cursor.nextBlock;
            auto next = awaitReady(controller, request);
            count += next->paginated.pages.size();
            cursor = next->cursor;
        }
        QVERIFY(cursor.finished);
        QCOMPARE(count, fullPages);
    }
    void ebook07ZoomDoesNotRelayout() {
        DocumentView view;
        view.resize(640, 480);
        view.setRenderMode(DocumentView::PrintMode);
        QSignalSpy spy(&view, &DocumentView::webRelayoutRequested);
        view.setZoomPercent(150);
        QCOMPARE(spy.size(), 0);
        QResizeEvent printResize(QSize(700, 480), QSize(640, 480));
        QApplication::sendEvent(&view, &printResize);
        QCOMPARE(spy.size(), 0);
        view.setRenderMode(DocumentView::WebMode);
        view.show();
        view.resize(760, 480);
        QResizeEvent webResize(QSize(820, 480), view.size());
        QApplication::sendEvent(&view, &webResize);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 1000);
    }
    void ebook08BoxPagePaintsAfterFontsReleased() {
        std::shared_ptr<FontManager> fonts = std::make_shared<FontManager>();
        QSizeF pageSize;
        Layout::Page page;
        {
            TextShaper shaper(fonts.get());
            Layout::Engine engine(fonts.get(), &shaper);
            ContentBuilder builder;
            auto doc = builder.build(QStringLiteral("Hello from a box page.\n"));
            PageLayout layout;
            auto result = engine.layout(doc, layout);
            QVERIFY(!result.pages.isEmpty());
            page = result.pages.first();
            pageSize = result.pageSize;
        }
        BoxPageItem item(std::make_shared<Layout::Page>(page), pageSize, fonts);
        QImage image(qMax(1, int(pageSize.width())), qMax(1, int(pageSize.height())), QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        QStyleOptionGraphicsItem option;
        option.exposedRect = item.boundingRect();
        item.paint(&painter, &option, nullptr);
        painter.end();
        int colored = 0;
        for (int y = 0; y < image.height(); y += 3) {
            for (int x = 0; x < image.width(); x += 3) {
                if (image.pixelColor(x, y) != QColor(Qt::white))
                    ++colored;
            }
        }
        QVERIFY(colored > 0);
    }
    void ebook08HeaderPlanTokens() {
        PageLayout layout;
        layout.footerEnabled = true;
        layout.footerRight = QStringLiteral("{page} / {pages}");
        layout.headerEnabled = true;
        layout.headerLeft = QStringLiteral("{filename}");
        const auto plan = makeHeaderFooterPlan(layout, 1, 9, QStringLiteral("a.md"), QStringLiteral("Title"));
        QVERIFY(plan.footerRight.text.contains(QStringLiteral("2")));
        QVERIFY(plan.footerRight.text.contains(QStringLiteral("9")));
        QVERIFY(!plan.footerRight.text.contains(QStringLiteral("{page}")));
        QVERIFY(plan.headerLeft.text.contains(QStringLiteral("a.md")));
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("Footer check.\n"));
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto result = engine.layout(doc, layout);
        PdfGenerator generator(&fonts);
        generator.setDocumentInfo(QStringLiteral("a.md"), QStringLiteral("Title"));
        auto pdf = generator.generate(result, layout, QStringLiteral("Title"));
        auto poppler = Poppler::Document::loadFromData(pdf);
        QVERIFY(poppler);
        std::unique_ptr<Poppler::Page> page(poppler->page(0));
        const QString extracted = page->text(QRectF());
        QVERIFY2(extracted.contains(QStringLiteral("1 /")), qPrintable(extracted));
    }
    void ebook08InteractivePrintHasNoPdf() {
        CompositionController controller;
        CompositionRequest request;
        request.markdown = QStringLiteral("# Page\n\n");
        for (int i = 0; i < 30; ++i)
            request.markdown += QString(QStringLiteral("Paragraph %1. ").arg(i).repeated(40)) + QStringLiteral("\n\n");
        request.continuous = false;
        request.onePage = true;
        request.interactivePages = true;
        auto result = awaitReady(controller, request);
        QVERIFY(result->pdf.isEmpty());
        QCOMPARE(result->paginated.pages.size(), 1);
        QVERIFY(!result->finished);
    }
    void largeTextSurvivesGlyphCacheLimit() {
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("# Zoomed Heading\n"));
        auto layout = engine.layoutContinuous(doc, 400);
        const Layout::BlockBox *heading = nullptr;
        for (const auto &element : layout.elements)
            if (const auto *block = std::get_if<Layout::BlockBox>(&element))
                if (block->type == Layout::BlockBox::HeadingBlock)
                    heading = block;
        QVERIFY(heading);
        QVERIFY(!heading->lines.isEmpty());

        QImage image(2400, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.scale(6.0, 6.0);
        QtBoxRenderer renderer(&fonts);
        renderer.setPainter(&painter);
        renderer.renderBlockBox(*heading);
        painter.end();

        int colored = 0;
        for (int y = 0; y < image.height(); y += 3)
            for (int x = 0; x < image.width(); x += 3)
                if (image.pixelColor(x, y) != QColor(Qt::white))
                    ++colored;
        QVERIFY2(colored > 0, "heading glyphs vanished when scaled past the glyph cache limit");
    }
    void ebook09HitIndexFindsLine() {
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("First paragraph stays on the page.\n\nSecond paragraph is the target.\n"));
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout layout;
        auto result = engine.layout(doc, layout);
        QVERIFY(!result.pages.isEmpty());
        const auto &page = result.pages.first();
        const auto *first = std::get_if<Layout::BlockBox>(&page.elements.first());
        QVERIFY(first);
        const auto index = buildHitIndex(page);
        const int second = first->lines.size();
        QVERIFY(second < index.lineBounds.size());
        QCOMPARE(hitLine(index, index.lineBounds.at(second).center()), second);
        QCOMPARE(hitLine(index, QPointF(-20, -20)), -1);
    }
    void ebook09BoxLinkAndSelection() {
        ContentBuilder builder;
        auto doc = builder.build(QStringLiteral("See [go](https://example.com) now.\n"));
        auto fonts = std::make_shared<FontManager>();
        TextShaper shaper(fonts.get());
        Layout::Engine engine(fonts.get(), &shaper);
        PageLayout layout;
        auto result = engine.layout(doc, layout);
        BoxPageItem item(std::make_shared<Layout::Page>(result.pages.first()), result.pageSize, fonts);
        item.setPageChrome(layout, 1, QStringLiteral("a.md"), QStringLiteral("a"));
        const auto &index = item.hitIndex();
        QVERIFY(!index.linkBounds.isEmpty());
        constexpr qreal mmToPt = 72.0 / 25.4;
        const QPointF origin(layout.margins.left() * mmToPt,
                             layout.margins.top() * mmToPt + layout.headerTotalHeight());
        const QRectF word = index.linkBounds.first().translated(origin);
        QCOMPARE(item.linkAt(word.center()), QStringLiteral("https://example.com"));
        const QString text = item.textInRect(word);
        QVERIFY2(text.contains(QStringLiteral("go")), qPrintable(text + QStringLiteral(" | ") + index.lineText.join(QStringLiteral("/"))));
        QVERIFY(!text.contains(QStringLiteral("https://example.com")));
    }
    void ebook09FindResumesCursor() {
        DocumentView view;
        view.requestScrollToBlock(12);
        Layout::LayoutCursor cursor;
        cursor.nextBlock = 10;
        cursor.finished = false;
        view.integrateCursor(cursor);
        QCOMPARE(view.scrollToBlock(), 12);
        cursor.nextBlock = 12;
        view.integrateCursor(cursor);
        QCOMPARE(view.scrollToBlock(), 12);
        cursor.nextBlock = 14;
        view.integrateCursor(cursor, 12, 480);
        QCOMPARE(view.scrollToBlock(), -1);
        QCOMPARE(view.scrollTargetY(), 480);
    }
    void ebook10FingerprintStable() {
        Layout::LayoutCursor cursor;
        cursor.y = 12;
        cursor.yOnPage = 12;
        cursor.nextBlock = 3;
        const QList<quint64> ids{4, 5};
        const quint64 hash = 99;
        const quint64 first = pageFingerprint(cursor, ids, hash, false, 0, 0);
        const quint64 second = pageFingerprint(cursor, ids, hash, false, 0, 0);
        QCOMPARE(second, first);
        cursor.y = 40;
        cursor.yOnPage = 40;
        QVERIFY(pageFingerprint(cursor, ids, hash, false, 0, 0) != first);
        cursor.y = 12;
        cursor.yOnPage = 12;
        QVERIFY(pageFingerprint(cursor, {4, 6}, hash, false, 0, 0) != first);
    }
    void ebook10ReuseTailAfterInsert() {
        PageLayout layout;
        layout.footerEnabled = false;
        layout.footerRight.clear();
        // One short paragraph per page, so an early page ends on a block boundary
        // and does not contain the edited tail paragraph.
        layout.margins = QMarginsF(20, 145, 20, 145);
        QString markdown;
        for (int i = 0; i < 15; ++i)
            markdown += QStringLiteral("Paragraph %1.\n\n").arg(i);
        ContentBuilder builder;
        auto doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        auto stored = engine.layout(doc, layout);
        QVERIFY2(stored.pages.size() > 1, qPrintable(QString::number(stored.pages.size())));
        quint64 editedId = 0;
        for (const auto &node : doc.blocks) {
            Content::Document one;
            one.blocks.append(node);
            if (Content::toPlainText(one).contains(QStringLiteral("Paragraph 14.")))
                editedId = std::visit([](const auto &block) { return block.blockId; }, node);
        }
        QVERIFY(editedId != 0);
        int prefix = -1;
        for (int i = 0; i < stored.pages.size(); ++i) {
            const auto &candidate = stored.pages.at(i);
            if (candidate.boundary && !candidate.hasTable && !candidate.blockIds.isEmpty()
                && !candidate.blockIds.contains(editedId)) {
                prefix = i;
                break;
            }
        }
        QVERIFY(prefix >= 0);
        QString edited = markdown;
        edited.replace(QStringLiteral("Paragraph 14."), QStringLiteral("Paragraph fourteen."));
        auto docEdited = builder.build(edited);
        restabilizeBlockIds(doc.blocks, docEdited.blocks);
        auto mutated = stored;
        auto &prefixPage = mutated.pages[prefix];
        if (auto *block = std::get_if<Layout::BlockBox>(&prefixPage.elements.first()))
            block->y = 4242;
        auto reused = engine.layoutReusing(docEdited, layout, &mutated, false);
        QCOMPARE(reused.pages.at(prefix).blockIds, stored.pages.at(prefix).blockIds);
        if (const auto *block = std::get_if<Layout::BlockBox>(&reused.pages.at(prefix).elements.first()))
            QCOMPARE(block->y, 4242);
        auto fullEdited = engine.layout(docEdited, layout);
        QCOMPARE(reused.pages.size(), fullEdited.pages.size());
        QCOMPARE(lastSourceLine(reused), lastSourceLine(fullEdited));

        auto inserted = builder.build(QStringLiteral("Inserted on the first page.\n\n") + markdown);
        restabilizeBlockIds(doc.blocks, inserted.blocks);
        auto rebuilt = engine.layoutReusing(inserted, layout, &stored, false);
        auto fullInserted = engine.layout(inserted, layout);
        QCOMPARE(rebuilt.pages.size(), fullInserted.pages.size());
        QCOMPARE(lastSourceLine(rebuilt), lastSourceLine(fullInserted));

        auto flipped = stored;
        flipped.pages[0].fingerprint ^= 1;
        if (auto *block = std::get_if<Layout::BlockBox>(&flipped.pages[0].elements.first()))
            block->y = 4242;
        auto fresh = engine.layoutReusing(doc, layout, &flipped, false);
        auto full = engine.layout(doc, layout);
        QCOMPARE(fresh.pages.size(), full.pages.size());
        QCOMPARE(lastSourceLine(fresh), lastSourceLine(full));
        if (const auto *block = std::get_if<Layout::BlockBox>(&fresh.pages.first().elements.first()))
            QVERIFY(!qFuzzyCompare(block->y, 4242));
    }

    void webModeClearsPrintAndKeepsScrolling() {
        DocumentView view;
        view.resize(640, 480);
        view.show();
        PageLayout layout;
        layout.pageBackground = QColor(QStringLiteral("#282828"));
        auto fonts = std::make_shared<FontManager>();
        auto page = std::make_shared<Layout::Page>();
        page->pageNumber = 0;
        view.setViewMode(DocumentView::SinglePage);
        view.setBoxPages({page}, fonts, layout, false);
        QVERIFY(view.hasBoxPages());
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        view.setRenderMode(DocumentView::WebMode);
        QVERIFY(!view.hasBoxPages());
        QVERIFY(!view.webViewItem());
        QCOMPARE(view.scene()->items().size(), 0);
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        QCOMPARE(view.backgroundBrush().color(), QColor(QStringLiteral("#282828")));
        view.setViewMode(DocumentView::FacingPages);
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        view.setWebFontManager(fonts.get());
        Layout::ContinuousLayoutResult column;
        column.contentWidth = 400;
        column.totalHeight = 2400;
        view.setWebContent(std::move(column));
        QVERIFY(view.webViewItem());
        QVERIFY(view.verticalScrollBar()->maximum() > 0);
        view.setRenderMode(DocumentView::PrintMode);
        QVERIFY(!view.webViewItem());
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    }
    void darkCodeSurfaceUsesLightSyntaxColors() {
        auto keyword = [](const QColor &background) {
            Content::CodeBlock block;
            block.language = QStringLiteral("cpp");
            block.code = QStringLiteral("int value = 1;\n");
            block.background = background;
            block.style.foreground = QColor(QStringLiteral("#1a1a1a"));
            block.style.fontFamily = QStringLiteral("Noto Sans");
            block.style.fontSize = 10;
            Content::Document doc;
            doc.blocks.append(block);
            FontManager fonts;
            TextShaper shaper(&fonts);
            Layout::Engine engine(&fonts, &shaper);
            engine.setCodeHighlighting(QString(), true);
            const auto layout = engine.layoutContinuous(doc, 420);
            for (const auto &element : layout.elements) {
                const auto *box = std::get_if<Layout::BlockBox>(&element);
                if (!box) continue;
                for (const auto &line : box->lines)
                    for (const auto &glyph : line.glyphs)
                        if (glyph.text == QLatin1String("int"))
                            return glyph.style.foreground;
            }
            return QColor();
        };
        const QColor onLight = keyword(QColor(Qt::white));
        const QColor onDark = keyword(QColor(QStringLiteral("#3c3836")));
        QVERIFY(onLight.isValid());
        QVERIFY(onDark.isValid());
        QVERIFY2(onDark.lightness() > onLight.lightness(),
                 qPrintable(onLight.name() + QLatin1Char(' ') + onDark.name()));
    }
    void webViewScrollSurvivesLayout() {
        PenelopeSettings::self()->setUseWebView(true);
        QTemporaryDir dir;
        QString markdown = QStringLiteral("# Title\n\n");
        for (int i = 0; i < 40; ++i)
            markdown += QStringLiteral("Paragraph %1. The quick brown fox jumps over the lazy dog.\n\n").arg(i);
        const QString file = dir.filePath(QStringLiteral("scroll.md"));
        QVERIFY(writeFile(file, markdown.toUtf8()));
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.openFile(QUrl::fromLocalFile(file));
        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        auto *tab = qobject_cast<DocumentTab *>(tabs->currentWidget());
        QVERIFY(tab);
        auto *view = tab->documentView();
        QTRY_VERIFY_WITH_TIMEOUT(view->webViewItem()
                                 && view->verticalScrollBar()->maximum() > 100, 10000);
        QTest::qWait(500);
        QCOMPARE(view->scene()->items().size(), 1);
        auto *bar = view->verticalScrollBar();
        const int mid = bar->maximum() / 2;
        bar->setValue(mid);
        QTest::qWait(600);
        QVERIFY2(bar->value() > mid / 2, qPrintable(QString::number(bar->value())));
        QCOMPARE(view->scene()->items().size(), 1);
    }
    void contentsFollowsScrollAndJump() {
        PenelopeSettings::self()->setUseWebView(true);
        QTemporaryDir dir;
        QString markdown = QStringLiteral("# Alpha\n\n");
        for (int i = 0; i < 40; ++i)
            markdown += QStringLiteral("Alpha paragraph %1 fills the column before the next heading.\n\n").arg(i);
        markdown += QStringLiteral("# Beta\n\n");
        for (int i = 0; i < 40; ++i)
            markdown += QStringLiteral("Beta paragraph %1 keeps this section well below the first screen.\n\n").arg(i);
        markdown += QStringLiteral("# Gamma\n\nGamma is last.\n");
        const QString file = dir.filePath(QStringLiteral("toc.md"));
        QVERIFY(writeFile(file, markdown.toUtf8()));
        MainWindow window;
        window.resize(900, 640);
        window.show();
        window.openFile(QUrl::fromLocalFile(file));
        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        auto *tab = qobject_cast<DocumentTab *>(tabs->currentWidget());
        QVERIFY(tab);
        auto *view = tab->documentView();
        auto *toc = window.findChild<TocWidget *>();
        QVERIFY(toc);
        auto *tree = toc->findChild<QTreeWidget *>();
        QVERIFY(tree);
        auto headingText = [](QTreeWidgetItem *item) {
            QString text = item ? item->text(0) : QString();
            text.remove(QChar(0x00AD));
            return text;
        };
        QTRY_VERIFY_WITH_TIMEOUT(tree->topLevelItemCount() >= 3 && view->webViewItem(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Alpha"), 2000);
        QTreeWidgetItem *beta = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((beta = tree->topLevelItem(1))
                                 && headingText(beta) == QStringLiteral("Beta")
                                 && beta->data(0, Qt::UserRole).toInt() >= 0, 10000);
        const qreal betaY = beta->data(0, Qt::UserRole + 1).toDouble();
        QVERIFY(betaY > 200);
        view->scrollToPosition(0, betaY);
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Beta"), 2000);
        view->verticalScrollBar()->setValue(0);
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Alpha"), 2000);
        auto *gamma = tree->topLevelItem(2);
        QVERIFY(gamma);
        Q_EMIT tree->itemClicked(gamma, 0);
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Gamma"), 10000);

        auto *printAction = window.actionCollection()->action(QStringLiteral("view_print_mode"));
        QVERIFY(printAction);
        printAction->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(view->hasBoxPages(), 10000);
        QTreeWidgetItem *printedBeta = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((printedBeta = tree->topLevelItem(1))
                                 && headingText(printedBeta) == QStringLiteral("Beta")
                                 && printedBeta->data(0, Qt::UserRole).toInt() >= 0, 15000);
        view->scrollToPosition(printedBeta->data(0, Qt::UserRole).toInt(),
                               printedBeta->data(0, Qt::UserRole + 1).toDouble());
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Beta"), 2000);
        view->verticalScrollBar()->setValue(0);
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Alpha"), 2000);
        Q_EMIT tree->itemClicked(tree->topLevelItem(2), 0);
        QTRY_COMPARE_WITH_TIMEOUT(headingText(tree->currentItem()), QStringLiteral("Gamma"), 15000);
    }
    void styleResolutionFollowsTypeSetAndPalette() {
        TypeSet typeSet;
        typeSet.body.family = QStringLiteral("Georgia");
        typeSet.heading.family = QStringLiteral("Trebuchet MS");
        typeSet.mono.family = QStringLiteral("Courier New");
        QJsonObject paragraphs;
        paragraphs.insert(QStringLiteral("Heading"),
                          QJsonObject{{QStringLiteral("foreground"), QStringLiteral("#ff00ff")}});
        paragraphs.insert(QStringLiteral("Heading1"),
                          QJsonObject{{QStringLiteral("fontSize"), 28},
                                      {QStringLiteral("fontWeight"), QStringLiteral("bold")}});
        paragraphs.insert(QStringLiteral("BodyText"),
                          QJsonObject{{QStringLiteral("alignment"), QStringLiteral("justify")},
                                      {QStringLiteral("lineHeightPercent"), 120}});
        typeSet.paragraphStyles = paragraphs;
        QJsonObject characters;
        characters.insert(QStringLiteral("InlineCode"),
                          QJsonObject{{QStringLiteral("foreground"), QStringLiteral("#ff0000")}});
        typeSet.characterStyles = characters;

        ColorPalette palette;
        palette.colors.insert(QStringLiteral("headingText"), QColor(QStringLiteral("#fabd2f")));
        palette.colors.insert(QStringLiteral("text"), QColor(QStringLiteral("#ebdbb2")));
        palette.colors.insert(QStringLiteral("codeText"), QColor(QStringLiteral("#b8bb26")));
        palette.colors.insert(QStringLiteral("surfaceCode"), QColor(QStringLiteral("#3c3836")));

        ThemeManager themes;
        ThemeComposer composer(&themes);
        composer.setTypeSet(typeSet);
        composer.setColorPalette(palette);
        StyleManager styles;
        composer.compose(&styles);

        const ParagraphStyle heading = styles.resolvedParagraphStyle(QStringLiteral("Heading1"));
        QCOMPARE(heading.fontFamily(), QStringLiteral("Trebuchet MS"));
        QCOMPARE(heading.fontSize(), 28.0);
        QCOMPARE(heading.fontWeight(), QFont::Bold);
        QCOMPARE(heading.foreground(), QColor(QStringLiteral("#fabd2f")));

        const ParagraphStyle body = styles.resolvedParagraphStyle(QStringLiteral("BodyText"));
        QCOMPARE(body.fontFamily(), QStringLiteral("Georgia"));
        QCOMPARE(body.alignment(), Qt::AlignJustify);
        QCOMPARE(body.lineHeightPercent(), 120);
        QCOMPARE(body.foreground(), QColor(QStringLiteral("#ebdbb2")));
        QVERIFY(body.foreground() != QColor(0x1a, 0x1a, 0x1a));

        const CharacterStyle emphasis = styles.characterStyleOverrides(QStringLiteral("Emphasis"));
        QVERIFY(emphasis.fontItalic());
        QVERIFY(!emphasis.hasFontFamily());
        const CharacterStyle strong = styles.characterStyleOverrides(QStringLiteral("Strong"));
        QCOMPARE(strong.fontWeight(), QFont::Bold);
        QVERIFY(!strong.fontItalic());

        ContentBuilder builder;
        builder.setStyleManager(&styles);
        const Content::Document doc = builder.build(
            QStringLiteral("# *Emphasized* and [linked](https://example.test)\n\n"
                           "Body *italicword* **boldword** ***bothword*** `codeword` ~~gone~~.\n\n"
                           "```\n"
                           "fenceword\n"
                           "```\n"));
        const auto styled = [](const Content::Document &document, const QString &needle) -> const Content::TextStyle * {
            for (const auto &block : document.blocks) {
                const QList<Content::InlineNode> *inlines = nullptr;
                if (const auto *headingBlock = std::get_if<Content::Heading>(&block))
                    inlines = &headingBlock->inlines;
                else if (const auto *paragraph = std::get_if<Content::Paragraph>(&block))
                    inlines = &paragraph->inlines;
                if (!inlines)
                    continue;
                for (const auto &node : *inlines) {
                    if (const auto *run = std::get_if<Content::TextRun>(&node)) {
                        if (run->text.contains(needle))
                            return &run->style;
                    } else if (const auto *code = std::get_if<Content::InlineCode>(&node)) {
                        if (code->text.contains(needle))
                            return &code->style;
                    }
                }
            }
            return nullptr;
        };
        const Content::TextStyle *emphasized = styled(doc, QStringLiteral("Emphasized"));
        QVERIFY(emphasized);
        QCOMPARE(emphasized->fontFamily, QStringLiteral("Trebuchet MS"));
        QCOMPARE(emphasized->fontSize, 28.0);
        QVERIFY(emphasized->italic);
        QCOMPARE(emphasized->foreground, QColor(QStringLiteral("#fabd2f")));

        const Content::TextStyle *linked = styled(doc, QStringLiteral("linked"));
        QVERIFY(linked);
        QCOMPARE(linked->fontFamily, QStringLiteral("Trebuchet MS"));
        QVERIFY(linked->underline);
        QCOMPARE(linked->linkHref, QStringLiteral("https://example.test"));

        const Content::TextStyle *italic = styled(doc, QStringLiteral("italicword"));
        QVERIFY(italic);
        QCOMPARE(italic->fontFamily, QStringLiteral("Georgia"));
        QVERIFY(italic->italic);
        QCOMPARE(italic->fontWeight, 400);
        QCOMPARE(italic->foreground, QColor(QStringLiteral("#ebdbb2")));

        const Content::TextStyle *bold = styled(doc, QStringLiteral("boldword"));
        QVERIFY(bold);
        QCOMPARE(bold->fontWeight, 700);
        QVERIFY(!bold->italic);
        QCOMPARE(bold->fontFamily, QStringLiteral("Georgia"));

        const Content::TextStyle *both = styled(doc, QStringLiteral("bothword"));
        QVERIFY(both);
        QCOMPARE(both->fontWeight, 700);
        QVERIFY(both->italic);

        const Content::TextStyle *code = styled(doc, QStringLiteral("codeword"));
        QVERIFY(code);
        QCOMPARE(code->fontFamily, QStringLiteral("Courier New"));
        QCOMPARE(code->foreground, QColor(QStringLiteral("#b8bb26")));

        const Content::TextStyle *struck = styled(doc, QStringLiteral("gone"));
        QVERIFY(struck);
        QVERIFY(struck->strikethrough);
        QCOMPARE(struck->fontFamily, QStringLiteral("Georgia"));

        const Content::CodeBlock *fence = nullptr;
        for (const auto &block : doc.blocks) {
            if (const auto *codeBlock = std::get_if<Content::CodeBlock>(&block))
                fence = codeBlock;
        }
        QVERIFY(fence);
        QVERIFY(fence->code.contains(QStringLiteral("fenceword")));
        QCOMPARE(fence->style.fontFamily, QStringLiteral("Courier New"));
        QCOMPARE(fence->background, QColor(QStringLiteral("#3c3836")));

        ParagraphStyle indent(QStringLiteral("CodeIndent"));
        indent.setParentStyleName(QStringLiteral("CodeBlock"));
        indent.setLeftMargin(18);
        styles.addParagraphStyle(indent);
        const ParagraphStyle resolvedIndent = styles.resolvedParagraphStyle(QStringLiteral("CodeIndent"));
        QCOMPARE(resolvedIndent.fontFamily(), QStringLiteral("Courier New"));
        QCOMPARE(resolvedIndent.leftMargin(), 18.0);

        FootnoteStyle prior = styles.footnoteStyle();
        prior.suffix = QStringLiteral(")");
        prior.prefix = QStringLiteral("(");
        styles.setFootnoteStyle(prior);
        QJsonObject footnote{{QStringLiteral("format"), QStringLiteral("roman_lower")}};
        themes.applyStyleOverrides(QJsonObject{{QStringLiteral("footnoteStyle"), footnote}}, &styles);
        QCOMPARE(styles.footnoteStyle().format, FootnoteStyle::RomanLower);
        QCOMPARE(styles.footnoteStyle().suffix, QStringLiteral(")"));
        QCOMPARE(styles.footnoteStyle().prefix, QStringLiteral("("));
        QVERIFY(styles.footnoteStyle().superscriptRef);

        CharacterStyle cycleA(QStringLiteral("CycleA"));
        cycleA.setParentStyleName(QStringLiteral("CycleB"));
        cycleA.setFontItalic(true);
        CharacterStyle cycleB(QStringLiteral("CycleB"));
        cycleB.setParentStyleName(QStringLiteral("CycleA"));
        cycleB.setFontStrikeOut(true);
        styles.addCharacterStyle(cycleA);
        styles.addCharacterStyle(cycleB);
        const CharacterStyle cycle = styles.characterStyleOverrides(QStringLiteral("CycleA"));
        QVERIFY(cycle.fontItalic());
        QVERIFY(cycle.fontStrikeOut());
        QVERIFY(!cycle.hasFontFamily());
    }
    void printPdfBecomesPostScript() {
        QString markdown;
        for (int i = 0; i < 80; ++i)
            markdown += QStringLiteral("Paragraph %1 fills a printed page of the same layout used for PDF export.\n\n").arg(i);
        ContentBuilder builder;
        const Content::Document doc = builder.build(markdown);
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout layout;
        const auto laidOut = engine.layout(doc, layout);
        QVERIFY(laidOut.pages.size() > 1);
        PdfGenerator generator(&fonts);
        const QByteArray pdf = generator.generate(laidOut, layout, QStringLiteral("Print"));
        std::unique_ptr<Poppler::Document> poppler(Poppler::Document::loadFromData(pdf));
        QVERIFY(poppler);
        QCOMPARE(poppler->numPages(), laidOut.pages.size());

        QString error;
        const QByteArray all = PrintController::pdfToPostScript(pdf, layout.pageSizePoints(), 0, 0, &error);
        QVERIFY2(!all.isEmpty(), qPrintable(error));
        QVERIFY(all.startsWith("%!PS"));
        QVERIFY(all.contains("%%Pages:"));
        const QByteArray expectedPages = QByteArray("%%Pages: ") + QByteArray::number(poppler->numPages());
        QVERIFY2(all.contains(expectedPages), all.left(400).constData());

        const int lastPage = poppler->numPages();
        const QByteArray onlyLast = PrintController::pdfToPostScript(pdf, layout.pageSizePoints(),
                                                                     lastPage, lastPage, &error);
        QVERIFY2(!onlyLast.isEmpty(), qPrintable(error));
        QVERIFY(onlyLast.startsWith("%!PS"));
        QVERIFY(onlyLast.contains("%%Pages: 1"));
        QVERIFY(onlyLast.size() < all.size());

        const QByteArray onlySecond = PrintController::pdfToPostScript(pdf, layout.pageSizePoints(), 2, 2, &error);
        QVERIFY2(!onlySecond.isEmpty(), qPrintable(error));
        QVERIFY(onlySecond.contains("%%Pages: 1"));

        QString badError;
        const QByteArray bad = PrintController::pdfToPostScript(QByteArray("not a pdf"),
                                                                layout.pageSizePoints(), 0, 0, &badError);
        QVERIFY(bad.isEmpty());
        QVERIFY(!badError.isEmpty());
    }
    // --- Interim alignment with markoff-core (AL-01 …) ---

    void al01FrontmatterIsMetadata() {
        ContentBuilder builder;
        const QString md = QStringLiteral("---\ntitle: X\nauthor: Y\n---\n\n# Body\n\nText.\n");
        const auto doc = builder.build(md);
        QVERIFY(doc.hasFrontmatter);
        QCOMPARE(doc.frontmatterRaw, QStringLiteral("title: X\nauthor: Y"));
        const QString text = Content::toPlainText(doc);
        QVERIFY2(!text.contains(QStringLiteral("title:")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Body")));
        // Blanked lines keep source line numbers addressing the file.
        const auto *heading = std::get_if<Content::Heading>(&doc.blocks.first());
        QVERIFY(heading);
        QCOMPARE(heading->source.startLine, 6);

        // A thematic break in the body is still a rule, and a document with no
        // front matter is untouched.
        const auto rule = builder.build(QStringLiteral("Text.\n\n---\n\nMore.\n"));
        QVERIFY(!rule.hasFrontmatter);
        bool hasRule = false;
        for (const auto &block : rule.blocks)
            hasRule |= std::holds_alternative<Content::HorizontalRule>(block);
        QVERIFY(hasRule);
        QVERIFY(Content::toPlainText(rule).contains(QStringLiteral("More.")));

        // CRLF front matter, and the EOF-close form with no trailing newline.
        const auto crlf = builder.build(QStringLiteral("---\r\ntitle: X\r\n---\r\n\r\nBody.\r\n"));
        QVERIFY(crlf.hasFrontmatter);
        QCOMPARE(crlf.frontmatterRaw, QStringLiteral("title: X"));
        QVERIFY(Content::toPlainText(crlf).contains(QStringLiteral("Body.")));

        const auto eofClose = builder.build(QStringLiteral("---\ntitle: X\n---"));
        QVERIFY(eofClose.hasFrontmatter);
        QCOMPARE(eofClose.frontmatterRaw, QStringLiteral("title: X"));
        QVERIFY(eofClose.blocks.isEmpty());

        // An opening --- without a closing line is body text, not metadata.
        const auto unterminated = builder.build(QStringLiteral("---\ntitle: X\n"));
        QVERIFY(!unterminated.hasFrontmatter);
    }
    void al02UnderscoreIsEmphasis() {
        ContentBuilder builder;
        const auto doc = builder.build(
            QStringLiteral("_one_ and *two* and __three__ and **four** and _five six_\n"));
        const auto *para = std::get_if<Content::Paragraph>(&doc.blocks.first());
        QVERIFY(para);
        auto runStyled = [para](const QString &text, bool italic, bool underline, bool bold) {
            for (const auto &node : para->inlines) {
                const auto *run = std::get_if<Content::TextRun>(&node);
                if (!run || run->text.trimmed() != text)
                    continue;
                return run->style.italic == italic && run->style.underline == underline
                    && (bold ? run->style.fontWeight >= 600 : run->style.fontWeight < 600);
            }
            return false;
        };
        QVERIFY2(runStyled(QStringLiteral("one"), true, false, false),
                 "underscore emphasis should be italic, not underline");
        QVERIFY(runStyled(QStringLiteral("two"), true, false, false));
        QVERIFY(runStyled(QStringLiteral("three"), false, false, true));
        QVERIFY(runStyled(QStringLiteral("four"), false, false, true));
        QVERIFY(runStyled(QStringLiteral("five six"), true, false, false));
        for (const auto &node : para->inlines) {
            const auto *run = std::get_if<Content::TextRun>(&node);
            if (run)
                QVERIFY2(!run->style.underline, qPrintable(run->text));
        }
        QCOMPARE(Content::toPlainText(doc).trimmed(),
                 QStringLiteral("one and two and three and four and five six"));
    }
    void al03UnreferencedNotesAreDropped() {
        ContentBuilder builder;
        const auto doc = builder.build(QStringLiteral(
            "Cited[^a] here.\n\n[^a]: Used note.\n[^b]: Never cited.\n"));
        const Content::FootnoteSection *section = nullptr;
        for (const auto &block : doc.blocks) {
            if (const auto *candidate = std::get_if<Content::FootnoteSection>(&block))
                section = candidate;
        }
        QVERIFY(section);
        QCOMPARE(section->footnotes.size(), 1);
        QVERIFY(!Content::toPlainText(doc).contains(QStringLiteral("Never cited")));
        QVERIFY(Content::toPlainText(doc).contains(QStringLiteral("Used note")));
    }
    void al04CopyUsesNoteMarkers() {
        ContentBuilder builder;
        FootnoteStyle style;
        style.format = FootnoteStyle::RomanLower;
        style.prefix = QStringLiteral("[");
        style.suffix = QStringLiteral("]");
        builder.setFootnoteStyle(style);
        const auto doc = builder.build(QStringLiteral("Cited[^a] here.\n\n[^a]: Used note.\n"));
        const QString text = Content::toPlainText(doc);
        QVERIFY2(text.contains(QStringLiteral("[i]")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("[i]. Used note.")), qPrintable(text));
        QVERIFY(!text.contains(QStringLiteral("0")));
    }
    void al05LinkTargetsAndTitles() {
        ContentBuilder builder;
        const auto doc = builder.build(QStringLiteral(
            "[docs](https://example.com \"Docs site\") and [[Page|label]] and [[Other]] "
            "and ![[Note]] and a #project/sub tag and [local](notes.md)\n"));
        const auto *para = std::get_if<Content::Paragraph>(&doc.blocks.first());
        QVERIFY(para);
        auto find = [para](const QString &text) -> const Content::TextRun * {
            for (const auto &node : para->inlines) {
                const auto *run = std::get_if<Content::TextRun>(&node);
                if (run && run->text == text)
                    return run;
            }
            return nullptr;
        };
        const auto *docs = find(QStringLiteral("docs"));
        QVERIFY(docs);
        QCOMPARE(docs->style.linkHref, QStringLiteral("https://example.com"));
        QCOMPARE(docs->style.linkTitle, QStringLiteral("Docs site"));
        QVERIFY(docs->style.linkKind == Content::LinkKind::External);

        const auto *wiki = find(QStringLiteral("label"));
        QVERIFY(wiki);
        QCOMPARE(wiki->style.linkHref, QStringLiteral("Page"));
        QVERIFY(wiki->style.linkKind == Content::LinkKind::WikiLink);

        const auto *other = find(QStringLiteral("Other"));
        QVERIFY(other);
        QCOMPARE(other->style.linkHref, QStringLiteral("Other"));
        QVERIFY(other->style.linkKind == Content::LinkKind::WikiLink);

        const auto *note = find(QStringLiteral("Note"));
        QVERIFY2(note, "an embed shows its target text");
        QCOMPARE(note->style.linkHref, QStringLiteral("Note"));
        QVERIFY(note->style.linkKind == Content::LinkKind::Embed);
        QVERIFY(!Content::toPlainText(doc).contains(QStringLiteral("![[Note]]")));

        const auto *tag = find(QStringLiteral("#project/sub"));
        QVERIFY(tag);
        QVERIFY(tag->style.linkKind == Content::LinkKind::Tag);

        const auto *local = find(QStringLiteral("local"));
        QVERIFY(local);
        QVERIFY(local->style.linkKind == Content::LinkKind::File);

        // The title reaches the hit index the painted pages read.
        FontManager fonts;
        TextShaper shaper(&fonts);
        Layout::Engine engine(&fonts, &shaper);
        PageLayout pl;
        const auto layout = engine.layout(
            builder.build(QStringLiteral("A [titled](https://example.com \"Tip\") link.\n")), pl);
        QVERIFY(!layout.pages.isEmpty());
        const HitIndex index = buildHitIndex(layout.pages.first());
        QVERIFY(index.linkHrefs.contains(QStringLiteral("https://example.com")));
        QVERIFY(index.linkTitles.contains(QStringLiteral("Tip")));
    }
};

int main(int argc, char **argv) {
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", config.filePath("data").toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("penelope-test");
    KLocalizedString::setApplicationDomain("penelope");
    RegressionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "regressiontest.moc"
