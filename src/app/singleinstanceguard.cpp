// SPDX-License-Identifier: GPL-2.0-or-later

#include "singleinstanceguard.h"
#include "mainwindow.h"

#include <QDataStream>
#include <QLocalSocket>

SingleInstanceGuard::SingleInstanceGuard(MainWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

QString SingleInstanceGuard::serverName()
{
    return QStringLiteral("Penelope-%1").arg(qEnvironmentVariable("USER", QStringLiteral("default")));
}

bool SingleInstanceGuard::tryAcquire(const QStringList &filePaths)
{
    // Try to connect to an existing instance
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (socket.waitForConnected(500)) {
        // Another instance is running — send file paths and exit
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        out << filePaths;
        socket.write(data);
        socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
        return false;
    }

    // We are the primary instance — start listening
    QLocalServer::removeServer(serverName()); // clean up stale socket
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceGuard::onNewConnection);
    m_server->listen(serverName());
    return true;
}

void SingleInstanceGuard::onNewConnection()
{
    while (QLocalSocket *conn = m_server->nextPendingConnection()) {
        connect(conn, &QLocalSocket::readyRead, this, [this, conn]() {
            QByteArray data = conn->readAll();
            QDataStream in(data);
            QStringList paths;
            in >> paths;
            if (!paths.isEmpty())
                m_window->activateWithFiles(paths);
            else {
                m_window->raise();
                m_window->activateWindow();
            }
            conn->deleteLater();
        });
        // Handle disconnect without data
        connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
    }
}
