#include "headerfooterdialog.h"
#include "droptargetlineedit.h"

#include "headerfooterplan.h"
#include "headerfooterrenderer.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QPainter>
#include <QPageSize>
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
    DragTileLabel(const QString &displayText, const QString &insertText,
                  std::function<void(const QString &)> insert, QWidget *parent = nullptr)
        : QLabel(displayText, parent)
        , m_insertText(insertText)
        , m_insert(std::move(insert))
    {
        setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
        setMargin(6);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Click to insert %1, or drag it into a field").arg(insertText));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragStartPos = event->pos();
            m_pressed = true;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        // A click, not a drag: insert into the field the user last worked in.
        if (m_pressed && event->button() == Qt::LeftButton && m_insert
            && (event->pos() - m_dragStartPos).manhattanLength()
                   < QApplication::startDragDistance()) {
            m_insert(m_insertText);
        }
        m_pressed = false;
        QLabel::mouseReleaseEvent(event);
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
    std::function<void(const QString &)> m_insert;
    QPoint m_dragStartPos;
    bool m_pressed = false;
};

// --- HeaderFooterPreview: the page as the fields describe it ---

class HeaderFooterPreview : public QWidget
{
public:
    explicit HeaderFooterPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setLayout(const PageLayout &layout)
    {
        m_layout = layout;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().window());

        const QSizeF page = m_layout.pageSizePoints();
        if (page.isEmpty())
            return;
        // Two pages: the first page, and a regular one, so a first-page or
        // odd/even override is visible before it is accepted.
        const qreal gap = 18.0;
        const qreal availW = (width() - gap * 3) / 2.0;
        const qreal availH = height() - 26.0;
        const qreal scale = qMin(availW / page.width(), availH / page.height());
        const QSizeF drawn(page.width() * scale, page.height() * scale);
        const int pages[2] = {0, 1};
        const QString labels[2] = {tr("First page"), tr("Page 2")};

        for (int i = 0; i < 2; ++i) {
            const QPointF origin(gap + i * (drawn.width() + gap), 6.0);
            const QRectF pageRect(origin, drawn);
            painter.fillRect(pageRect, m_layout.pageBackground.isValid()
                                             ? m_layout.pageBackground : QColor(Qt::white));
            painter.setPen(QPen(QColor(0xb0, 0xb0, 0xb0), 1));
            painter.drawRect(pageRect);

            const HeaderFooterPlan plan = makeHeaderFooterPlan(
                m_layout, pages[i], 3, QStringLiteral("document.md"), QStringLiteral("Title"));
            painter.setPen(QPen(QColor(0xc0, 0xc0, 0xd0), 0.5, Qt::DotLine));
            painter.drawRect(pageRect.adjusted(plan.headerLeft.rect.left() * scale,
                                               plan.headerLeft.rect.top() * scale,
                                               -plan.headerLeft.rect.left() * scale,
                                               -(plan.headerLeft.rect.top() * scale)));

            QFont font(QStringLiteral("Noto Sans"));
            font.setPointSizeF(qMax(4.0, plan.fontSize * scale));
            font.setBold(plan.bold);
            font.setItalic(plan.italic);
            painter.setFont(font);
            painter.setPen(plan.color);
            auto drawField = [&](const HeaderFooterField &field, Qt::Alignment align) {
                if (field.text.isEmpty())
                    return;
                const QRectF target(origin.x() + field.rect.left() * scale,
                                    origin.y() + field.rect.top() * scale,
                                    field.rect.width() * scale,
                                    qMax(8.0, field.rect.height() * scale));
                painter.drawText(target, int(align) | Qt::AlignVCenter, field.text);
            };
            if (plan.headerEnabled) {
                drawField(plan.headerLeft, Qt::AlignLeft);
                drawField(plan.headerCenter, Qt::AlignHCenter);
                drawField(plan.headerRight, Qt::AlignRight);
            }
            if (plan.footerEnabled) {
                drawField(plan.footerLeft, Qt::AlignLeft);
                drawField(plan.footerCenter, Qt::AlignHCenter);
                drawField(plan.footerRight, Qt::AlignRight);
            }
            painter.setPen(QPen(QColor(0xcc, 0xcc, 0xcc), 0.6));
            if (plan.hasHeaderRule)
                painter.drawLine(QPointF(origin.x() + plan.headerRule.left() * scale,
                                         origin.y() + plan.headerRule.top() * scale),
                                 QPointF(origin.x() + plan.headerRule.right() * scale,
                                         origin.y() + plan.headerRule.top() * scale));
            if (plan.hasFooterRule)
                painter.drawLine(QPointF(origin.x() + plan.footerRule.left() * scale,
                                         origin.y() + plan.footerRule.top() * scale),
                                 QPointF(origin.x() + plan.footerRule.right() * scale,
                                         origin.y() + plan.footerRule.top() * scale));

            painter.setPen(palette().color(QPalette::WindowText));
            QFont labelFont = font;
            labelFont.setPointSizeF(8.0);
            labelFont.setBold(false);
            labelFont.setItalic(false);
            painter.setFont(labelFont);
            painter.drawText(QRectF(origin.x(), origin.y() + drawn.height() + 2.0,
                                    drawn.width(), 18.0),
                             Qt::AlignHCenter, labels[i]);
        }
    }

private:
    PageLayout m_layout;
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

    // What the fields look like on the page
    m_preview = new HeaderFooterPreview(this);
    m_preview->setObjectName(QStringLiteral("headerFooterPreview"));
    mainLayout->addWidget(m_preview);

    // Header/footer text styling
    mainLayout->addWidget(createTextRow());

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

    // Any edit redraws the preview.
    for (auto *edit : findChildren<DropTargetLineEdit *>())
        connect(edit, &QLineEdit::textChanged, this, &HeaderFooterDialog::refreshPreview);
    if (m_firstPageGroup)
        connect(m_firstPageGroup, &QGroupBox::toggled, this, &HeaderFooterDialog::refreshPreview);
    if (m_differentOddEven)
        connect(m_differentOddEven, &QCheckBox::toggled, this, &HeaderFooterDialog::refreshPreview);
    refreshPreview();
}

// The header and footer text styling, applied to both page renderers.
QWidget *HeaderFooterDialog::createTextRow()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel(tr("Text:")));
    m_sizeSpin = new QDoubleSpinBox;
    m_sizeSpin->setRange(5.0, 36.0);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setSuffix(QStringLiteral(" pt"));
    layout->addWidget(m_sizeSpin);

    m_colourCombo = new QComboBox;
    const struct { const char *name; QColor colour; } colours[] = {
        {QT_TR_NOOP("Grey"), QColor(0x88, 0x88, 0x88)},
        {QT_TR_NOOP("Black"), QColor(0x1a, 0x1a, 0x1a)},
        {QT_TR_NOOP("Accent"), QColor(0x3d, 0xae, 0xe9)},
        {QT_TR_NOOP("Warm grey"), QColor(0xa0, 0x8a, 0x7a)},
        {QT_TR_NOOP("Custom..."), QColor()},
    };
    for (const auto &entry : colours) {
        m_colourCombo->addItem(tr(entry.name), entry.colour);
        if (entry.colour.isValid())
            m_colourCombo->setItemData(m_colourCombo->count() - 1,
                                       QBrush(entry.colour), Qt::DecorationRole);
    }
    layout->addWidget(m_colourCombo);

    m_boldCheck = new QCheckBox(tr("Bold"));
    layout->addWidget(m_boldCheck);
    m_italicCheck = new QCheckBox(tr("Italic"));
    layout->addWidget(m_italicCheck);
    layout->addStretch();

    connect(m_sizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &HeaderFooterDialog::refreshPreview);
    connect(m_colourCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const QColor chosen = m_colourCombo->itemData(index, Qt::UserRole).value<QColor>();
                if (!chosen.isValid()) {
                    const QColor custom = QColorDialog::getColor(m_baseLayout.chromeColor, this,
                                                                tr("Header and footer colour"));
                    if (custom.isValid()) {
                        m_colourCombo->setItemData(index, custom, Qt::UserRole);
                        m_colourCombo->setItemData(index, QBrush(custom), Qt::DecorationRole);
                    } else {
                        // Cancelled: go back to the colour already in use.
                        const int current = m_colourCombo->findData(
                            QVariant::fromValue(m_baseLayout.chromeColor), Qt::UserRole);
                        if (current >= 0)
                            m_colourCombo->setCurrentIndex(current);
                    }
                }
                refreshPreview();
            });
    connect(m_boldCheck, &QCheckBox::toggled, this, &HeaderFooterDialog::refreshPreview);
    connect(m_italicCheck, &QCheckBox::toggled, this, &HeaderFooterDialog::refreshPreview);

    return row;
}

void HeaderFooterDialog::insertToken(const QString &token)
{
    auto *target = qobject_cast<DropTargetLineEdit *>(QApplication::focusWidget());
    if (!target || !isAncestorOf(target) || !target->isEnabled()) {
        // Nothing focused: take the first empty field of an enabled group.
        for (auto *edit : findChildren<DropTargetLineEdit *>()) {
            if (edit->isEnabled() && edit->isVisible() && edit->text().isEmpty()) {
                target = edit;
                break;
            }
        }
    }
    if (!target) {
        for (auto *edit : findChildren<DropTargetLineEdit *>()) {
            if (edit->isEnabled() && edit->isVisible()) {
                target = edit;
                break;
            }
        }
    }
    if (!target)
        return;
    target->insert(token);
    target->setFocus();
    refreshPreview();
}

// The layout the fields describe right now, without accepting the dialog.
PageLayout HeaderFooterDialog::currentLayout() const
{
    PageLayout pl = result();
    pl.headerEnabled = m_baseLayout.headerEnabled;
    pl.footerEnabled = m_baseLayout.footerEnabled;
    return pl;
}

void HeaderFooterDialog::refreshPreview()
{
    if (!m_preview)
        return;
    PageLayout pl = currentLayout();
    pl.chromeFontSize = m_sizeSpin->value();
    const QVariant chosen = m_colourCombo->currentData(Qt::UserRole);
    if (chosen.isValid() && chosen.value<QColor>().isValid())
        pl.chromeColor = chosen.value<QColor>();
    pl.chromeBold = m_boldCheck->isChecked();
    pl.chromeItalic = m_italicCheck->isChecked();
    m_preview->setLayout(pl);
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
        layout->addWidget(new DragTileLabel(tile.label, tile.insert,
                                            [this](const QString &token) { insertToken(token); },
                                            group));
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
    if (m_sizeSpin)
        m_sizeSpin->setValue(layout.chromeFontSize);
    if (m_boldCheck)
        m_boldCheck->setChecked(layout.chromeBold);
    if (m_italicCheck)
        m_italicCheck->setChecked(layout.chromeItalic);
    if (m_colourCombo) {
        int index = -1;
        for (int i = 0; i < m_colourCombo->count(); ++i) {
            const QColor candidate = m_colourCombo->itemData(i, Qt::UserRole).value<QColor>();
            if (candidate.isValid() && candidate == layout.chromeColor) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            // Not one of the presets: keep it on the Custom entry.
            index = m_colourCombo->count() - 1;
            m_colourCombo->setItemData(index, layout.chromeColor, Qt::UserRole);
            m_colourCombo->setItemData(index, QBrush(layout.chromeColor), Qt::DecorationRole);
        }
        m_colourCombo->setCurrentIndex(index);
    }

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

    if (m_sizeSpin) {
        pl.chromeFontSize = m_sizeSpin->value();
        const QVariant chosen = m_colourCombo->currentData(Qt::UserRole);
        if (chosen.isValid() && chosen.value<QColor>().isValid())
            pl.chromeColor = chosen.value<QColor>();
        pl.chromeBold = m_boldCheck->isChecked();
        pl.chromeItalic = m_italicCheck->isChecked();
    }

    // Write back default fields
    pl.headerLeft = m_headerLeftEdit->text();
    pl.headerCenter = m_headerCenterEdit->text();
    pl.headerRight = m_headerRightEdit->text();
    pl.footerLeft = m_footerLeftEdit->text();
    pl.footerCenter = m_footerCenterEdit->text();
    pl.footerRight = m_footerRightEdit->text();

    // This dialog owns the six header/footer fields of each page type. Every
    // other override — the enabled flags and the margins, which the page panel
    // sets — belongs to the page layout widget and has to survive an OK here.
    auto applyFields = [&pl](const QString &type,
                             const DropTargetLineEdit *hL, const DropTargetLineEdit *hC,
                             const DropTargetLineEdit *hR, const DropTargetLineEdit *fL,
                             const DropTargetLineEdit *fC, const DropTargetLineEdit *fR) {
        auto it = pl.masterPages.find(type);
        MasterPage mp = (it == pl.masterPages.end()) ? MasterPage{} : it.value();
        mp.name = type;

        mp.headerLeft.clear();   mp.hasHeaderLeft = false;
        mp.headerCenter.clear(); mp.hasHeaderCenter = false;
        mp.headerRight.clear();  mp.hasHeaderRight = false;
        mp.footerLeft.clear();   mp.hasFooterLeft = false;
        mp.footerCenter.clear(); mp.hasFooterCenter = false;
        mp.footerRight.clear();  mp.hasFooterRight = false;

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

        if (mp.isDefault())
            pl.masterPages.remove(type);
        else
            pl.masterPages.insert(type, mp);
    };

    if (m_firstPageGroup->isChecked()) {
        applyFields(QStringLiteral("first"),
                    m_firstHeaderLeftEdit, m_firstHeaderCenterEdit, m_firstHeaderRightEdit,
                    m_firstFooterLeftEdit, m_firstFooterCenterEdit, m_firstFooterRightEdit);
    } else {
        // The dialog no longer manages this page type's fields; anything else
        // the page panel set for it stays.
        auto it = pl.masterPages.find(QStringLiteral("first"));
        if (it != pl.masterPages.end()) {
            MasterPage mp = it.value();
            mp.headerLeft.clear();   mp.hasHeaderLeft = false;
            mp.headerCenter.clear(); mp.hasHeaderCenter = false;
            mp.headerRight.clear();  mp.hasHeaderRight = false;
            mp.footerLeft.clear();   mp.hasFooterLeft = false;
            mp.footerCenter.clear(); mp.hasFooterCenter = false;
            mp.footerRight.clear();  mp.hasFooterRight = false;
            if (mp.isDefault())
                pl.masterPages.remove(QStringLiteral("first"));
            else
                pl.masterPages.insert(QStringLiteral("first"), mp);
        }
    }

    if (m_differentOddEven->isChecked()) {
        applyFields(QStringLiteral("left"),
                    m_leftHeaderLeftEdit, m_leftHeaderCenterEdit, m_leftHeaderRightEdit,
                    m_leftFooterLeftEdit, m_leftFooterCenterEdit, m_leftFooterRightEdit);
        applyFields(QStringLiteral("right"),
                    m_rightHeaderLeftEdit, m_rightHeaderCenterEdit, m_rightHeaderRightEdit,
                    m_rightFooterLeftEdit, m_rightFooterCenterEdit, m_rightFooterRightEdit);
    } else {
        for (const QString &type : {QStringLiteral("left"), QStringLiteral("right")}) {
            auto it = pl.masterPages.find(type);
            if (it == pl.masterPages.end())
                continue;
            MasterPage mp = it.value();
            mp.headerLeft.clear();   mp.hasHeaderLeft = false;
            mp.headerCenter.clear(); mp.hasHeaderCenter = false;
            mp.headerRight.clear();  mp.hasHeaderRight = false;
            mp.footerLeft.clear();   mp.hasFooterLeft = false;
            mp.footerCenter.clear(); mp.hasFooterCenter = false;
            mp.footerRight.clear();  mp.hasFooterRight = false;
            if (mp.isDefault())
                pl.masterPages.remove(type);
            else
                pl.masterPages.insert(type, mp);
        }
    }

    return pl;
}
