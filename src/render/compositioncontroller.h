/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PENELOPE_COMPOSITIONCONTROLLER_H
#define PENELOPE_COMPOSITIONCONTROLLER_H

#include "contentmodel.h"
#include "layoutengine.h"
#include "pagelayout.h"
#include "stylemanager.h"
#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>
#include <optional>

class FontManager;
class CompositionWorker;
using CompositionCancellation = std::shared_ptr<std::atomic_bool>;

// Value-only input, captured on the GUI thread. semanticRevision must change
// whenever the style maps change. No widget, settings singleton or GUI-owned
// QObject may be referenced from a request.
struct CompositionRequest {
    QString markdown, basePath, filename, title;
    QHash<QString, ParagraphStyle> paragraphs;
    QHash<QString, CharacterStyle> characters;
    QHash<QString, TableStyle> tables;
    FootnoteStyle footnotes;
    PageLayout pageLayout;
    QHash<QString, QString> languageOverrides;
    quint64 semanticRevision = 0;
    quint64 sourceRevision = 0;
    bool continuous = false;
    int maxBlocks = 0;
    qreal maxHeight = 0;
    bool onePage = false;
    bool interactivePages = false;
    bool geometryChanged = false;
    int startBlock = 0;
    qreal width = 500;
    bool renderImages = true;
    bool hyphenation = false;
    bool hyphenateJustified = true;
    bool shortWords = false;
    QString language = QStringLiteral("en_US");
    int minWordLength = 5;
    bool optimalTables = false;
    bool highlightCode = true;
    QString codeTheme;
    qreal maxJustifyGap = 14;
};

struct CompositionResult {
    // Faces are owned by this context. Only the composition thread mutates
    // its font state; GUI painters access immutable font bytes/Hershey data.
    std::shared_ptr<FontManager> fonts;
    Content::Document document;
    QString processedMarkdown;
    Layout::LayoutResult paginated;
    Layout::ContinuousLayoutResult continuousLayout;
    QByteArray pdf;
    CompositionRequest request;
    bool reusedContent = false;
    quint64 shapeHits = 0, shapeMisses = 0;
    bool finished = true;
    Layout::LayoutCursor cursor;
    qint64 parseNs = 0, layoutNs = 0, pdfNs = 0;
};
using CompositionResultPtr = std::shared_ptr<CompositionResult>;
Q_DECLARE_METATYPE(CompositionResultPtr)

// Also usable by explicit export/print paths: it always reads the supplied
// source snapshot, never the potentially stale reader's semantic document.
Content::Document buildCompositionContent(const CompositionRequest &request,
    QString *processed = nullptr, const CompositionCancellation &cancel = {});

class CompositionController : public QObject {
    Q_OBJECT
public:
    explicit CompositionController(QObject *parent = nullptr);
    ~CompositionController() override;
    quint64 submit(CompositionRequest request);
    void cancel();
    bool isBusy() const { return m_busy; }
    int pendingCount() const { return m_pending ? 1 : 0; }

Q_SIGNALS:
    void started(quint64 id);
    void ready(quint64 id, CompositionResultPtr result);
    void failed(quint64 id, const QString &error);

private:
    void startNext();
    void finish(quint64 id, CompositionResultPtr result, const QString &error);
    QThread m_thread;
    CompositionWorker *m_worker = nullptr;
    bool m_busy = false;
    quint64 m_nextId = 0, m_latestId = 0;
    std::optional<CompositionRequest> m_pending;
    CompositionCancellation m_cancel;
};
#endif
