#ifndef PENELOPE_DOCUMENTTAB_H
#define PENELOPE_DOCUMENTTAB_H

#include <QWidget>

#include "contentmodel.h"
#include "layoutengine.h"

class QPlainTextEdit;
class QStackedWidget;
class DocumentView;
class MarkdownHighlighter;

class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(QWidget *parent = nullptr);

    DocumentView *documentView() const { return m_documentView; }
    QPlainTextEdit *sourceEditor() const { return m_sourceEditor; }
    MarkdownHighlighter *markdownHighlighter() const { return m_highlighter; }

    void setFilePath(const QString &path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }

    bool isSourceMode() const { return m_sourceMode; }
    void setSourceMode(bool source);

    // Get current source text (from editor)
    QString sourceText() const;
    // Set source text (into editor)
    void setSourceText(const QString &text);

    // Cached TOC data for instant rebuild on tab switch
    void setTocData(const Content::Document &doc, const QList<Layout::SourceMapEntry> &sourceMap);
    const Content::Document &cachedContentDoc() const { return m_contentDoc; }
    const QList<Layout::SourceMapEntry> &cachedSourceMap() const { return m_sourceMap; }
    bool hasTocData() const { return m_hasTocData; }

    // Composition generation tracking for stale-tab detection
    void setCompositionGeneration(quint64 gen) { m_compositionGeneration = gen; }
    quint64 compositionGeneration() const { return m_compositionGeneration; }

private:
    QStackedWidget *m_stack = nullptr;
    DocumentView *m_documentView = nullptr;
    QPlainTextEdit *m_sourceEditor = nullptr;
    MarkdownHighlighter *m_highlighter = nullptr;
    QString m_filePath;
    bool m_sourceMode = false;

    // Cached TOC data
    Content::Document m_contentDoc;
    QList<Layout::SourceMapEntry> m_sourceMap;
    bool m_hasTocData = false;

    // Composition generation (0 = never built)
    quint64 m_compositionGeneration = 0;
};

#endif // PENELOPE_DOCUMENTTAB_H
