#pragma once

#include <QWidget>

class SleekManager;

// Full-screen transparent overlay that hosts all panel widgets.
// On Windows, this parents itself into the desktop shell hierarchy.
class DesktopOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit DesktopOverlay(QWidget *parent = nullptr);
    ~DesktopOverlay() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void embedInDesktop();
    void setupTrayIcon();
    void registerHotkey();
    void unregisterHotkey();
    void toggleAllPanels();
    QString configFilePath() const;

    SleekManager *m_manager    = nullptr;
    bool          m_panelsHidden = false;
    static constexpr int HOTKEY_ID = 1;   // Win+` hotkey identifier
};
