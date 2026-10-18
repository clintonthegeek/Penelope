#ifndef PENELOPE_DOCUMENTTAB_H
#define PENELOPE_DOCUMENTTAB_H

#include <QWidget>
#include <QFileSystemWatcher>
#include <QTimer>

#include "contentmodel.h"
#include "documentview.h"
#include <optional>
#include "layoutengine.h"

class QPlainTextEdit;
class QStackedWidget;
class DocumentView;
class MarkdownHighlighter;
class KMessageWidget;
class QAction;

class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(QWidget *parent = nullptr);

    DocumentView *documentView() const { return m_documentView; }
    QPlainTextEdit *sourceEditor() const { return m_sourceEditor; }
    MarkdownHighlighter *markdownHighlighter() const { return m_highlighter; }

    void setFilePath(const QString &path);
    bool isModified() const;
    bool saveSource(QString *error = nullptr);
    bool hasExternalChanges() const;
    QString filePath() const { return m_filePath; }

    bool isSourceMode() const { return m_sourceMode; }
    void setSourceMode(bool source);

    // Get current source text (from editor)
    QString sourceText() const;
    // Set source text (into editor)
    void setSourceText(const QString &text);

    void setInitialViewState(const ViewState &state) { m_initialViewState = state; }
    std::optional<ViewState> takeInitialViewState() {
        auto state = m_initialViewState; m_initialViewState.reset(); return state;
    }

    // Cached TOC data for instant rebuild on tab switch
    void setTocData(const Content::Document &doc, const QList<Layout::SourceMapEntry> &sourceMap);
    const Content::Document &cachedContentDoc() const { return m_contentDoc; }
    const QList<Layout::SourceMapEntry> &cachedSourceMap() const { return m_sourceMap; }
    void clearTocData() { m_hasTocData = false; m_contentDoc = {}; m_sourceMap.clear(); }
    bool hasTocData() const { return m_hasTocData; }

    // Composition generation tracking for stale-tab detection
    void setCompositionGeneration(quint64 gen) { m_compositionGeneration = gen; }
    quint64 sourceRevision() const { return m_sourceRevision; }
    quint64 compositionGeneration() const { return m_compositionGeneration; }

Q_SIGNALS:
    void sourceChanged();
    // The buffer was replaced with the current disk contents.
    void reloadedFromDisk();

private:
    enum class DiskNotice {
        None,
        Changed,
        Deleted,
    };

    void replaceSourceText(const QString &text);
    void inspectDiskFile();
    void reloadFromDisk();
    void applyDiskContents(const QByteArray &data);
    void showDiskNotice(DiskNotice notice);
    void hideDiskNotice();
    void ensureWatched();

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QByteArray m_diskContents;
    QString m_knownText;
    bool m_externalChanges = false;
    bool m_diskMissing = false;
    int m_readRetries = 0;
    DiskNotice m_notice = DiskNotice::None;
    KMessageWidget *m_changeBar = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_dismissAction = nullptr;
    QStackedWidget *m_stack = nullptr;
    DocumentView *m_documentView = nullptr;
    QPlainTextEdit *m_sourceEditor = nullptr;
    MarkdownHighlighter *m_highlighter = nullptr;
    QString m_filePath;
    bool m_sourceMode = false;

    std::optional<ViewState> m_initialViewState;
    // Cached TOC data
    Content::Document m_contentDoc;
    QList<Layout::SourceMapEntry> m_sourceMap;
    bool m_hasTocData = false;

    // Composition generation (0 = never built)
    quint64 m_sourceRevision = 0;
    quint64 m_compositionGeneration = 0;
};

#endif // PENELOPE_DOCUMENTTAB_H
