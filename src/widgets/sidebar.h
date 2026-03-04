#ifndef PENELOPE_SIDEBAR_H
#define PENELOPE_SIDEBAR_H

#include <QWidget>

class KMultiTabBar;
class QStackedWidget;
class ToolView;

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    enum Position { Left, Right };

    explicit Sidebar(Position position, QWidget *parent = nullptr);

    // Add a panel with an icon tab. Returns the tab ID.
    int addPanel(ToolView *view, const QIcon &icon, const QString &tooltip);

    // Show/hide a specific panel by tab ID
    void showPanel(int tabId);
    void hidePanel(int tabId);
    void togglePanel(int tabId);
    bool isPanelVisible(int tabId) const;

    // Collapse/expand the whole sidebar
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);

Q_SIGNALS:
    void panelVisibilityChanged(int tabId, bool visible);

private Q_SLOTS:
    void onTabClicked(int tabId);

private:
    void updateVisibility();
    int tabBarWidth() const;

    Position m_position;
    KMultiTabBar *m_tabBar;
    QStackedWidget *m_stack;
    bool m_collapsed = true;
    int m_activeTab = -1;
    int m_expandedWidth = 250;

    struct PanelInfo {
        int tabId;
        ToolView *view;
    };
    QList<PanelInfo> m_panels;
    int m_nextTabId = 0;
};

#endif // PENELOPE_SIDEBAR_H
