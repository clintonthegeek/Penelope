#include "readeraccessible.h"

#include "contentplaintext.h"
#include "documentview.h"

#include <QCoreApplication>
#include <QWidget>

namespace {

// A block's role, so a screen reader can tell a heading from a paragraph.
QAccessible::Role roleForBlock(const Content::BlockNode &block)
{
    return std::visit([](const auto &b) {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, Content::Heading>)
            return QAccessible::Heading;
        else if constexpr (std::is_same_v<T, Content::List>)
            return QAccessible::List;
        else if constexpr (std::is_same_v<T, Content::Table>)
            return QAccessible::Table;
        else if constexpr (std::is_same_v<T, Content::CodeBlock>)
            return QAccessible::Paragraph;
        else if constexpr (std::is_same_v<T, Content::HorizontalRule>)
            return QAccessible::Separator;
        else if constexpr (std::is_same_v<T, Content::FootnoteSection>)
            return QAccessible::Note;
        return QAccessible::Paragraph;
    }, block);
}

QString textForBlock(const Content::BlockNode &block)
{
    Content::Document document;
    document.blocks.append(block);
    return Content::toPlainText(document).simplified();
}

} // namespace

// --- ReaderAccessible ---

void ReaderAccessible::installFactory()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    QAccessible::installFactory([](const QString &className, QObject *object) -> QAccessibleInterface * {
        if (className != QLatin1String("DocumentView") || !object)
            return nullptr;
        auto *view = qobject_cast<DocumentView *>(object);
        if (!view)
            return nullptr;
        return new ReaderAccessible(view);
    });
}

ReaderAccessible::ReaderAccessible(DocumentView *view)
    : m_view(view)
{
}

ReaderAccessible::~ReaderAccessible()
{
    qDeleteAll(m_children);
}

bool ReaderAccessible::isValid() const
{
    return m_view != nullptr;
}

QObject *ReaderAccessible::object() const
{
    return m_view;
}

void ReaderAccessible::ensureBuilt() const
{
    if (m_built)
        return;
    m_built = true;
    QStringList parts;
    for (const auto &block : m_view->contentDocument().blocks) {
        ReaderBlock entry;
        entry.role = roleForBlock(block);
        entry.text = textForBlock(block);
        m_blocks.append(entry);
        if (!entry.text.isEmpty())
            parts.append(entry.text);
    }
    m_text = parts.join(QStringLiteral("\n\n"));
}

const QString &ReaderAccessible::documentText() const
{
    ensureBuilt();
    return m_text;
}

const QList<ReaderBlock> &ReaderAccessible::blocks() const
{
    ensureBuilt();
    return m_blocks;
}

QAccessibleInterface *ReaderAccessible::childAt(int, int) const
{
    return nullptr;
}

QAccessibleInterface *ReaderAccessible::parent() const
{
    return m_view ? QAccessible::queryAccessibleInterface(m_view->parentWidget()) : nullptr;
}

QAccessibleInterface *ReaderAccessible::child(int index) const
{
    ensureBuilt();
    if (index < 0 || index >= m_blocks.size())
        return nullptr;
    while (m_children.size() <= index)
        m_children.append(new ReaderBlockAccessible(const_cast<ReaderAccessible *>(this), m_children.size()));
    return m_children.at(index);
}

int ReaderAccessible::childCount() const
{
    ensureBuilt();
    return m_blocks.size();
}

int ReaderAccessible::indexOfChild(const QAccessibleInterface *child) const
{
    return m_children.indexOf(const_cast<QAccessibleInterface *>(child));
}

QString ReaderAccessible::text(QAccessible::Text t) const
{
    if (!m_view)
        return {};
    switch (t) {
    case QAccessible::Name: {
        const QString name = m_view->accessibleName();
        return name.isEmpty() ? m_view->window()->windowTitle() : name;
    }
    case QAccessible::Description:
        return QCoreApplication::translate("ReaderAccessible", "Markdown document, %1 blocks")
            .arg(blocks().size());
    case QAccessible::Value:
        return documentText();
    default:
        return {};
    }
}

void ReaderAccessible::setText(QAccessible::Text, const QString &)
{
    // Read-only reader: nothing to set.
}

QRect ReaderAccessible::rect() const
{
    return m_view ? m_view->rect() : QRect();
}

QAccessible::Role ReaderAccessible::role() const
{
    return QAccessible::Document;
}

QAccessible::State ReaderAccessible::state() const
{
    QAccessible::State state;
    state.readOnly = true;
    state.focusable = true;
    if (m_view && m_view->hasFocus())
        state.focused = true;
    return state;
}

// --- text interface (the whole document) ---

void ReaderAccessible::selection(int, int *startOffset, int *endOffset) const
{
    if (startOffset)
        *startOffset = 0;
    if (endOffset)
        *endOffset = 0;
}

int ReaderAccessible::selectionCount() const
{
    return 0;
}

void ReaderAccessible::addSelection(int, int) {}
void ReaderAccessible::removeSelection(int) {}
void ReaderAccessible::setSelection(int, int, int) {}

int ReaderAccessible::cursorPosition() const
{
    return 0;
}

void ReaderAccessible::setCursorPosition(int) {}

QString ReaderAccessible::text(int startOffset, int endOffset) const
{
    const QString &full = documentText();
    if (startOffset < 0 || endOffset > full.size() || startOffset >= endOffset)
        return {};
    return full.mid(startOffset, endOffset - startOffset);
}

int ReaderAccessible::characterCount() const
{
    return documentText().size();
}

QRect ReaderAccessible::characterRect(int) const
{
    return {};
}

int ReaderAccessible::offsetAtPoint(const QPoint &) const
{
    return -1;
}

void ReaderAccessible::scrollToSubstring(int, int) {}

QString ReaderAccessible::attributes(int offset, int *startOffset, int *endOffset) const
{
    if (startOffset)
        *startOffset = offset;
    if (endOffset)
        *endOffset = documentText().size();
    return QString();
}

// --- ReaderBlockAccessible ---

ReaderBlockAccessible::ReaderBlockAccessible(ReaderAccessible *parent, int index)
    : m_parent(parent)
    , m_index(index)
{
}

ReaderBlockAccessible::~ReaderBlockAccessible() = default;

bool ReaderBlockAccessible::isValid() const
{
    return m_parent && m_index >= 0 && m_index < m_parent->blocks().size();
}

QAccessibleInterface *ReaderBlockAccessible::parent() const
{
    return m_parent;
}

QString ReaderBlockAccessible::text(QAccessible::Text t) const
{
    if (!isValid())
        return {};
    switch (t) {
    case QAccessible::Name:
    case QAccessible::Value:
        return m_parent->blocks().at(m_index).text;
    default:
        return {};
    }
}

QRect ReaderBlockAccessible::rect() const
{
    return isValid() ? m_parent->blocks().at(m_index).rect : QRect();
}

QAccessible::Role ReaderBlockAccessible::role() const
{
    return isValid() ? m_parent->blocks().at(m_index).role : QAccessible::Paragraph;
}

QAccessible::State ReaderBlockAccessible::state() const
{
    QAccessible::State state;
    state.readOnly = true;
    return state;
}

void ReaderBlockAccessible::selection(int, int *startOffset, int *endOffset) const
{
    if (startOffset)
        *startOffset = 0;
    if (endOffset)
        *endOffset = 0;
}

QString ReaderBlockAccessible::text(int startOffset, int endOffset) const
{
    const QString blockText = text(QAccessible::Value);
    if (startOffset < 0 || endOffset > blockText.size() || startOffset >= endOffset)
        return {};
    return blockText.mid(startOffset, endOffset - startOffset);
}

int ReaderBlockAccessible::characterCount() const
{
    return text(QAccessible::Value).size();
}

QString ReaderBlockAccessible::attributes(int offset, int *startOffset, int *endOffset) const
{
    if (startOffset)
        *startOffset = offset;
    if (endOffset)
        *endOffset = characterCount();
    return QString();
}
