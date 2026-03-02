#include "headerfooterdialog.h"
#include "droptargetlineedit.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDrag>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>

// --- DragTileLabel: a small draggable label for the tile palette ---

class DragTileLabel : public QLabel
{
public:
    DragTileLabel(const QString &displayText, const QString &insertText, QWidget *parent = nullptr)
        : QLabel(displayText, parent)
        , m_insertText(insertText)
    {
        setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
        setMargin(6);
        setCursor(Qt::OpenHandCursor);
        setToolTip(insertText);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_dragStartPos = event->pos();
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        if ((event->pos() - m_dragStartPos).manhattanLength()
            < QApplication::startDragDistance())
            return;

        auto *drag = new QDrag(this);
        auto *mimeData = new QMimeData;
        mimeData->setText(m_insertText);
        drag->setMimeData(mimeData);
        drag->exec(Qt::CopyAction);
    }

private:
    QString m_insertText;
    QPoint m_dragStartPos;
};

// --- HeaderFooterDialog ---

HeaderFooterDialog::HeaderFooterDialog(const PageLayout &layout, QWidget *parent)
    : QDialog(parent)
    , m_baseLayout(layout)
{
    setWindowTitle(tr("Edit Headers & Footers"));
    setMinimumWidth(600);

    auto *mainLayout = new QVBoxLayout(this);

    // Tile palette
    mainLayout->addWidget(createTilePalette());

    // --- First Page (checkable group box) ---
    m_firstPageGroup = new QGroupBox(tr("First Page"));
    m_firstPageGroup->setCheckable(true);
    m_firstPageGroup->setChecked(false);
    auto *firstPageLayout = new QVBoxLayout(m_firstPageGroup);

    auto *firstHeaderLabel = new QLabel(tr("Header:"));
    firstPageLayout->addWidget(firstHeaderLabel);
    firstPageLayout->addWidget(createFieldRow(m_firstHeaderLeftEdit, m_firstHeaderCenterEdit, m_firstHeaderRightEdit));

    auto *firstFooterLabel = new QLabel(tr("Footer:"));
    firstPageLayout->addWidget(firstFooterLabel);
    firstPageLayout->addWidget(createFieldRow(m_firstFooterLeftEdit, m_firstFooterCenterEdit, m_firstFooterRightEdit));

    mainLayout->addWidget(m_firstPageGroup);

    // --- Main (regular group box) ---
    auto *mainGroup = new QGroupBox(tr("Main"));
    auto *mainGroupLayout = new QVBoxLayout(mainGroup);

    // Wrapper for Main's header/footer fields (enabled/disabled as a unit)
    m_mainHeaderFooterRow = new QWidget;
    auto *mainFieldsLayout = new QVBoxLayout(m_mainHeaderFooterRow);
    mainFieldsLayout->setContentsMargins(0, 0, 0, 0);

    auto *mainHeaderLabel = new QLabel(tr("Header:"));
    mainFieldsLayout->addWidget(mainHeaderLabel);
    mainFieldsLayout->addWidget(createFieldRow(m_headerLeftEdit, m_headerCenterEdit, m_headerRightEdit));

    auto *mainFooterLabel = new QLabel(tr("Footer:"));
    mainFieldsLayout->addWidget(mainFooterLabel);
    mainFieldsLayout->addWidget(createFieldRow(m_footerLeftEdit, m_footerCenterEdit, m_footerRightEdit));

    mainGroupLayout->addWidget(m_mainHeaderFooterRow);

    // Different odd/even checkbox
    m_differentOddEven = new QCheckBox(tr("Different odd and even pages"));
    mainGroupLayout->addWidget(m_differentOddEven);

    // Odd Pages sub-group (uses "right" edits internally)
    m_oddPagesGroup = new QGroupBox(tr("Odd Pages"));
    auto *oddLayout = new QVBoxLayout(m_oddPagesGroup);

    auto *oddHeaderLabel = new QLabel(tr("Header:"));
    oddLayout->addWidget(oddHeaderLabel);
    oddLayout->addWidget(createFieldRow(m_rightHeaderLeftEdit, m_rightHeaderCenterEdit, m_rightHeaderRightEdit));

    auto *oddFooterLabel = new QLabel(tr("Footer:"));
    oddLayout->addWidget(oddFooterLabel);
    oddLayout->addWidget(createFieldRow(m_rightFooterLeftEdit, m_rightFooterCenterEdit, m_rightFooterRightEdit));

    mainGroupLayout->addWidget(m_oddPagesGroup);

    // Even Pages sub-group (uses "left" edits internally)
    m_evenPagesGroup = new QGroupBox(tr("Even Pages"));
    auto *evenLayout = new QVBoxLayout(m_evenPagesGroup);

    auto *evenHeaderLabel = new QLabel(tr("Header:"));
    evenLayout->addWidget(evenHeaderLabel);
    evenLayout->addWidget(createFieldRow(m_leftHeaderLeftEdit, m_leftHeaderCenterEdit, m_leftHeaderRightEdit));

    auto *evenFooterLabel = new QLabel(tr("Footer:"));
    evenLayout->addWidget(evenFooterLabel);
    evenLayout->addWidget(createFieldRow(m_leftFooterLeftEdit, m_leftFooterCenterEdit, m_leftFooterRightEdit));

    mainGroupLayout->addWidget(m_evenPagesGroup);

    mainLayout->addWidget(mainGroup);

    // Button box
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // State connections
    connect(m_differentOddEven, &QCheckBox::toggled, this, &HeaderFooterDialog::updateFieldStates);

    // Load initial values
    loadFromLayout(layout);
    updateFieldStates();
}

QWidget *HeaderFooterDialog::createTilePalette()
{
    auto *group = new QGroupBox(tr("Drag tiles into fields below"));
    auto *layout = new QHBoxLayout(group);
    layout->setSpacing(8);

    struct TileDef { QString label; QString insert; };
    const TileDef tiles[] = {
        {tr("Page X of Y"),  QStringLiteral("{page} / {pages}")},
        {tr("Page Number"),  QStringLiteral("{page}")},
        {tr("Title"),        QStringLiteral("{title}")},
        {tr("Filename"),     QStringLiteral("{filename}")},
        {tr("Date"),         QStringLiteral("{date}")},
        {tr("Full Date"),    QStringLiteral("{date:d MMMM yyyy}")},
    };

    for (const auto &tile : tiles) {
        layout->addWidget(new DragTileLabel(tile.label, tile.insert, group));
    }
    layout->addStretch();

    return group;
}

QWidget *HeaderFooterDialog::createFieldRow(DropTargetLineEdit *&leftEdit,
                                             DropTargetLineEdit *&centerEdit,
                                             DropTargetLineEdit *&rightEdit)
{
    auto *widget = new QWidget;
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);

    row->addWidget(new QLabel(tr("Left:")));
    leftEdit = new DropTargetLineEdit;
    row->addWidget(leftEdit, 1);

    row->addWidget(new QLabel(tr("Center:")));
    centerEdit = new DropTargetLineEdit;
    row->addWidget(centerEdit, 1);

    row->addWidget(new QLabel(tr("Right:")));
    rightEdit = new DropTargetLineEdit;
    row->addWidget(rightEdit, 1);

    return widget;
}

void HeaderFooterDialog::updateFieldStates()
{
    const bool oddEven = m_differentOddEven->isChecked();
    m_mainHeaderFooterRow->setEnabled(!oddEven);
    m_oddPagesGroup->setEnabled(oddEven);
    m_evenPagesGroup->setEnabled(oddEven);
}

void HeaderFooterDialog::loadFromLayout(const PageLayout &layout)
{
    // Default fields
    m_headerLeftEdit->setText(layout.headerLeft);
    m_headerCenterEdit->setText(layout.headerCenter);
    m_headerRightEdit->setText(layout.headerRight);
    m_footerLeftEdit->setText(layout.footerLeft);
    m_footerCenterEdit->setText(layout.footerCenter);
    m_footerRightEdit->setText(layout.footerRight);

    // Check if first page master exists
    bool hasFirst = layout.masterPages.contains(QStringLiteral("first"));
    m_firstPageGroup->setChecked(hasFirst);
    if (hasFirst) {
        const MasterPage &mp = layout.masterPages[QStringLiteral("first")];
        if (mp.hasHeaderLeft)   m_firstHeaderLeftEdit->setText(mp.headerLeft);
        if (mp.hasHeaderCenter) m_firstHeaderCenterEdit->setText(mp.headerCenter);
        if (mp.hasHeaderRight)  m_firstHeaderRightEdit->setText(mp.headerRight);
        if (mp.hasFooterLeft)   m_firstFooterLeftEdit->setText(mp.footerLeft);
        if (mp.hasFooterCenter) m_firstFooterCenterEdit->setText(mp.footerCenter);
        if (mp.hasFooterRight)  m_firstFooterRightEdit->setText(mp.footerRight);
    }

    // Check if left/right masters exist
    bool hasLeft = layout.masterPages.contains(QStringLiteral("left"));
    bool hasRight = layout.masterPages.contains(QStringLiteral("right"));
    m_differentOddEven->setChecked(hasLeft || hasRight);
    if (hasLeft) {
        const MasterPage &mp = layout.masterPages[QStringLiteral("left")];
        if (mp.hasHeaderLeft)   m_leftHeaderLeftEdit->setText(mp.headerLeft);
        if (mp.hasHeaderCenter) m_leftHeaderCenterEdit->setText(mp.headerCenter);
        if (mp.hasHeaderRight)  m_leftHeaderRightEdit->setText(mp.headerRight);
        if (mp.hasFooterLeft)   m_leftFooterLeftEdit->setText(mp.footerLeft);
        if (mp.hasFooterCenter) m_leftFooterCenterEdit->setText(mp.footerCenter);
        if (mp.hasFooterRight)  m_leftFooterRightEdit->setText(mp.footerRight);
    }
    if (hasRight) {
        const MasterPage &mp = layout.masterPages[QStringLiteral("right")];
        if (mp.hasHeaderLeft)   m_rightHeaderLeftEdit->setText(mp.headerLeft);
        if (mp.hasHeaderCenter) m_rightHeaderCenterEdit->setText(mp.headerCenter);
        if (mp.hasHeaderRight)  m_rightHeaderRightEdit->setText(mp.headerRight);
        if (mp.hasFooterLeft)   m_rightFooterLeftEdit->setText(mp.footerLeft);
        if (mp.hasFooterCenter) m_rightFooterCenterEdit->setText(mp.footerCenter);
        if (mp.hasFooterRight)  m_rightFooterRightEdit->setText(mp.footerRight);
    }
}

PageLayout HeaderFooterDialog::result() const
{
    PageLayout pl = m_baseLayout;

    // Write back default fields
    pl.headerLeft = m_headerLeftEdit->text();
    pl.headerCenter = m_headerCenterEdit->text();
    pl.headerRight = m_headerRightEdit->text();
    pl.footerLeft = m_footerLeftEdit->text();
    pl.footerCenter = m_footerCenterEdit->text();
    pl.footerRight = m_footerRightEdit->text();

    // Clear master pages we manage (preserve margin overrides from other sources)
    pl.masterPages.remove(QStringLiteral("first"));
    pl.masterPages.remove(QStringLiteral("left"));
    pl.masterPages.remove(QStringLiteral("right"));

    // First page
    if (m_firstPageGroup->isChecked()) {
        MasterPage mp;
        mp.name = QStringLiteral("first");

        auto setIfNonEmpty = [](const DropTargetLineEdit *edit, QString &field, bool &hasField) {
            if (!edit->text().isEmpty()) {
                field = edit->text();
                hasField = true;
            }
        };

        setIfNonEmpty(m_firstHeaderLeftEdit,   mp.headerLeft,   mp.hasHeaderLeft);
        setIfNonEmpty(m_firstHeaderCenterEdit,  mp.headerCenter, mp.hasHeaderCenter);
        setIfNonEmpty(m_firstHeaderRightEdit,   mp.headerRight,  mp.hasHeaderRight);
        setIfNonEmpty(m_firstFooterLeftEdit,    mp.footerLeft,   mp.hasFooterLeft);
        setIfNonEmpty(m_firstFooterCenterEdit,  mp.footerCenter, mp.hasFooterCenter);
        setIfNonEmpty(m_firstFooterRightEdit,   mp.footerRight,  mp.hasFooterRight);

        if (!mp.isDefault())
            pl.masterPages.insert(QStringLiteral("first"), mp);
    }

    // Odd/even pages
    if (m_differentOddEven->isChecked()) {
        auto buildMasterPage = [](const QString &name,
                                   const DropTargetLineEdit *hL, const DropTargetLineEdit *hC, const DropTargetLineEdit *hR,
                                   const DropTargetLineEdit *fL, const DropTargetLineEdit *fC, const DropTargetLineEdit *fR) {
            MasterPage mp;
            mp.name = name;
            auto setIfNonEmpty = [](const DropTargetLineEdit *edit, QString &field, bool &hasField) {
                if (!edit->text().isEmpty()) {
                    field = edit->text();
                    hasField = true;
                }
            };
            setIfNonEmpty(hL, mp.headerLeft,   mp.hasHeaderLeft);
            setIfNonEmpty(hC, mp.headerCenter, mp.hasHeaderCenter);
            setIfNonEmpty(hR, mp.headerRight,  mp.hasHeaderRight);
            setIfNonEmpty(fL, mp.footerLeft,   mp.hasFooterLeft);
            setIfNonEmpty(fC, mp.footerCenter, mp.hasFooterCenter);
            setIfNonEmpty(fR, mp.footerRight,  mp.hasFooterRight);
            return mp;
        };

        MasterPage leftMp = buildMasterPage(QStringLiteral("left"),
            m_leftHeaderLeftEdit, m_leftHeaderCenterEdit, m_leftHeaderRightEdit,
            m_leftFooterLeftEdit, m_leftFooterCenterEdit, m_leftFooterRightEdit);
        if (!leftMp.isDefault())
            pl.masterPages.insert(QStringLiteral("left"), leftMp);

        MasterPage rightMp = buildMasterPage(QStringLiteral("right"),
            m_rightHeaderLeftEdit, m_rightHeaderCenterEdit, m_rightHeaderRightEdit,
            m_rightFooterLeftEdit, m_rightFooterCenterEdit, m_rightFooterRightEdit);
        if (!rightMp.isDefault())
            pl.masterPages.insert(QStringLiteral("right"), rightMp);
    }

    return pl;
}
