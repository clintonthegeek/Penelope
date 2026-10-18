#ifndef PENELOPE_READERACCESSIBLE_H
#define PENELOPE_READERACCESSIBLE_H

#include <QAccessible>
#include <QList>
#include <QString>

class DocumentView;
struct ReaderBlock;
class ReaderBlockAccessible;

// The reader draws its own pages, so assistive technology has nothing to read
// unless the view says what it holds. This exposes the document as a text
// interface over its whole text, with one child per top-level block so a screen
// reader can move through the document block by block.
class ReaderAccessible : public QAccessibleInterface, public QAccessibleTextInterface
{
public:
    explicit ReaderAccessible(DocumentView *view);
    ~ReaderAccessible() override;

    // Installs the factory that hands this interface to Qt. Idempotent.
    static void installFactory();

    // QAccessibleInterface
    bool isValid() const override;
    QObject *object() const override;
    QAccessibleInterface *childAt(int x, int y) const override;
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int index) const override;
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text t, const QString &text) override;
    QRect rect() const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    void *interface_cast(QAccessible::InterfaceType type) override
    {
        if (type == QAccessible::TextInterface)
            return static_cast<QAccessibleTextInterface *>(this);
        return nullptr;
    }

    // QAccessibleTextInterface
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override;
    int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;
    int cursorPosition() const override;
    void setCursorPosition(int position) override;
    QString text(int startOffset, int endOffset) const override;
    int characterCount() const override;
    QRect characterRect(int offset) const override;
    int offsetAtPoint(const QPoint &point) const override;
    void scrollToSubstring(int startIndex, int endIndex) override;
    QString attributes(int offset, int *startOffset, int *endOffset) const override;

    // The document's text and its blocks, built once from the content model.
    const QString &documentText() const;
    const QList<ReaderBlock> &blocks() const;

private:
    void ensureBuilt() const;

    DocumentView *m_view = nullptr;
    mutable bool m_built = false;
    mutable QString m_text;
    mutable QList<ReaderBlock> m_blocks;
    mutable QList<ReaderBlockAccessible *> m_children;
};

// One top-level block: a name to read and its own text interface.
class ReaderBlockAccessible : public QAccessibleInterface, public QAccessibleTextInterface
{
public:
    ReaderBlockAccessible(ReaderAccessible *parent, int index);
    ~ReaderBlockAccessible() override;

    bool isValid() const override;
    QObject *object() const override { return nullptr; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text, const QString &) override {}
    QRect rect() const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    void *interface_cast(QAccessible::InterfaceType type) override
    {
        if (type == QAccessible::TextInterface)
            return static_cast<QAccessibleTextInterface *>(this);
        return nullptr;
    }

    void selection(int, int *startOffset, int *endOffset) const override;
    int selectionCount() const override { return 0; }
    void addSelection(int, int) override {}
    void removeSelection(int) override {}
    void setSelection(int, int, int) override {}
    int cursorPosition() const override { return 0; }
    void setCursorPosition(int) override {}
    QString text(int startOffset, int endOffset) const override;
    int characterCount() const override;
    QRect characterRect(int) const override { return {}; }
    int offsetAtPoint(const QPoint &) const override { return -1; }
    void scrollToSubstring(int, int) override {}
    QString attributes(int offset, int *startOffset, int *endOffset) const override;

private:
    ReaderAccessible *m_parent = nullptr;
    int m_index = -1;
};

// What a block exposes: its role and its plain text.
struct ReaderBlock {
    QAccessible::Role role = QAccessible::Paragraph;
    QString text;
    QRect rect;
};

#endif // PENELOPE_READERACCESSIBLE_H
