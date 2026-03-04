#ifndef PENELOPE_STYLETREEMODEL_H
#define PENELOPE_STYLETREEMODEL_H

#include <QAbstractItemModel>
#include <QStringList>

class StyleManager;

class StyleTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit StyleTreeModel(QObject *parent = nullptr);
    ~StyleTreeModel() override;

    void setStyleManager(StyleManager *sm);
    StyleManager *styleManager() const { return m_styleManager; }

    void setShowPreviews(bool show);
    bool showPreviews() const { return m_showPreviews; }

    // Returns the style name for the given index, or empty for category nodes
    QString styleName(const QModelIndex &index) const;
    bool isParagraphStyle(const QModelIndex &index) const;
    bool isCharacterStyle(const QModelIndex &index) const;
    bool isTableStyle(const QModelIndex &index) const;
    bool isFootnoteNode(const QModelIndex &index) const;
    bool isCategoryNode(const QModelIndex &index) const;

    void refresh();

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct TreeNode {
        QString name;
        QString styleName;   // empty for category nodes
        enum NodeType { Category, ParagraphStyleNode, CharacterStyleNode,
                        TableStyleNode, FootnoteNode };
        NodeType nodeType = Category;
        TreeNode *parent = nullptr;
        QList<TreeNode *> children;
        ~TreeNode() { qDeleteAll(children); }
    };

    void rebuildTree();
    void buildSemanticTree(TreeNode *root);
    TreeNode *findChildByStyleName(TreeNode *parent, const QString &name) const;

    StyleManager *m_styleManager = nullptr;
    TreeNode *m_rootNode = nullptr;
    bool m_showPreviews = false;
};

#endif // PENELOPE_STYLETREEMODEL_H
