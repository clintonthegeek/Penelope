#ifndef PENELOPE_TABLESTYLE_H
#define PENELOPE_TABLESTYLE_H

#include <QColor>
#include <QMarginsF>
#include <QString>

class TableStyle
{
public:
    TableStyle() = default;
    explicit TableStyle(const QString &name);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Border model
    void setBorderCollapse(bool on) { m_borderCollapse = on; }

    // Cell padding (points)
    QMarginsF cellPadding() const { return m_cellPadding; }
    void setCellPadding(const QMarginsF &p) { m_cellPadding = p; }

    // Colors
    QColor headerBackground() const { return m_headerBackground; }
    void setHeaderBackground(const QColor &c) { m_headerBackground = c; m_hasHeaderBackground = true; }
    bool hasHeaderBackground() const { return m_hasHeaderBackground; }

    QColor headerForeground() const { return m_headerForeground; }
    void setHeaderForeground(const QColor &c) { m_headerForeground = c; m_hasHeaderForeground = true; }
    bool hasHeaderForeground() const { return m_hasHeaderForeground; }

    QColor bodyBackground() const { return m_bodyBackground; }
    void setBodyBackground(const QColor &c) { m_bodyBackground = c; m_hasBodyBackground = true; }
    bool hasBodyBackground() const { return m_hasBodyBackground; }

    QColor alternateRowColor() const { return m_alternateRowColor; }
    void setAlternateRowColor(const QColor &c) { m_alternateRowColor = c; m_hasAlternateRowColor = true; }
    bool hasAlternateRowColor() const { return m_hasAlternateRowColor; }

    int alternateFrequency() const { return m_alternateFrequency; }
    void setAlternateFrequency(int n) { m_alternateFrequency = n; }

    // Border definitions
    struct Border {
        qreal width = 0.5;
        QColor color{0x33, 0x33, 0x33};
        Qt::PenStyle style = Qt::SolidLine;
    };

    Border outerBorder() const { return m_outerBorder; }
    void setOuterBorder(const Border &b) { m_outerBorder = b; m_hasOuterBorder = true; }
    bool hasOuterBorder() const { return m_hasOuterBorder; }

    Border innerBorder() const { return m_innerBorder; }
    void setInnerBorder(const Border &b) { m_innerBorder = b; m_hasInnerBorder = true; }
    bool hasInnerBorder() const { return m_hasInnerBorder; }

    Border headerBottomBorder() const { return m_headerBottomBorder; }
    void setHeaderBottomBorder(const Border &b) { m_headerBottomBorder = b; m_hasHeaderBottomBorder = true; }
    bool hasHeaderBottomBorder() const { return m_hasHeaderBottomBorder; }

    // Style references (paragraph style names for cell content)
    QString headerParagraphStyle() const { return m_headerParagraphStyle; }
    void setHeaderParagraphStyle(const QString &s) { m_headerParagraphStyle = s; }

    QString bodyParagraphStyle() const { return m_bodyParagraphStyle; }
    void setBodyParagraphStyle(const QString &s) { m_bodyParagraphStyle = s; }

private:
    QString m_name;

    bool m_borderCollapse = true;
    QMarginsF m_cellPadding{4, 3, 4, 3};

    QColor m_headerBackground{0xe8, 0xe8, 0xe8};
    QColor m_headerForeground;
    QColor m_bodyBackground{Qt::white};
    QColor m_alternateRowColor{0xf9, 0xf9, 0xf9};
    int m_alternateFrequency = 1;

    bool m_hasHeaderBackground = false;
    bool m_hasHeaderForeground = false;
    bool m_hasBodyBackground = false;
    bool m_hasAlternateRowColor = false;

    Border m_outerBorder{1.0, QColor(0x33, 0x33, 0x33), Qt::SolidLine};
    Border m_innerBorder{0.5, QColor(0xcc, 0xcc, 0xcc), Qt::SolidLine};
    Border m_headerBottomBorder{2.0, QColor(0x33, 0x33, 0x33), Qt::SolidLine};

    bool m_hasOuterBorder = false;
    bool m_hasInnerBorder = false;
    bool m_hasHeaderBottomBorder = false;

    QString m_headerParagraphStyle{QStringLiteral("TableHeader")};
    QString m_bodyParagraphStyle{QStringLiteral("TableBody")};
};

#endif // PENELOPE_TABLESTYLE_H
