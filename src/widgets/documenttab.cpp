#include "documenttab.h"
#include <QSignalBlocker>
#include "documentview.h"
#include "markdownhighlighter.h"
#include "penelopesettings.h"

#include <KLocalizedString>
#include <KMessageWidget>

#include <QAction>
#include <QFont>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSaveFile>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>

DocumentTab::DocumentTab(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Disk-change notice. Hidden until the watcher sees a revision that was
    // not applied automatically; the reader stays usable behind it.
    m_changeBar = new KMessageWidget(this);
    m_changeBar->setMessageType(KMessageWidget::Warning);
    m_changeBar->setWordWrap(true);
    m_changeBar->setCloseButtonVisible(false);
    m_changeBar->setVisible(false);
    layout->addWidget(m_changeBar);

    m_reloadAction = new QAction(this);
    m_reloadAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    connect(m_reloadAction, &QAction::triggered, this, &DocumentTab::reloadFromDisk);

    m_dismissAction = new QAction(this);
    connect(m_dismissAction, &QAction::triggered, this, &DocumentTab::hideDiskNotice);

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
    connect(&m_reloadTimer, &QTimer::timeout, this, &DocumentTab::inspectDiskFile);
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
        // An already-visible "changed on disk" notice must now warn that
        // reloading would discard the edit the user just made.
        if (m_notice == DiskNotice::Changed)
            showDiskNotice(DiskNotice::Changed);
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
    m_externalChanges = false;
    m_diskMissing = false;
    m_readRetries = 0;
    hideDiskNotice();
    ensureWatched();
    QFile file(path);
    m_diskContents = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void DocumentTab::ensureWatched()
{
    if (m_filePath.isEmpty())
        return;
    // Editors that replace the file atomically drop both watches; the
    // directory is what tells us when the replacement has settled.
    const QString dir = QFileInfo(m_filePath).absolutePath();
    if (!m_watcher.directories().contains(dir) && QFileInfo(dir).isDir())
        m_watcher.addPath(dir);
    if (QFileInfo::exists(m_filePath) && !m_watcher.files().contains(m_filePath))
        m_watcher.addPath(m_filePath);
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
    m_diskMissing = false;
    hideDiskNotice();
    m_sourceEditor->document()->setModified(false);
    ensureWatched();
    return true;
}

// Re-reads the file and decides between applying it, asking the user, and
// remembering that the file is gone. Detection always runs; the auto-reload
// preference only decides whether an unmodified buffer is replaced silently.
void DocumentTab::inspectDiskFile()
{
    if (m_filePath.isEmpty())
        return;
    ensureWatched();

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (QFileInfo::exists(m_filePath)) {
            // Windows readers can hit a sharing violation while the writer
            // still holds the handle. Retry briefly before believing the read.
            if (m_readRetries++ < 10) {
                m_reloadTimer.start();
                return;
            }
            m_readRetries = 0;
            return;
        }
        if (!m_diskMissing) {
            m_diskMissing = true;
            m_externalChanges = true;
            showDiskNotice(DiskNotice::Deleted);
        }
        return;
    }
    m_readRetries = 0;
    m_diskMissing = false;

    const QByteArray data = file.readAll();
    const bool diskMatchesBuffer = data == sourceText().toUtf8();
    if (data == m_diskContents) {
        // The file reverted to the content the buffer already holds.
        if (diskMatchesBuffer) {
            m_externalChanges = false;
            if (m_notice != DiskNotice::None)
                hideDiskNotice();
        }
        return;
    }

    m_diskContents = data;
    if (diskMatchesBuffer) {
        // An external writer produced exactly our text (or a save raced the
        // watcher); there is nothing to reload.
        m_externalChanges = false;
        hideDiskNotice();
        return;
    }

    m_externalChanges = true;
    if (!isModified() && PenelopeSettings::self()->autoReloadOnChange()) {
        applyDiskContents(data);
        return;
    }
    showDiskNotice(DiskNotice::Changed);
}

// User-confirmed reload: the disk copy wins, including over unsaved edits.
void DocumentTab::reloadFromDisk()
{
    if (m_filePath.isEmpty())
        return;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (!QFileInfo::exists(m_filePath) && !m_diskMissing) {
            m_diskMissing = true;
            m_externalChanges = true;
            showDiskNotice(DiskNotice::Deleted);
        }
        return;
    }
    m_readRetries = 0;
    m_diskMissing = false;
    applyDiskContents(file.readAll());
}

void DocumentTab::applyDiskContents(const QByteArray &data)
{
    // Text is replaced wholesale, so line numbers are the only position that
    // survives. Keep the cursor/selection block and column, plus the top line.
    QTextDocument *document = m_sourceEditor->document();
    const QTextCursor oldCursor = m_sourceEditor->textCursor();
    const QTextBlock anchorBlock = document->findBlock(oldCursor.anchor());
    const QTextBlock cursorBlock = document->findBlock(oldCursor.position());
    const int anchorColumn = oldCursor.anchor() - anchorBlock.position();
    const int cursorColumn = oldCursor.position() - cursorBlock.position();
    const int topBlock = m_sourceEditor->cursorForPosition(QPoint(0, 0)).blockNumber();

    replaceSourceText(QString::fromUtf8(data));
    document = m_sourceEditor->document();

    auto blockPosition = [document](int blockNumber, int column) {
        const QTextBlock block = document->findBlockByNumber(
            qBound(0, blockNumber, qMax(0, document->blockCount() - 1)));
        return block.position() + qBound(0, column, qMax(0, block.length() - 1));
    };
    QTextCursor restored(m_sourceEditor->textCursor());
    restored.setPosition(blockPosition(anchorBlock.blockNumber(), anchorColumn));
    restored.setPosition(blockPosition(cursorBlock.blockNumber(), cursorColumn),
                         QTextCursor::KeepAnchor);
    m_sourceEditor->setTextCursor(restored);
    m_sourceEditor->verticalScrollBar()->setValue(
        qBound(0, topBlock, qMax(0, document->blockCount() - 1)));

    m_externalChanges = false;
    m_diskMissing = false;
    m_readRetries = 0;
    hideDiskNotice();
    document->setModified(false);
    m_compositionGeneration = 0;
    Q_EMIT sourceChanged();
    Q_EMIT reloadedFromDisk();
}

void DocumentTab::showDiskNotice(DiskNotice notice)
{
    if (!m_changeBar)
        return;
    m_notice = notice;
    const QString name = QFileInfo(m_filePath).fileName();
    m_changeBar->clearActions();
    m_dismissAction->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
    if (notice == DiskNotice::Deleted) {
        m_changeBar->setText(
            i18n("%1 was deleted or moved on disk. Your open copy is kept.", name));
        m_dismissAction->setText(i18n("Keep Editing"));
        m_dismissAction->setToolTip(i18n("Keeps the open copy and ignores the missing file."));
        m_changeBar->addAction(m_dismissAction);
    } else if (isModified()) {
        m_changeBar->setText(
            i18n("%1 changed on disk. Your unsaved edits have been kept.", name));
        m_reloadAction->setText(i18n("Discard Edits and Reload"));
        m_reloadAction->setToolTip(
            i18n("Reloads the file from disk. Unsaved changes will be lost."));
        m_dismissAction->setText(i18n("Keep My Edits"));
        m_dismissAction->setToolTip(i18n("Keeps your edits without touching the file on disk."));
        m_changeBar->addAction(m_reloadAction);
        m_changeBar->addAction(m_dismissAction);
    } else {
        m_changeBar->setText(i18n("%1 changed on disk.", name));
        m_reloadAction->setText(i18n("Reload"));
        m_reloadAction->setToolTip(i18n("Reloads the file from disk."));
        m_dismissAction->setText(i18n("Ignore"));
        m_dismissAction->setToolTip(i18n("Leaves the open copy unchanged."));
        m_changeBar->addAction(m_reloadAction);
        m_changeBar->addAction(m_dismissAction);
    }
    m_changeBar->setVisible(true);
}

void DocumentTab::hideDiskNotice()
{
    m_notice = DiskNotice::None;
    if (m_changeBar)
        m_changeBar->setVisible(false);
}

bool DocumentTab::hasExternalChanges() const
{
    if (m_externalChanges)
        return true;
    QFile file(m_filePath);
    return !file.open(QIODevice::ReadOnly) || file.readAll() != m_diskContents;
}
