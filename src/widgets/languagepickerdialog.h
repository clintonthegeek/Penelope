/*
 * languagepickerdialog.h — Searchable syntax-language picker
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_LANGUAGEPICKERDIALOG_H
#define PENELOPE_LANGUAGEPICKERDIALOG_H

#include <QDialog>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

class LanguagePickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LanguagePickerDialog(const QString &currentLanguage,
                                  QWidget *parent = nullptr);

    QString selectedLanguage() const;

private Q_SLOTS:
    void onFilterChanged(const QString &text);
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void populate();
    void applyFilter(const QString &filter);
    void selectLanguage(const QString &language);

    QLineEdit *m_filterEdit = nullptr;
    QListWidget *m_listWidget = nullptr;
    QString m_currentLanguage;
    QString m_selectedLanguage;
};

#endif // PENELOPE_LANGUAGEPICKERDIALOG_H
