#ifndef PENELOPE_FILEBROWSERDOCK_H
#define PENELOPE_FILEBROWSERDOCK_H

#include <QWidget>

class QTreeView;
class QLineEdit;
class KDirModel;
class KDirSortFilterProxyModel;

class FileBrowserDock : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserDock(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    QString rootPath() const;

Q_SIGNALS:
    void fileActivated(const QUrl &url);

private Q_SLOTS:
    void onItemActivated(const QModelIndex &index);
    void onPathEdited();

private:
    QTreeView *m_treeView = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    KDirModel *m_dirModel = nullptr;
    KDirSortFilterProxyModel *m_proxyModel = nullptr;
};

#endif // PENELOPE_FILEBROWSERDOCK_H
