#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QList>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QLayout>
#include <QStyle>

class SleekItem;
class FlowLayout;

class SleekPanel : public QWidget
{
    Q_OBJECT

public:
    enum class DockSide : int { None = 0, Left = 1, Right = 2, Top = 3, Bottom = 4 };

    explicit SleekPanel(const QString &title,
                         const QString &watchPath = QString(),
                         QWidget *parent = nullptr);

    QString  title()     const { return m_title; }
    void     setTitle(const QString &title);
    QString  watchPath() const { return m_watchPath; }
    DockSide dockSide()  const { return m_dockSide; }
    void     setDockSide(DockSide side);
    void     setFontSize(int px);
    void     setFontFamily(const QString &family);
    void     setOpacity(int alpha);        // 0–255
    int      opacity() const { return m_bgColor.alpha(); }
    void     setIconScale(int size);       // icon pixel size
    int      iconScale() const { return m_iconSize; }

    QJsonObject toJson() const;
    static SleekPanel *fromJson(const QJsonObject &obj, QWidget *parent = nullptr);

    void addFile(const QString &filePath);
    void removeFile(const QString &filePath);
    void reloadFromPath();

    // Called by DesktopOverlay on global hotkey
    void toggleVisibility();

signals:
    void titleChanged(const QString &newTitle);
    void requestDelete(SleekPanel *panel);
    void requestNewPanel(SleekPanel *sibling);
    void changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setupUi();
    void relayout();
    QRect titleBarRect() const;

    void rebuildLayout();   // re-adds m_items to m_flowLayout in current order

    // Dock helpers
    void applyDock();               // snap to hidden position at edge
    void slideIn();
    void slideOut();
    void checkEdgeProximity();       // poll cursor vs screen edge

    // Search / filter
    void applyFilter(const QString &text);

    // Multi-monitor: returns the screen this panel belongs to
    QScreen *panelScreen() const;

    enum ResizeEdge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
    int  hitTestEdge(const QPoint &pos) const;
    void updateCursorShape(const QPoint &pos);

    // Panel state
    QString  m_title;
    QString  m_watchPath;
    bool     m_collapsed     = false;
    int      m_expandedHeight = 260;

    // Dock state
    DockSide           m_dockSide     = DockSide::None;
    bool               m_dockExpanded = false;
    QRect              m_preDockGeom;
    QTimer            *m_expandDelay  = nullptr;   // debounce before sliding in
    QPropertyAnimation *m_slideAnim  = nullptr;
    QElapsedTimer       m_slideInTime;           // when the panel last slid open
    QTimer             *m_slideOutDelay = nullptr;
    QTimer             *m_edgePoll     = nullptr;  // cursor-at-edge polling

    // Widgets
    QLabel      *m_titleLabel    = nullptr;
    QScrollArea *m_scrollArea    = nullptr;
    QWidget     *m_contentWidget = nullptr;
    FlowLayout  *m_flowLayout    = nullptr;
    QPushButton *m_settingsBtn   = nullptr;
    QPushButton *m_addBtn        = nullptr;
    QPushButton *m_deleteBtn     = nullptr;
    QLineEdit   *m_searchBox     = nullptr;  // filter bar

    QList<SleekItem *> m_items;

    // Interaction state
    bool   m_dragging    = false;
    bool   m_resizing    = false;
    int    m_resizeEdges = None;
    QPoint m_dragOffset;
    QPoint m_resizeOrigin;
    QRect  m_resizeStartGeom;

    static constexpr int TITLE_HEIGHT = 28;
    static constexpr int EDGE_MARGIN  = 6;
    static constexpr int MIN_W        = 92;   // exactly 1 column wide
    static constexpr int MIN_H        = 80;
    static constexpr int BTN_SIZE      = 16;
    static constexpr int SLIVER          = 4;    // fixed px visible when hidden
    static constexpr int SLIVER_RIGHT    = 4;    // strip for right edge
    static constexpr int DEFAULT_TRIGGER = 300;  // default hover delay (ms)
    static constexpr int EDGE_HOTZONE    = 4;    // px from screen edge to trigger dock
    static constexpr int SEARCH_HEIGHT   = 22;   // search bar height

    int m_triggerZone = DEFAULT_TRIGGER;  // hover delay before panel slides in (ms)
    static constexpr int DEFAULT_FONT  = 10;
    static constexpr int DEFAULT_ICON  = 38;

    int     m_fontSize   = DEFAULT_FONT;
    int     m_iconSize   = DEFAULT_ICON;
    QString m_fontFamily;                  // empty = system default

    QColor m_bgColor    {30, 30, 30, 180};
    QColor m_titleColor {45, 45, 45, 200};
    QColor m_borderColor{80, 80, 80, 120};
};

// ---------------------------------------------------------------------------
// FlowLayout
// ---------------------------------------------------------------------------

class FlowLayout : public QLayout
{
    Q_OBJECT

public:
    explicit FlowLayout(QWidget *parent = nullptr, int margin = 4, int hSpacing = 4, int vSpacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int  count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int  heightForWidth(int width) const override;

    void setGeometry(const QRect &rect) override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;

    int horizontalSpacing() const;
    int verticalSpacing() const;

private:
    int doLayout(const QRect &rect, bool testOnly) const;

    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};
