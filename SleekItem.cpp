#include "SleekItem.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFileIconProvider>
#include <QFontMetrics>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QStyle>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

SleekItem::SleekItem(const QString &filePath, QWidget *parent)
    : QWidget(parent)
    , m_filePath(filePath)
{
    setupUi();
    setFixedWidth(m_itemWidth);
    setCursor(Qt::PointingHandCursor);
    setToolTip(filePath);
    setAttribute(Qt::WA_Hover, true);
    setAcceptDrops(true);
}

QString SleekItem::displayName() const
{
    QFileInfo fi(m_filePath);
    // For .lnk shortcuts, strip the extension
    QString name = fi.completeBaseName();
    if (fi.suffix().toLower() == "lnk" || fi.suffix().toLower() == "url") {
        return name;
    }
    return fi.fileName();
}

void SleekItem::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->setAlignment(Qt::AlignHCenter);

    // Icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(m_iconSize, m_iconSize);
    QIcon icon = resolveIcon();
    m_iconLabel->setPixmap(icon.pixmap(m_iconSize, m_iconSize));
    layout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

    // Text — single line, "…" if too long
    QFont labelFont;
    labelFont.setPixelSize(10);
    QFontMetrics fm(labelFont);
    const int maxW = m_itemWidth - 6;

    m_textLabel = new QLabel(fm.elidedText(displayName(), Qt::ElideRight, maxW), this);
    m_textLabel->setFont(labelFont);
    m_textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_textLabel->setWordWrap(false);
    m_textLabel->setFixedWidth(maxW);
    m_textLabel->setFixedHeight(fm.lineSpacing() + 2);
    m_textLabel->setStyleSheet("QLabel { color: white; background: transparent; }");
    layout->addWidget(m_textLabel, 0, Qt::AlignHCenter);
}

QIcon SleekItem::resolveIcon() const
{
    QFileIconProvider provider;

#ifdef Q_OS_WIN
    // On Windows, use SHGetFileInfo for richer icon extraction (handles .lnk targets etc.)
    SHFILEINFOW sfi = {};
    DWORD_PTR hr = SHGetFileInfoW(
        reinterpret_cast<const wchar_t *>(m_filePath.utf16()),
        0, &sfi, sizeof(sfi),
        SHGFI_ICON | SHGFI_LARGEICON
    );
    if (hr && sfi.hIcon) {
        QPixmap pix = QPixmap::fromImage(QImage::fromHICON(sfi.hIcon));
        DestroyIcon(sfi.hIcon);
        if (!pix.isNull())
            return QIcon(pix);
    }
#endif

    // Fallback: Qt's built-in provider
    QFileInfo fi(m_filePath);
    return provider.icon(fi);
}

void SleekItem::refreshIcon()
{
    QIcon icon = resolveIcon();
    m_iconLabel->setPixmap(icon.pixmap(m_iconSize, m_iconSize));
}

// --- Events ---

// Shared helper — applies a fully-formed QFont to the label and re-elides text.
static void applyLabelFont(QLabel *label, const QFont &f,
                            const QString &text, int maxW)
{
    label->setFont(f);
    QFontMetrics fm(f);
    label->setText(fm.elidedText(text, Qt::ElideRight, maxW));
    label->setFixedHeight(fm.lineSpacing() + 2);
}

void SleekItem::setFontSize(int px)
{
    QFont f = m_textLabel->font();
    f.setPixelSize(px);
    applyLabelFont(m_textLabel, f, displayName(), m_itemWidth - 6);
}

void SleekItem::setFontFamily(const QString &family)
{
    QFont f = m_textLabel->font();
    f.setFamily(family);
    applyLabelFont(m_textLabel, f, displayName(), m_itemWidth - 6);
}

void SleekItem::setIconSize(int px)
{
    m_iconSize  = px;
    m_itemWidth = qMax(64, px + 26);  // ensure width accommodates icon + padding

    m_iconLabel->setFixedSize(m_iconSize, m_iconSize);
    refreshIcon();

    setFixedWidth(m_itemWidth);

    // Re-elide text for new width
    const int maxW = m_itemWidth - 6;
    m_textLabel->setFixedWidth(maxW);
    QFont f = m_textLabel->font();
    QFontMetrics fm(f);
    m_textLabel->setText(fm.elidedText(displayName(), Qt::ElideRight, maxW));
}

void SleekItem::contextMenuEvent(QContextMenuEvent *event)
{
#ifdef Q_OS_WIN
    // ---------------------------------------------------------------
    // Use the Windows Shell IContextMenu so we get the full native
    // menu: Run as administrator, Open with, Send to, Share, etc.
    // ---------------------------------------------------------------
    static const UINT ID_REMOVE = 0x8000;   // our custom command above shell range

    const QString nativePath = QDir::toNativeSeparators(m_filePath);
    HWND hwnd = reinterpret_cast<HWND>(window()->winId());

    // Parse path → PIDL
    PIDLIST_ABSOLUTE pidlFull = nullptr;
    SFGAOF attrs = 0;
    HRESULT hr = SHParseDisplayName(
        reinterpret_cast<PCWSTR>(nativePath.utf16()),
        nullptr, &pidlFull, 0, &attrs);
    if (SUCCEEDED(hr) && pidlFull) {
        // Bind to the parent shell folder
        IShellFolder *pFolder  = nullptr;
        PCITEMID_CHILD childId = nullptr;
        hr = SHBindToParent(pidlFull, IID_PPV_ARGS(&pFolder), &childId);
        CoTaskMemFree(pidlFull);

        if (SUCCEEDED(hr) && pFolder) {
            IContextMenu *pCtx = nullptr;
            hr = pFolder->GetUIObjectOf(hwnd, 1,
                reinterpret_cast<LPCITEMIDLIST*>(&childId),
                IID_IContextMenu, nullptr, reinterpret_cast<void**>(&pCtx));
            pFolder->Release();

            if (SUCCEEDED(hr) && pCtx) {
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    // Shell fills IDs 1 … 0x7FFE
                    pCtx->QueryContextMenu(hMenu, 0, 1, 0x7FFE,
                                           CMF_NORMAL | CMF_EXPLORE);

                    // Append our own option
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING, ID_REMOVE, L"Remove from panel");

                    UINT cmd = static_cast<UINT>(
                        TrackPopupMenu(hMenu,
                            TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
                            event->globalPos().x(), event->globalPos().y(),
                            0, hwnd, nullptr));
                    DestroyMenu(hMenu);

                    if (cmd == ID_REMOVE) {
                        emit requestRemove(this);
                    } else if (cmd >= 1 && cmd < ID_REMOVE) {
                        CMINVOKECOMMANDINFO ici = {};
                        ici.cbSize = sizeof(ici);
                        ici.hwnd   = hwnd;
                        ici.lpVerb = MAKEINTRESOURCEA(cmd - 1);
                        ici.nShow  = SW_SHOWNORMAL;
                        pCtx->InvokeCommand(&ici);
                    }
                }
                pCtx->Release();
            }
        }
        return; // handled
    }
    // Fall through to basic menu if shell lookup failed
#endif

    // Non-Windows / shell-lookup-failed fallback
    QMenu menu;
    menu.addAction("Open", this, [this]() { emit activated(m_filePath); });
    menu.addAction("Open file location", this, [this]() {
        QProcess::startDetached("explorer.exe",
            { "/select,", QDir::toNativeSeparators(m_filePath) });
    });
    menu.addSeparator();
    menu.addAction("Remove from panel", this, [this]() {
        emit requestRemove(this);
    });
    menu.exec(event->globalPos());
}

void SleekItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit activated(m_filePath);
    }
}

void SleekItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
    }
    QWidget::mousePressEvent(event);
}

void SleekItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if ((event->pos() - m_dragStartPos).manhattanLength()
        < QApplication::startDragDistance())
        return;

    // Initiate drag
    auto *drag = new QDrag(this);
    auto *mimeData = new QMimeData();
    mimeData->setUrls({ QUrl::fromLocalFile(m_filePath) });
    // Custom data so panels can identify internal drags
    mimeData->setData("application/x-sleek-item", m_filePath.toUtf8());
    drag->setMimeData(mimeData);

    QPixmap pix = m_iconLabel->pixmap();
    if (!pix.isNull())
        drag->setPixmap(pix.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    emit dragStarted(this);
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

void SleekItem::paintEvent(QPaintEvent * /*event*/)
{
    if (m_hovered) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(255, 255, 255, 30));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
    }
}

void SleekItem::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-sleek-item")
        && event->source() != this) {
        m_hovered = true;
        update();
        event->acceptProposedAction();
    }
}

void SleekItem::dragLeaveEvent(QDragLeaveEvent * /*event*/)
{
    m_hovered = false;
    update();
}

void SleekItem::dropEvent(QDropEvent *event)
{
    m_hovered = false;
    update();
    if (event->mimeData()->hasFormat("application/x-sleek-item")) {
        auto *dragged = qobject_cast<SleekItem *>(event->source());
        if (dragged && dragged != this) {
            emit requestReorder(dragged, this);
            event->acceptProposedAction();
        }
    }
}

void SleekItem::enterEvent(QEnterEvent * /*event*/)
{
    m_hovered = true;
    update();
}

void SleekItem::leaveEvent(QEvent * /*event*/)
{
    m_hovered = false;
    update();
}
