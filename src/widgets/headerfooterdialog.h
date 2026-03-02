#ifndef PRETTYREADER_HEADERFOOTERDIALOG_H
#define PRETTYREADER_HEADERFOOTERDIALOG_H

#include <QDialog>

#include "pagelayout.h"

class QCheckBox;
class QGroupBox;
class DropTargetLineEdit;

class HeaderFooterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HeaderFooterDialog(const PageLayout &layout, QWidget *parent = nullptr);

    PageLayout result() const;

private:
    QWidget *createTilePalette();
    QWidget *createFieldRow(DropTargetLineEdit *&leftEdit,
                            DropTargetLineEdit *&centerEdit,
                            DropTargetLineEdit *&rightEdit);
    void updateFieldStates();
    void loadFromLayout(const PageLayout &layout);

    // Default header/footer fields
    DropTargetLineEdit *m_headerLeftEdit = nullptr;
    DropTargetLineEdit *m_headerCenterEdit = nullptr;
    DropTargetLineEdit *m_headerRightEdit = nullptr;
    DropTargetLineEdit *m_footerLeftEdit = nullptr;
    DropTargetLineEdit *m_footerCenterEdit = nullptr;
    DropTargetLineEdit *m_footerRightEdit = nullptr;

    // First page overrides
    QGroupBox *m_firstPageGroup = nullptr;
    DropTargetLineEdit *m_firstHeaderLeftEdit = nullptr;
    DropTargetLineEdit *m_firstHeaderCenterEdit = nullptr;
    DropTargetLineEdit *m_firstHeaderRightEdit = nullptr;
    DropTargetLineEdit *m_firstFooterLeftEdit = nullptr;
    DropTargetLineEdit *m_firstFooterCenterEdit = nullptr;
    DropTargetLineEdit *m_firstFooterRightEdit = nullptr;

    // Odd/even page overrides
    QCheckBox *m_differentOddEven = nullptr;
    QWidget *m_mainHeaderFooterRow = nullptr;
    QGroupBox *m_oddPagesGroup = nullptr;
    QGroupBox *m_evenPagesGroup = nullptr;
    DropTargetLineEdit *m_leftHeaderLeftEdit = nullptr;
    DropTargetLineEdit *m_leftHeaderCenterEdit = nullptr;
    DropTargetLineEdit *m_leftHeaderRightEdit = nullptr;
    DropTargetLineEdit *m_leftFooterLeftEdit = nullptr;
    DropTargetLineEdit *m_leftFooterCenterEdit = nullptr;
    DropTargetLineEdit *m_leftFooterRightEdit = nullptr;
    DropTargetLineEdit *m_rightHeaderLeftEdit = nullptr;
    DropTargetLineEdit *m_rightHeaderCenterEdit = nullptr;
    DropTargetLineEdit *m_rightHeaderRightEdit = nullptr;
    DropTargetLineEdit *m_rightFooterLeftEdit = nullptr;
    DropTargetLineEdit *m_rightFooterCenterEdit = nullptr;
    DropTargetLineEdit *m_rightFooterRightEdit = nullptr;

    // Copy of incoming layout for fields we don't edit
    PageLayout m_baseLayout;
};

#endif // PRETTYREADER_HEADERFOOTERDIALOG_H
