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
#include <KSharedConfig>
#include "pdfpageitem.h"
#include <KLocalizedString>
#include <poppler-qt6.h>
#include "contentbuilder.h"
#include "contentplaintext.h"
#include "contentfilter.h"
#include "contentrtfexporter.h"
#include "documenttab.h"
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
        QTRY_VERIFY_WITH_TIMEOUT(tab->documentView()->isPdfMode() && tab->hasTocData(), 10000);
        window.actionCollection()->action("view_source_mode")->trigger();
        tab->sourceEditor()->insertPlainText("new ");
        window.actionCollection()->action("view_web_mode")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!tab->documentView()->isPdfMode() && tab->compositionGeneration() > 0, 10000);
        QVERIFY(tab->documentView()->pdfData().isEmpty());
        auto rtf = ContentRtfExporter().exportBlocks(tab->cachedContentDoc().blocks);
        QVERIFY(rtf.contains("new"));
        window.actionCollection()->action("view_print_mode")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(tab->documentView()->isPdfMode(), 10000);
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
        QTRY_COMPARE_WITH_TIMEOUT(tab->documentView()->saveViewState().currentPage, 2, 10000);
        auto *view = tab->documentView();
        for (auto mode : {DocumentView::SinglePage, DocumentView::FacingPages,
                          DocumentView::FacingPagesFirstAlone}) {
            view->setViewMode(mode);
            view->goToPage(0);
            view->goToPage(2);
            bool hasTargetPage = false;
            for (auto *item : view->scene()->items()) {
                if (auto *page = dynamic_cast<PdfPageItem *>(item))
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
