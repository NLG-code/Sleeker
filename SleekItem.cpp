#include "SleekItem.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFileIconProvider>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
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
#include <commoncontrols.h>   // IImageList, SHGetImageList
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
    layout->setContentsMargins(4, 4, 4, 2);
    layout->setSpacing(2);
    layout->setAlignment(Qt::AlignHCenter);

    // Icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(m_iconSize, m_iconSize);
    refreshIcon();
    layout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

    // Text — 2-line wrap with ellipsis, matching Windows desktop style
    // Windows desktop uses Segoe UI 9pt, white text with drop shadow
    QFont labelFont("Segoe UI", 9);
    const int maxW = m_itemWidth - 4;

    m_textLabel = new QLabel(this);
    m_textLabel->setFont(labelFont);
    m_textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_textLabel->setWordWrap(true);
    m_textLabel->setTextFormat(Qt::RichText);   // enables CSS word-wrap
    m_textLabel->setFixedWidth(maxW);
    m_textLabel->setMaximumHeight(QFontMetrics(labelFont).lineSpacing() * 2 + 4);
    m_textLabel->setStyleSheet(
        "QLabel { color: white; background: transparent; word-wrap: break-word; }");

    // Drop shadow like Windows desktop icons
    auto *shadow = new QGraphicsDropShadowEffect(m_textLabel);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setBlurRadius(4);
    shadow->setOffset(1, 1);
    m_textLabel->setGraphicsEffect(shadow);

    updateLabel();
    layout->addWidget(m_textLabel, 0, Qt::AlignHCenter);
}

// Lay text out on up to two lines, breaking mid-word when a single token is
// wider than the available width. Qt's word-wrap (and the rich-text
// `word-wrap:break-word` CSS) only breaks at spaces, so space-less names like
// folder names ("Cdrama_download", "processing_models") would overflow and clip
// instead of wrapping the way space-separated program names do. We break at a
// nearby separator when one exists, otherwise mid-character, and elide the
// second line — matching Windows desktop icon labels.
static QString wrapLabelText(const QString &text, const QFontMetrics &fm, int maxW)
{
    if (maxW <= 0 || fm.horizontalAdvance(text) <= maxW)
        return text;                       // fits on a single line

    // Greedily find how many characters fit on line 1.
    int cut = text.length();
    for (int i = 1; i <= text.length(); ++i) {
        if (fm.horizontalAdvance(text, i) > maxW) { cut = i - 1; break; }
    }
    if (cut < 1) cut = 1;

    // Prefer breaking at a separator close to the fill point so whole words stay
    // together when they can.
    for (int i = cut; i > qMax(1, cut - 8); --i) {
        const QChar c = text.at(i - 1);
        if (c == QLatin1Char(' ') || c == QLatin1Char('_') ||
            c == QLatin1Char('-') || c == QLatin1Char('.')) { cut = i; break; }
    }

    const QString line1 = text.left(cut);
    QString line2 = text.mid(cut);
    if (line2.startsWith(QLatin1Char(' ')))
        line2 = line2.mid(1);
    line2 = fm.elidedText(line2, Qt::ElideRight, maxW);   // cap at two lines
    return line1 + QLatin1Char('\n') + line2;
}

void SleekItem::updateLabel()
{
    const int maxW = m_itemWidth - 4;
    QFontMetrics fm(m_textLabel->font());

    // Plain text with a manually computed break: we wrap ourselves so that
    // long, space-less names (e.g. folders) break mid-word instead of clipping.
    m_textLabel->setTextFormat(Qt::PlainText);
    m_textLabel->setWordWrap(false);
    m_textLabel->setText(wrapLabelText(displayName(), fm, maxW));
    m_textLabel->setFixedWidth(maxW);
    m_textLabel->setMaximumHeight(fm.lineSpacing() * 2 + 4);
}

QIcon SleekItem::resolveIcon() const
{
    QFileIconProvider provider;

#ifdef Q_OS_WIN
    // Always fetch from the JUMBO (256x256) image list so we have enough
    // resolution to scale to any requested size. QIcon::pixmap() can only
    // scale down, so fetching a smaller source (48x48) then requesting 96px
    // would just pad instead of enlarging.
    int imageListId = 0x04; /*SHIL_JUMBO = 256x256*/

    // Get the system image list index for this file
    SHFILEINFOW sfi = {};
    DWORD_PTR ok = SHGetFileInfoW(
        reinterpret_cast<const wchar_t *>(m_filePath.utf16()),
        0, &sfi, sizeof(sfi),
        SHGFI_SYSICONINDEX
    );
    if (ok) {
        IImageList *pImgList = nullptr;
        HRESULT hr = SHGetImageList(imageListId, IID_PPV_ARGS(&pImgList));
        if (SUCCEEDED(hr) && pImgList) {
            HICON hIcon = nullptr;
            hr = pImgList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
            pImgList->Release();
            if (SUCCEEDED(hr) && hIcon) {
                QPixmap pix = QPixmap::fromImage(QImage::fromHICON(hIcon));
                DestroyIcon(hIcon);
                if (!pix.isNull())
                    return QIcon(pix);
            }
        }
    }

    // Fallback: SHGetFileInfo SHGFI_ICON (32x32)
    sfi = {};
    ok = SHGetFileInfoW(
        reinterpret_cast<const wchar_t *>(m_filePath.utf16()),
        0, &sfi, sizeof(sfi),
        SHGFI_ICON | SHGFI_LARGEICON
    );
    if (ok && sfi.hIcon) {
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
    QPixmap pix = icon.pixmap(256, 256);   // get full-res source
    if (!pix.isNull() && (pix.width() != m_iconSize || pix.height() != m_iconSize))
        pix = pix.scaled(m_iconSize, m_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_iconLabel->setPixmap(pix);
}

// --- Events ---

void SleekItem::setFontSize(int px)
{
    QFont f = m_textLabel->font();
    f.setPixelSize(px);
    m_textLabel->setFont(f);
    updateLabel();
}

void SleekItem::setFontFamily(const QString &family)
{
    QFont f = m_textLabel->font();
    f.setFamily(family);
    m_textLabel->setFont(f);
    updateLabel();
}

void SleekItem::setIconSize(int px)
{
    m_iconSize  = px;
    m_itemWidth = qMax(75, px + 27);  // ensure width accommodates icon + padding

    m_iconLabel->setFixedSize(m_iconSize, m_iconSize);
    refreshIcon();

    setFixedWidth(m_itemWidth);
    updateLabel();
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

    // Parse path -> PIDL
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
                    // Shell fills IDs 1 ... 0x7FFE
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

SleekItem *SleekItem::s_dragSource = nullptr;

#ifdef Q_OS_WIN
bool SleekItem::startNativeFileDrag()
{
    // Drag the file's REAL shell data object (the same one Explorer provides),
    // not Qt's synthetic CF_HDROP. Chromium/Electron drop targets (Discord,
    // browsers, Slack) validate against the shell's IDataObject and reject the
    // synthetic one — which is why dragging from the desktop works but Qt's
    // QDrag doesn't.
    const QString nativePath = QDir::toNativeSeparators(m_filePath);
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(reinterpret_cast<PCWSTR>(nativePath.utf16()),
                                    nullptr, &pidl, 0, nullptr);
    if (FAILED(hr) || !pidl)
        return false;

    IShellFolder *psf = nullptr;
    PCUITEMID_CHILD child = nullptr;
    hr = SHBindToParent(pidl, IID_IShellFolder,
                        reinterpret_cast<void **>(&psf), &child);
    if (FAILED(hr) || !psf) {
        CoTaskMemFree(pidl);
        return false;
    }

    const auto hwnd = reinterpret_cast<HWND>(window()->winId());
    IDataObject *pdo = nullptr;
    hr = psf->GetUIObjectOf(hwnd, 1, &child, IID_IDataObject, nullptr,
                            reinterpret_cast<void **>(&pdo));
    psf->Release();
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !pdo)
        return false;

    s_dragSource = this;          // mark in-process drag for reorder detection
    emit dragStarted(this);

    DWORD effect = 0;
    // COPY/LINK only — never MOVE, so dropping on Explorer can't relocate the
    // real file off disk. SHDoDragDrop supplies a default drop source + image.
    SHDoDragDrop(hwnd, pdo, nullptr, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);

    pdo->Release();
    s_dragSource = nullptr;
    return true;
}
#endif

void SleekItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if ((event->pos() - m_dragStartPos).manhattanLength()
        < QApplication::startDragDistance())
        return;

#ifdef Q_OS_WIN
    if (startNativeFileDrag())
        return;
    // else fall through to the Qt drag below
#endif

    // Fallback drag (non-Windows, or if the shell object couldn't be built)
    auto *drag = new QDrag(this);
    auto *mimeData = new QMimeData();
    mimeData->setUrls({ QUrl::fromLocalFile(m_filePath) });
    // Custom data so panels can identify internal drags
    mimeData->setData("application/x-sleek-item", m_filePath.toUtf8());
    drag->setMimeData(mimeData);

    QPixmap pix = m_iconLabel->pixmap();
    if (!pix.isNull())
        drag->setPixmap(pix.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    s_dragSource = this;
    emit dragStarted(this);
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
    s_dragSource = nullptr;
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
    // Accept only our own in-process drag, for reordering. s_dragSource covers
    // the native shell drag (which carries no Qt source/custom MIME); the MIME
    // check covers the non-Windows Qt fallback drag.
    const bool internal =
        (s_dragSource && s_dragSource != this) ||
        (event->mimeData()->hasFormat("application/x-sleek-item")
         && event->source() != this);
    if (internal) {
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
    // Identify the dragged item: s_dragSource for the native shell drag, or the
    // Qt source for the fallback drag.
    SleekItem *dragged = s_dragSource;
    if (!dragged && event->mimeData()->hasFormat("application/x-sleek-item"))
        dragged = qobject_cast<SleekItem *>(event->source());
    if (dragged && dragged != this) {
        emit requestReorder(dragged, this);
        event->acceptProposedAction();
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

QSize SleekItem::sizeHint() const
{
    QFontMetrics fm(m_textLabel->font());
    int textH = fm.lineSpacing() * 2 + 4;   // 2-line text area
    int totalH = 4 + m_iconSize + 2 + textH + 2;  // margins + icon + spacing + text + bottom
    return QSize(m_itemWidth, totalH);
}
