// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_ITEMSELECTORBAR_H
#define PENELOPE_ITEMSELECTORBAR_H

#include <QWidget>

class QComboBox;
class QToolButton;

class ItemSelectorBar : public QWidget
{
    Q_OBJECT

public:
    explicit ItemSelectorBar(QWidget *parent = nullptr);

    void setItems(const QStringList &ids, const QStringList &names,
                  const QStringList &builtinIds);

    QString currentId() const;
    void setCurrentId(const QString &id);

Q_SIGNALS:
    void currentItemChanged(const QString &id);
    void duplicateRequested();
    void saveRequested();
    void deleteRequested();

private:
    void updateButtonStates();

    QComboBox *m_combo = nullptr;
    QToolButton *m_duplicateBtn = nullptr;
    QToolButton *m_saveBtn = nullptr;
    QToolButton *m_deleteBtn = nullptr;
    QStringList m_builtinIds;
};

#endif // PENELOPE_ITEMSELECTORBAR_H
