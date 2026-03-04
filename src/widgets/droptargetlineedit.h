#ifndef PENELOPE_DROPTARGETLINEEDIT_H
#define PENELOPE_DROPTARGETLINEEDIT_H

#include <QLineEdit>

class QDragEnterEvent;
class QDropEvent;

class DropTargetLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit DropTargetLineEdit(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

#endif // PENELOPE_DROPTARGETLINEEDIT_H
