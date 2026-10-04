#include "documenttab.h"
#include <QSignalBlocker>
#include "documentview.h"
#include "markdownhighlighter.h"
#include "penelopesettings.h"

#include <QFont>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

DocumentTab::DocumentTab(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    // Reader view (index 0)
    m_documentView = new DocumentView(this);
    m_stack->addWidget(m_documentView);

    // Source editor (index 1)
    m_sourceEditor = new QPlainTextEdit(this);
    m_sourceEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("JetBrains Mono"), 11);
    mono.setStyleHint(QFont::Monospace);
    m_sourceEditor->setFont(mono);

    m_highlighter = new MarkdownHighlighter(m_sourceEditor->document());

    m_stack->addWidget(m_sourceEditor);
    m_stack->setCurrentIndex(0); // Start in reader mode
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() { m_reloadTimer.start(); });
    // Editors often replace the file atomically, removing the file watch.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { m_reloadTimer.start(); });
    connect(&m_reloadTimer, &QTimer::timeout, this, &DocumentTab::reloadFromDisk);
    // Only genuine text edits schedule a rebuild. QPlainTextEdit also emits
    // textChanged for presentation-only changes such as setPalette(), which
    // rebuildCurrentDocument() applies on every pass; treating those as edits
    // makes each rebuild schedule the next one.
    connect(m_sourceEditor, &QPlainTextEdit::textChanged, this, [this]() {
        const QString current = m_sourceEditor->toPlainText();
        if (current == m_knownText)
            return;
        m_knownText = current;
        ++m_sourceRevision;
        m_compositionGeneration = 0;
        Q_EMIT sourceChanged();
    });
    m_knownText = m_sourceEditor->toPlainText();
}

void DocumentTab::setSourceMode(bool source)
{
    if (m_sourceMode == source)
        return;
    m_sourceMode = source;
    m_stack->setCurrentIndex(source ? 1 : 0);
}

QString DocumentTab::sourceText() const
{
    return m_sourceEditor->toPlainText();
}

void DocumentTab::setSourceText(const QString &text)
{
    replaceSourceText(text);
    m_sourceEditor->document()->setModified(false);
}

// Replaces the buffer without reporting a user edit; callers that change the
// source on the user's behalf emit sourceChanged() themselves.
void DocumentTab::replaceSourceText(const QString &text)
{
    const QSignalBlocker blocker(m_sourceEditor);
    m_sourceEditor->setPlainText(text);
    m_knownText = m_sourceEditor->toPlainText();
    ++m_sourceRevision;
}

void DocumentTab::setTocData(const Content::Document &doc, const QList<Layout::SourceMapEntry> &sourceMap)
{
    m_contentDoc = doc;
    m_sourceMap = sourceMap;
    m_hasTocData = true;
}

void DocumentTab::setFilePath(const QString &path)
{
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    m_filePath = path;
    m_watcher.addPath(QFileInfo(path).absolutePath());
    m_watcher.addPath(path);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) m_diskContents = file.readAll();
}

bool DocumentTab::isModified() const
{
    return m_sourceEditor->document()->isModified();
}

bool DocumentTab::saveSource(QString *error)
{
    QSaveFile file(m_filePath);
    const QByteArray data = sourceText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    m_diskContents = data;
    m_externalChanges = false;
    m_sourceEditor->document()->setModified(false);
    if (!m_watcher.files().contains(m_filePath)) m_watcher.addPath(m_filePath);
    return true;
}

void DocumentTab::reloadFromDisk()
{
    if (!PenelopeSettings::self()->autoReloadOnChange()) return;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray data = file.readAll();
    if (!m_watcher.files().contains(m_filePath)) m_watcher.addPath(m_filePath);
    if (data == m_diskContents) return;
    if (isModified()) {
        m_externalChanges = true;
        Q_EMIT externalChangeDetected();
        return;
    }
    m_diskContents = data;
    m_externalChanges = false;
    const int scroll = m_sourceEditor->verticalScrollBar()->value();
    replaceSourceText(QString::fromUtf8(data));
    m_sourceEditor->verticalScrollBar()->setValue(scroll);
    m_compositionGeneration = 0;
    Q_EMIT sourceChanged();
}

bool DocumentTab::hasExternalChanges() const
{
    QFile file(m_filePath);
    return m_externalChanges || !file.open(QIODevice::ReadOnly) || file.readAll() != m_diskContents;
}
