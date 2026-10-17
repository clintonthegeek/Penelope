/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "compositioncontroller.h"
#include "fontmanager.h"
#include "textshaper.h"
#include "fragmentcache.h"
#include "pdfgenerator.h"
#include "hyphenator.h"
#include "shortwords.h"
#include "contentbuilder.h"
#include "blockidentity.h"
#include <QElapsedTimer>
#include <exception>

namespace {
bool cancelled(const CompositionCancellation &token) {
    return token && token->load(std::memory_order_relaxed);
}
void applyOverrides(QList<Content::BlockNode> &blocks, const QHash<QString, QString> &overrides) {
    if (overrides.isEmpty()) return;
    for (auto &block : blocks) {
        if (auto *code = std::get_if<Content::CodeBlock>(&block)) {
            auto it = overrides.constFind(code->code.trimmed());
            if (it != overrides.cend()) code->language = it.value();
        } else if (auto *quote = std::get_if<Content::BlockQuote>(&block)) {
            applyOverrides(quote->children, overrides);
        } else if (auto *list = std::get_if<Content::List>(&block)) {
            for (auto &item : list->items) applyOverrides(item.children, overrides);
        }
    }
}
}

Content::Document buildCompositionContent(const CompositionRequest &r, QString *processed,
                                          const CompositionCancellation &cancel) {
    StyleManager styles;
    for (const auto &s : r.paragraphs) styles.addParagraphStyle(s);
    for (const auto &s : r.characters) styles.addCharacterStyle(s);
    for (const auto &s : r.tables) styles.addTableStyle(s);
    styles.setFootnoteStyle(r.footnotes);
    Hyphenator hyphenator;
    ShortWords shortWords;
    ContentBuilder builder;
    builder.setBasePath(r.basePath);
    builder.setStyleManager(&styles);
    builder.setFootnoteStyle(r.footnotes);
    builder.setRenderImages(r.renderImages);
    builder.setCancellationCheck([cancel] { return cancelled(cancel); });
    if (r.hyphenation || r.hyphenateJustified) {
        hyphenator.loadDictionary(r.language);
        hyphenator.setMinWordLength(r.minWordLength);
        builder.setHyphenator(&hyphenator);
    }
    if (r.shortWords) {
        shortWords.setLanguage(r.language);
        builder.setShortWords(&shortWords);
    }
    auto doc = builder.build(r.markdown);
    if (processed) *processed = builder.processedMarkdown();
    applyOverrides(doc.blocks, r.languageOverrides);
    return doc;
}

class CompositionWorker : public QObject {
    Q_OBJECT
public:
    void render(quint64 id, const CompositionRequest &r, const CompositionCancellation &token) {
        try {
            if (cancelled(token)) { Q_EMIT completed(id, {}, {}); return; }
            if (!m_fonts) {
                m_fonts = std::make_shared<FontManager>();
                m_shaper = std::make_unique<TextShaper>(m_fonts.get());
                m_shaper->setFallbackFont(m_fonts->loadFontFromPath(
                    QStringLiteral(":/fonts/PrettySymbolsFallback.ttf")));
            }
            auto result = std::make_shared<CompositionResult>();
            result->request = r;
            result->fonts = m_fonts;
            QElapsedTimer timer;
            timer.start();
            // Single-entry semantic cache. Images are deliberately rebuilt:
            // a file may change without a Markdown/source revision changing.
            const bool reusable = !r.markdown.contains(QStringLiteral("!["))
                && r.markdown.toUtf8().size() <= 8 * 1024 * 1024;
            if (reusable && m_cached && sameContent(r, *m_cached)) {
                result->document = m_cachedDoc;
                result->processedMarkdown = m_processed;
                result->reusedContent = true;
            } else {
                bool hadPrevious = m_cached.has_value();
                Content::Document previousDoc = m_cachedDoc;
                result->document = buildCompositionContent(r, &result->processedMarkdown, token);
                if (!cancelled(token)) {
                    if (hadPrevious && !previousDoc.blocks.isEmpty())
                        restabilizeBlockIds(previousDoc.blocks, result->document.blocks);
                    m_cached.reset(); m_cachedDoc = {}; m_processed.clear();
                    if (reusable) {
                        m_cached = r; m_cachedDoc = result->document;
                        m_processed = result->processedMarkdown;
                    }
                }
            }
            result->parseNs = timer.nsecsElapsed();
            if (cancelled(token)) { m_shaper->setCancellationCheck({}); Q_EMIT completed(id, {}, {}); return; }
            m_fonts->resetUsage();
            const auto hits = m_shaper->cacheHits(), misses = m_shaper->cacheMisses();
            Layout::Engine engine(m_fonts.get(), m_shaper.get());
            m_shaper->setCancellationCheck([token] { return cancelled(token); });
            engine.setHyphenateJustifiedText(r.hyphenateJustified);
            engine.setOptimalTableLayout(r.optimalTables);
            engine.setCodeHighlighting(r.codeTheme, r.highlightCode);
            engine.setFragmentCache(&m_fragmentCache);
            engine.setCancellationCheck([token] { return cancelled(token); });
            timer.restart();
            if (r.startBlock > 0 && r.continuous) {
                // resume cursor
            }
            if (r.continuous) {
                if (r.maxBlocks > 0 || r.maxHeight > 0) {
                    Layout::LayoutCursor cursor;
                    cursor.nextBlock = r.startBlock;
                    cursor.y = 0;
                    Layout::LayoutBudget budget;
                    budget.maxBlocks = r.maxBlocks;
                    budget.maxHeight = r.maxHeight;
                    budget.onePage = r.onePage;
                    result->continuousLayout = engine.layoutContinuousFrom(result->document, r.width, &cursor, budget);
                    result->cursor = cursor;
                    result->finished = cursor.finished;
                } else {
                    result->continuousLayout = engine.layoutContinuous(result->document, r.width);
                    result->finished = true;
                }
            } else if (r.onePage) {
                Layout::LayoutCursor cursor;
                cursor.nextBlock = r.startBlock;
                Layout::Page page;
                bool ok = engine.layoutNextPage(result->document, r.pageLayout, &cursor, &page);
                if (ok) {
                    result->paginated.pages.append(page);
                }
                result->cursor = cursor;
                result->finished = cursor.finished;
            } else {
                result->paginated = engine.layout(result->document, r.pageLayout);
                result->finished = true;
            }
            result->layoutNs = timer.nsecsElapsed();
            if (cancelled(token)) { m_shaper->setCancellationCheck({}); Q_EMIT completed(id, {}, {}); return; }
            m_shaper->setCancellationCheck({});
            result->shapeHits = m_shaper->cacheHits() - hits;
            result->shapeMisses = m_shaper->cacheMisses() - misses;
            if (!r.continuous) {
                PdfGenerator generator(m_fonts.get());
                generator.setDocumentInfo(r.filename, r.title);
                generator.setMaxJustifyGap(r.maxJustifyGap);
                generator.setCompressionLevel(1);
                generator.setCancellationCheck([token] { return cancelled(token); });
                timer.restart();
                result->pdf = generator.generate(result->paginated, r.pageLayout, r.title);
                result->pdfNs = timer.nsecsElapsed();
                if (result->pdf.isEmpty() && !cancelled(token)) {
                    Q_EMIT completed(id, {}, QStringLiteral("PDF generation failed")); return;
                }
            }
            m_shaper->setCancellationCheck({});
            Q_EMIT completed(id, cancelled(token) ? CompositionResultPtr{} : result, {});
        } catch (const std::exception &e) {
            if (m_shaper) m_shaper->setCancellationCheck({});
            Q_EMIT completed(id, {}, QString::fromUtf8(e.what()));
        }
    }
Q_SIGNALS:
    void completed(quint64 id, CompositionResultPtr result, const QString &error);
private:
    static bool sameContent(const CompositionRequest &a, const CompositionRequest &b) {
        return a.markdown == b.markdown && a.basePath == b.basePath
            && a.semanticRevision == b.semanticRevision && a.renderImages == b.renderImages
            && a.hyphenation == b.hyphenation && a.hyphenateJustified == b.hyphenateJustified
            && a.shortWords == b.shortWords && a.language == b.language
            && a.minWordLength == b.minWordLength && a.languageOverrides == b.languageOverrides;
    }
    std::shared_ptr<FontManager> m_fonts;
    std::unique_ptr<TextShaper> m_shaper;
    FragmentCache m_fragmentCache;
    std::optional<CompositionRequest> m_cached;
    Content::Document m_cachedDoc;
    QString m_processed;
};

CompositionController::CompositionController(QObject *parent) : QObject(parent) {
    qRegisterMetaType<CompositionResultPtr>();
    m_worker = new CompositionWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CompositionWorker::completed, this, &CompositionController::finish,
            Qt::QueuedConnection);
    m_thread.start();
}
CompositionController::~CompositionController() {
    cancel();
    m_thread.quit();
    m_thread.wait();
}
quint64 CompositionController::submit(CompositionRequest request) {
    m_latestId = ++m_nextId;
    if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
    m_pending = std::move(request);
    startNext();
    return m_latestId;
}
void CompositionController::cancel() {
    m_latestId = ++m_nextId;
    m_pending.reset();
    if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
}
void CompositionController::startNext() {
    if (m_busy || !m_pending) return;
    auto request = std::move(*m_pending);
    m_pending.reset();
    m_busy = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto id = m_latestId;
    const auto token = m_cancel;
    Q_EMIT started(id);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, id, request = std::move(request), token] {
        worker->render(id, request, token);
    }, Qt::QueuedConnection);
}
void CompositionController::finish(quint64 id, CompositionResultPtr result, const QString &error) {
    m_busy = false;
    m_cancel.reset();
    if (id == m_latestId) {
        if (!error.isEmpty()) Q_EMIT failed(id, error);
        else if (result) Q_EMIT ready(id, std::move(result));
    }
    startNext();
}
#include "compositioncontroller.moc"
