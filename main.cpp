#include <QApplication>
#include <QMessageBox>
#include <QSharedMemory>
#include "DesktopOverlay.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Sleeker");
    app.setOrganizationName("Sleeker");

    // Ensure only one instance runs
    QSharedMemory guard("SleekSingleInstanceGuard");
    if (!guard.create(1)) {
        // Another instance is already running
        QMessageBox::StandardButton answer = QMessageBox::question(
            nullptr, "Sleeker",
            "Sleeker is already running.\nDo you want to close the existing instance?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (answer == QMessageBox::Yes) {
#ifdef Q_OS_WIN
            // Find and close the existing Sleeker window
            HWND existing = nullptr;
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                wchar_t title[64];
                GetWindowTextW(hwnd, title, 64);
                if (wcscmp(title, L"Sleeker") == 0) {
                    *reinterpret_cast<HWND *>(lParam) = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&existing));

            if (existing) {
                PostMessage(existing, WM_CLOSE, 0, 0);
                // Wait briefly for the old instance to release the shared memory
                for (int i = 0; i < 20; ++i) {
                    Sleep(100);
                    if (guard.create(1))
                        goto launch;
                }
            }
#endif
            QMessageBox::warning(nullptr, "Sleeker",
                "Could not close the existing instance.\nPlease close it manually.");
            return 1;
        } else {
            return 0;
        }
    }

#ifdef Q_OS_WIN
launch:
#endif
    DesktopOverlay overlay;
    overlay.show();

    return app.exec();
}
