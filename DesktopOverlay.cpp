#include "DesktopOverlay.h"
#include "SleekManager.h"
#include "SleekPanel.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

DesktopOverlay::DesktopOverlay(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("Sleeker");

    // We want a frameless, transparent, always-on-bottom window
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    // Cover the primary screen
    QScreen *screen = QApplication::primaryScreen();
    if (screen)
        setGeometry(screen->geometry());

    embedInDesktop();
    setupTrayIcon();
    registerHotkey();

    // Create panel manager and tell it where to auto-save
    m_manager = new SleekManager(this, this);
    const QString cfg = configFilePath();
    m_manager->setSavePath(cfg);

    // Load saved config if it exists
    if (QFile::exists(cfg))
        m_manager->loadFromFile(cfg);

    // If nothing loaded (first run, or user deleted all panels), show one empty starter panel
    if (m_manager->panels().isEmpty())
        m_manager->createPanel("New Panel", QString(), QRect(60, 60, 320, 280));

    // Final save on quit — catches anything the 1 s debounce hasn't flushed yet
    connect(qApp, &QApplication::aboutToQuit, this, [this, cfg]() {
        m_manager->saveToFile(cfg);
    });
}

DesktopOverlay::~DesktopOverlay()
{
    unregisterHotkey();
    // Save state on exit
    m_manager->saveToFile(configFilePath());
}

void DesktopOverlay::registerHotkey()
{
#ifdef Q_OS_WIN
    // Register Win+` as a global hotkey to toggle panels
    RegisterHotKey(reinterpret_cast<HWND>(winId()),
                   HOTKEY_ID,
                   MOD_WIN | MOD_NOREPEAT,
                   VK_OEM_3);   // ` / ~ key
#endif
}

void DesktopOverlay::unregisterHotkey()
{
#ifdef Q_OS_WIN
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), HOTKEY_ID);
#endif
}

bool DesktopOverlay::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == HOTKEY_ID) {
        toggleAllPanels();
        if (result) *result = 0;
        return true;
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void DesktopOverlay::toggleAllPanels()
{
    m_panelsHidden = !m_panelsHidden;
    for (auto *panel : m_manager->panels())
        panel->toggleVisibility();
}

void DesktopOverlay::embedInDesktop()
{
#ifdef Q_OS_WIN
    // The standard trick: find the WorkerW window behind desktop icons
    // and parent our overlay into it, so we sit between the wallpaper
    // and the desktop icons.
    //
    // Step 1: Send a special message to Progman to spawn a WorkerW
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        SendMessageTimeoutW(progman, 0x052C, 0, 0,
                            SMTO_NORMAL, 1000, nullptr);
    }

    // Step 2: Find the WorkerW that sits BEHIND the desktop icons.
    HWND workerW = nullptr;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        HWND child = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
        if (child) {
            HWND next = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
            if (next) {
                auto *result = reinterpret_cast<HWND *>(lParam);
                *result = next;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&workerW));

    if (workerW) {
        // Parent our window into the WorkerW
        SetParent(reinterpret_cast<HWND>(winId()), workerW);
    } else {
        // Fallback: just stay on bottom
        setWindowFlags(windowFlags() | Qt::WindowStaysOnBottomHint);
    }
#else
    // On non-Windows, just stay on bottom
    setWindowFlags(windowFlags() | Qt::WindowStaysOnBottomHint);
#endif
}

void DesktopOverlay::setupTrayIcon()
{
    // Use nullptr parent so the tray icon isn't tied to the WorkerW-embedded window.
    auto *tray = new QSystemTrayIcon(
        QApplication::style()->standardIcon(QStyle::SP_DesktopIcon), nullptr);
    tray->setToolTip("Sleeker – right-click to manage\nWin+` to toggle panels");

    // Tray menu lives on the heap independently
    auto *trayMenu = new QMenu(nullptr);
    trayMenu->addAction("Toggle Panels (Win+`)", this, [this]{ toggleAllPanels(); });
    trayMenu->addSeparator();
    trayMenu->addAction("Quit Sleeker", qApp, &QApplication::quit);

    tray->setContextMenu(trayMenu);
    tray->show();

    // Clean up when the app exits
    connect(qApp, &QApplication::aboutToQuit, tray,    [tray, trayMenu]() {
        tray->hide();
        trayMenu->deleteLater();
        tray->deleteLater();
    });
}

QString DesktopOverlay::configFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/sleeker.json";
}

void DesktopOverlay::paintEvent(QPaintEvent *)
{
    // Fully transparent background — panels paint themselves
}

void DesktopOverlay::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *toggleAct = menu.addAction("Toggle Panels (Win+`)");
    menu.addSeparator();
    QAction *quitAct = menu.addAction("Quit Sleeker");

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == toggleAct)
        toggleAllPanels();
    else if (chosen == quitAct)
        qApp->quit();
}

void DesktopOverlay::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
}
