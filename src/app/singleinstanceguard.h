// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_SINGLEINSTANCEGUARD_H
#define PENELOPE_SINGLEINSTANCEGUARD_H

#include <QLocalServer>
#include <QObject>

class MainWindow;

class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(MainWindow *window, QObject *parent = nullptr);

    /// Try to become the primary instance. Returns true if we are primary.
    /// If another instance is running, sends files to it and returns false.
    bool tryAcquire(const QStringList &filePaths);

private Q_SLOTS:
    void onNewConnection();

private:
    static QString serverName();
    MainWindow *m_window;
    QLocalServer *m_server = nullptr;
};

#endif // PENELOPE_SINGLEINSTANCEGUARD_H
