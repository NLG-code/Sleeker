#ifndef SLEEKITEM_H
#define SLEEKITEM_H

#include <QWidget>
#include <QLabel>
#include <QIcon>
#include <QString>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QMouseEvent>

// Represents a single desktop item (shortcut, folder, file) inside a panel.
class SleekItem : public QWidget
{
    Q_OBJECT

public:
    explicit SleekItem(const QString &filePath, QWidget *parent = nullptr);

    QString filePath() const { return m_filePath; }
    QString displayName() const;
    void    setFontSize(int px);
    void    setFontFamily(const QString &family);
    void    setIconSize(int px);

signals:
    void activated(const QString &path);
    void dragStarted(SleekItem *item);
    void requestRemove(SleekItem *item);
    void requestReorder(SleekItem *dragged, SleekItem *target);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    void setupUi();
    QIcon resolveIcon() const;
    void refreshIcon();
    void updateLabel();        // re-elide / re-wrap text for current font & width
#ifdef Q_OS_WIN
    bool startNativeFileDrag(); // drag the file's real shell IDataObject
#endif

    // Set for the duration of an in-process drag so panels/items can recognise
    // our own drag (the native shell drag carries no Qt source or custom MIME).
    static SleekItem *s_dragSource;

    QString m_filePath;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QPoint  m_dragStartPos;
    bool    m_hovered = false;

    int m_iconSize  = 96;
    int m_itemWidth = 120;
};

#endif // SLEEKITEM_H
