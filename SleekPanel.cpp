#include "SleekPanel.h"
#include "SleekItem.h"

#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// =========================================================================
// SleekPanel
// =========================================================================

SleekPanel::SleekPanel(const QString& title, const QString& watchPath, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
    , m_watchPath(watchPath)
{
    setMinimumSize(MIN_W, MIN_H);
    setAcceptDrops(true);
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);

    setupUi();

    if (!m_watchPath.isEmpty())
        reloadFromPath();
}

// ---------------------------------------------------------------------------
// Windows startup registry helpers
// ---------------------------------------------------------------------------
#ifdef Q_OS_WIN
static const QString kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const QString kAppName = "Sleeker";

static bool startupEnabled()
{
    return QSettings(kRunKey, QSettings::NativeFormat).contains(kAppName);
}

static void setStartupEnabled(bool enable)
{
    QSettings reg(kRunKey, QSettings::NativeFormat);
    if (enable)
        reg.setValue(kAppName,
            QCoreApplication::applicationFilePath().replace('/', '\\'));
    else
        reg.remove(kAppName);
}
#endif

// ---------------------------------------------------------------------------
// Multi-monitor helper
// ---------------------------------------------------------------------------

QScreen *SleekPanel::panelScreen() const
{
    // If the panel is docked, use the screen that contains its pre-dock position.
    // Otherwise use the screen that contains the panel's center.
    QPoint probe = (m_dockSide != DockSide::None)
                       ? m_preDockGeom.center()
                       : geometry().center();

    for (QScreen *s : QApplication::screens()) {
        if (s->geometry().contains(probe))
            return s;
    }
    return QApplication::primaryScreen();
}

void SleekPanel::setupUi()
{
    // Title label
    m_titleLabel = new QLabel(m_title, this);
    m_titleLabel->setStyleSheet(
        "QLabel { color: #cccccc; font-size: 12px; font-weight: bold; background: transparent; }"
    );

    // Title bar action buttons: [+] [✎] [✕]  (left of collapse arrow)
    // Shared style: transparent, dim by default, white on hover
    const QString btnStyle =
        "QPushButton {"
        "  color: rgba(180,180,180,200); background: transparent;"
        "  border: none; font-size: 12px; padding: 0;"
        "}"
        "QPushButton:hover {"
        "  color: white; background: rgba(255,255,255,35); border-radius: 3px;"
        "}"
        "QPushButton:pressed { background: rgba(255,255,255,65); border-radius: 3px; }";

    m_settingsBtn = new QPushButton("⚙", this);
    m_settingsBtn->setStyleSheet(btnStyle);
    m_settingsBtn->setToolTip("Panel settings");
    m_settingsBtn->setCursor(Qt::ArrowCursor);
    m_settingsBtn->setFocusPolicy(Qt::NoFocus);

    m_addBtn = new QPushButton("+", this);
    m_addBtn->setStyleSheet(btnStyle);
    m_addBtn->setToolTip("New panel");
    m_addBtn->setCursor(Qt::ArrowCursor);
    m_addBtn->setFocusPolicy(Qt::NoFocus);

    m_deleteBtn = new QPushButton("✕", this);
    m_deleteBtn->setStyleSheet(btnStyle);
    m_deleteBtn->setToolTip("Remove panel");
    m_deleteBtn->setCursor(Qt::ArrowCursor);
    m_deleteBtn->setFocusPolicy(Qt::NoFocus);

    connect(m_addBtn,    &QPushButton::clicked, this, [this]{ raise(); emit requestNewPanel(this); });
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]{ emit requestDelete(this); });
    connect(m_settingsBtn, &QPushButton::clicked, this, [this]{
        raise();
        auto *dlg       = new QDialog(this);
        auto *form      = new QFormLayout(dlg);
        auto *fontCombo = new QFontComboBox(dlg);
        auto *spin      = new QSpinBox(dlg);
        auto *buttons   = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);

        dlg->setWindowTitle("Panel Settings");
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setMinimumWidth(280);

        // Initialise controls from current panel settings
        fontCombo->setCurrentFont(QFont(m_fontFamily.isEmpty()
                                        ? QApplication::font().family()
                                        : m_fontFamily));
        spin->setRange(8, 24);
        spin->setValue(m_fontSize);
        spin->setSuffix(" px");

        auto *triggerSpin = new QSpinBox(dlg);
        triggerSpin->setRange(0, 1000);
        triggerSpin->setValue(m_triggerZone);
        triggerSpin->setSuffix(" ms");
        triggerSpin->setToolTip("How long (ms) the cursor must hover on the edge\n"
                                "strip before the panel slides in.\n"
                                "0 = instant, 500 = deliberate hover required.");

        // --- Opacity slider ---
        auto *opacitySlider = new QSlider(Qt::Horizontal, dlg);
        opacitySlider->setRange(30, 255);
        opacitySlider->setValue(m_bgColor.alpha());
        auto *opacityLabel = new QLabel(QString::number(m_bgColor.alpha()), dlg);
        auto *opacityRow   = new QHBoxLayout();
        opacityRow->addWidget(opacitySlider);
        opacityRow->addWidget(opacityLabel);
        connect(opacitySlider, &QSlider::valueChanged, this, [this, opacityLabel](int v){
            opacityLabel->setText(QString::number(v));
            setOpacity(v);
        });

        // --- Icon size slider ---
        auto *iconSlider = new QSlider(Qt::Horizontal, dlg);
        iconSlider->setRange(24, 256);
        iconSlider->setValue(m_iconSize);
        auto *iconLabel = new QLabel(QString::number(m_iconSize) + " px", dlg);
        auto *iconRow   = new QHBoxLayout();
        iconRow->addWidget(iconSlider);
        iconRow->addWidget(iconLabel);
        connect(iconSlider, &QSlider::valueChanged, this, [this, iconLabel](int v){
            iconLabel->setText(QString::number(v) + " px");
            setIconScale(v);
        });

        form->addRow("Font:",          fontCombo);
        form->addRow("Font size:",     spin);
        form->addRow("Icon size:",     iconRow);
        form->addRow("Opacity:",       opacityRow);
        form->addRow("Hover delay:",   triggerSpin);

#ifdef Q_OS_WIN
        auto *startupChk = new QCheckBox("Start with Windows", dlg);
        startupChk->setChecked(startupEnabled());
        form->addRow(startupChk);
#endif

        form->addRow(buttons);

        const int     prevSize    = m_fontSize;
        const QString prevFamily  = m_fontFamily;
        const int     prevTrigger = m_triggerZone;
        const int     prevOpacity = m_bgColor.alpha();
        const int     prevIcon    = m_iconSize;

        // Live preview as user browses fonts / adjusts size
        connect(fontCombo, &QFontComboBox::currentFontChanged, this,
                [this](const QFont &f){ setFontFamily(f.family()); });
        connect(spin, &QSpinBox::valueChanged, this, &SleekPanel::setFontSize);

        connect(buttons, &QDialogButtonBox::accepted, this, [this, dlg, triggerSpin
#ifdef Q_OS_WIN
            , startupChk
#endif
        ]{
            m_triggerZone = triggerSpin->value();
            if (m_expandDelay) m_expandDelay->setInterval(qMax(0, m_triggerZone));
#ifdef Q_OS_WIN
            setStartupEnabled(startupChk->isChecked());
#endif
            dlg->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this,
                [this, prevSize, prevFamily, prevTrigger, prevOpacity, prevIcon, dlg]{
                    setFontSize(prevSize);
                    setFontFamily(prevFamily);
                    setOpacity(prevOpacity);
                    setIconScale(prevIcon);
                    m_triggerZone = prevTrigger;
                    dlg->reject();
                });
        dlg->exec();
    });

    // --- Search / filter bar ---
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Filter...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setFixedHeight(SEARCH_HEIGHT);
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "  background: rgba(255,255,255,20); color: #ccc;"
        "  border: 1px solid rgba(255,255,255,30); border-radius: 4px;"
        "  padding: 0 4px; font-size: 11px;"
        "}"
        "QLineEdit:focus { border: 1px solid rgba(100,150,255,120); }"
    );
    m_searchBox->hide();  // shown when items exceed a threshold or via Ctrl+F
    connect(m_searchBox, &QLineEdit::textChanged, this, &SleekPanel::applyFilter);

    // Scroll area for items
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollBar:vertical {"
        "  background: rgba(255,255,255,15);"
        "  width: 8px;"
        "  margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,60);"
        "  min-height: 20px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    m_contentWidget = new QWidget();
    m_contentWidget->setStyleSheet("background: transparent;");
    m_flowLayout = new FlowLayout(m_contentWidget, 6, 6, 6);
    m_contentWidget->setLayout(m_flowLayout);
    m_scrollArea->setWidget(m_contentWidget);

    relayout();
}

// ---------------------------------------------------------------------------
// Property setters
// ---------------------------------------------------------------------------

void SleekPanel::setFontSize(int px)
{
    m_fontSize = px;
    for (auto *item : m_items)
        item->setFontSize(px);
    emit changed();
}

void SleekPanel::setFontFamily(const QString &family)
{
    m_fontFamily = family;
    for (auto *item : m_items)
        item->setFontFamily(family);
    emit changed();
}

void SleekPanel::setOpacity(int alpha)
{
    alpha = qBound(30, alpha, 255);
    m_bgColor.setAlpha(alpha);
    m_titleColor.setAlpha(qMin(alpha + 20, 255));
    update();
    emit changed();
}

void SleekPanel::setIconScale(int size)
{
    m_iconSize = qBound(24, size, 256);
    for (auto *item : m_items)
        item->setIconSize(m_iconSize);
    m_contentWidget->updateGeometry();
    emit changed();
}

void SleekPanel::toggleVisibility()
{
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
    }
}

void SleekPanel::setTitle(const QString& title)
{
    m_title = title;
    m_titleLabel->setText(title);
    emit titleChanged(title);
    emit changed();
    update();
}

// ---------------------------------------------------------------------------
// Search / filter
// ---------------------------------------------------------------------------

void SleekPanel::applyFilter(const QString &text)
{
    const QString filter = text.trimmed().toLower();
    for (auto *item : m_items) {
        if (filter.isEmpty()) {
            item->show();
        } else {
            item->setVisible(item->displayName().toLower().contains(filter));
        }
    }
    m_contentWidget->updateGeometry();
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

void SleekPanel::addFile(const QString& filePath)
{
    // Avoid duplicates
    for (auto* item : m_items) {
        if (item->filePath() == filePath)
            return;
    }
    auto* item = new SleekItem(filePath, m_contentWidget);
    connect(item, &SleekItem::activated, this, [](const QString& path) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(item, &SleekItem::requestRemove, this, [this](SleekItem *it) {
        removeFile(it->filePath());
    });
    connect(item, &SleekItem::requestReorder, this, [this](SleekItem *dragged, SleekItem *target) {
        const int from = m_items.indexOf(dragged);
        const int to   = m_items.indexOf(target);
        if (from < 0 || to < 0 || from == to) return;
        m_items.move(from, to);
        rebuildLayout();
        emit changed();
    });
    // Always apply current panel font & icon size so items match the panel style
    item->setFontSize(m_fontSize);
    item->setIconSize(m_iconSize);
    if (!m_fontFamily.isEmpty())
        item->setFontFamily(m_fontFamily);
    m_flowLayout->addWidget(item);
    m_items.append(item);
    emit changed();
}

void SleekPanel::removeFile(const QString& filePath)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]->filePath() == filePath) {
            auto* item = m_items.takeAt(i);
            m_flowLayout->removeWidget(item);
            item->deleteLater();

            if (m_items.size() < 8 && m_searchBox) {
                m_searchBox->clear();
                m_searchBox->hide();
            }

            emit changed();
            return;
        }
    }
}

void SleekPanel::reloadFromPath()
{
    if (m_watchPath.isEmpty()) return;

    // Clear existing
    qDeleteAll(m_items);
    m_items.clear();

    QDir dir(m_watchPath);
    if (!dir.exists()) return;

    // List files and subdirectories
    auto entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase
    );
    for (const auto& fi : entries) {
        addFile(fi.absoluteFilePath());
    }

    if (m_items.size() >= 8 && m_searchBox) {
        m_searchBox->show();
        relayout();
    }
}

void SleekPanel::rebuildLayout()
{
    // Detach all layout items (does NOT delete the widgets)
    while (m_flowLayout->count())
        delete m_flowLayout->takeAt(0);
    // Re-add in the current m_items order
    for (auto *item : m_items)
        m_flowLayout->addWidget(item);
    m_contentWidget->updateGeometry();
}

// --- Docking ---

void SleekPanel::setDockSide(DockSide side)
{
    if (m_dockSide == side) return;

    // Stop any in-flight animation
    if (m_slideAnim) m_slideAnim->stop();

    if (side == DockSide::None) {
        // Undock: restore pre-dock geometry and constraints
        if (m_expandDelay) m_expandDelay->stop();
        if (m_edgePoll) m_edgePoll->stop();
        setMinimumSize(MIN_W, MIN_H);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        setGeometry(m_preDockGeom);
        m_dockSide     = DockSide::None;
        m_dockExpanded = false;
        emit changed();
        return;
    }

    // Save current free-floating geometry before first dock
    if (m_dockSide == DockSide::None)
        m_preDockGeom = geometry();

    m_dockSide     = side;
    m_dockExpanded = false;

    // Expand-delay timer: duration = m_triggerZone ms.
    // Triggered by enterEvent — no cursor polling needed.
    if (!m_expandDelay) {
        m_expandDelay = new QTimer(this);
        m_expandDelay->setSingleShot(true);
        connect(m_expandDelay, &QTimer::timeout, this, &SleekPanel::slideIn);
    }
    m_expandDelay->setInterval(qMax(0, m_triggerZone));

    // Poll cursor position so docked panels work even when another window
    // (e.g. Chrome) covers the screen edge and blocks enterEvent.
    if (!m_edgePoll) {
        m_edgePoll = new QTimer(this);
        m_edgePoll->setInterval(80);
        connect(m_edgePoll, &QTimer::timeout, this, &SleekPanel::checkEdgeProximity);
    }
    m_edgePoll->start();

    applyDock();
    emit changed();
}

void SleekPanel::applyDock()
{
    QScreen *screen = panelScreen();
    if (!screen) return;
    const QRect sg = screen->geometry();

    // Size the panel to fill the edge.
    switch (m_dockSide) {
    case DockSide::Left:
    case DockSide::Right:
        setMinimumWidth(MIN_W);
        setMaximumWidth(sg.width() / 2);
        setFixedHeight(sg.height());
        resize(m_preDockGeom.width(), sg.height());
        break;
    case DockSide::Top:
    case DockSide::Bottom: {
        setFixedWidth(sg.width());
        const int contentW = sg.width() - 8;
        const int contentH = m_flowLayout->heightForWidth(contentW);
        const int needed   = TITLE_HEIGHT + 8 + qMax(contentH, 50);
        setFixedHeight(qMin(needed, sg.height() / 3));
        break;
    }
    default:
        break;
    }

    // Snap to hidden position — always a fixed SLIVER px strip at the edge
    switch (m_dockSide) {
    case DockSide::Left:
        move(sg.left() - (width() - SLIVER), sg.top());
        break;
    case DockSide::Right:
        move(sg.right() - SLIVER_RIGHT, sg.top());
        break;
    case DockSide::Top:
        move(sg.left(), sg.top() - (height() - SLIVER));
        break;
    case DockSide::Bottom:
        move(sg.left(), sg.bottom() - SLIVER);
        break;
    default:
        break;
    }
    m_dockExpanded = false;
}

void SleekPanel::slideIn()
{
    m_dockExpanded = true;
    m_slideInTime.start();
    QScreen *screen = panelScreen();
    if (!screen) return;
    const QRect sg = screen->geometry();

    QPoint target;
    switch (m_dockSide) {
    case DockSide::Left:   target = { sg.left(),             sg.top() };  break;
    case DockSide::Right:  target = { sg.right() - width(),  sg.top() };  break;
    case DockSide::Top:    target = { sg.left(),             sg.top() };  break;
    case DockSide::Bottom: target = { sg.left(), sg.bottom() - height()}; break;
    default: return;
    }

    if (!m_slideAnim) {
        m_slideAnim = new QPropertyAnimation(this, "pos", this);
        m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_slideAnim->stop();
    m_slideAnim->setDuration(180);
    m_slideAnim->setStartValue(pos());
    m_slideAnim->setEndValue(target);
    m_slideAnim->start();
}

void SleekPanel::slideOut()
{
    m_dockExpanded = false;
    QScreen *screen = panelScreen();
    if (!screen) return;
    const QRect sg = screen->geometry();

    QPoint target;
    switch (m_dockSide) {
    case DockSide::Left:   target = { sg.left() - (width() - SLIVER),   sg.top() }; break;
    case DockSide::Right:  target = { sg.right() - SLIVER_RIGHT,        sg.top() }; break;
    case DockSide::Top:    target = { sg.left(), sg.top() - (height() - SLIVER) };   break;
    case DockSide::Bottom: target = { sg.left(), sg.bottom() - SLIVER };             break;
    default: return;
    }

    if (!m_slideAnim) {
        m_slideAnim = new QPropertyAnimation(this, "pos", this);
        m_slideAnim->setEasingCurve(QEasingCurve::InCubic);
    }
    m_slideAnim->stop();
    m_slideAnim->setDuration(180);
    m_slideAnim->setStartValue(pos());
    m_slideAnim->setEndValue(target);
    m_slideAnim->start();
}

bool SleekPanel::cursorOverThisPanel() const
{
    const QPoint g = QCursor::pos();
    if (!rect().contains(mapFromGlobal(g)))
        return false;
    // Geometry alone isn't enough: a sibling panel docked to a perpendicular
    // edge can sit on top of part of us. Confirm we're the widget actually
    // under the cursor so we don't stay pinned open beneath it.
    QWidget *hit = QApplication::widgetAt(g);
    return hit && (hit == this || isAncestorOf(hit));
}

bool SleekPanel::cursorAtDockEdge() const
{
    QScreen *screen = panelScreen();
    if (!screen) return false;
    const QRect sg = screen->geometry();
    const QPoint cur = QCursor::pos();

    // The trigger zone is a thin strip straddling the docked edge. It reaches
    // EDGE_HOTZONE inward (onto this panel's screen) and only EDGE_SEAM past the
    // edge. The small outward reach is what keeps the right edge responsive:
    // where a neighbouring monitor abuts the edge the cursor doesn't clamp, it
    // sails across, so it settles a few px onto the seam rather than inside our
    // screen — without the seam allowance the poll keeps missing it. The reach
    // is intentionally tiny so the panel does NOT trigger from deeper in the
    // other monitor. The perpendicular axis stays bounded to this screen's span.
    switch (m_dockSide) {
    case DockSide::Left:
        return (cur.x() <= sg.left() + EDGE_HOTZONE && cur.x() >= sg.left() - EDGE_SEAM)
            && (cur.y() >= sg.top() && cur.y() <= sg.bottom());
    case DockSide::Right:
        return (cur.x() >= sg.right() - EDGE_HOTZONE && cur.x() <= sg.right() + EDGE_SEAM)
            && (cur.y() >= sg.top() && cur.y() <= sg.bottom());
    case DockSide::Top:
        return (cur.y() <= sg.top() + EDGE_HOTZONE && cur.y() >= sg.top() - EDGE_SEAM)
            && (cur.x() >= sg.left() && cur.x() <= sg.right());
    case DockSide::Bottom:
        return (cur.y() >= sg.bottom() - EDGE_HOTZONE && cur.y() <= sg.bottom() + EDGE_SEAM)
            && (cur.x() >= sg.left() && cur.x() <= sg.right());
    default:
        return false;
    }
}

void SleekPanel::checkEdgeProximity()
{
    if (m_dockSide == DockSide::None)
        return;

    const bool atEdge = cursorAtDockEdge();

    if (!m_dockExpanded) {
        if (atEdge) {
            // Remember we're at the edge so brief dropouts don't reset the
            // pending open below.
            m_edgeSeen.restart();
            if (m_dockSide == DockSide::Right) {
                // The right edge is shared with the monitors to the right, so
                // the cursor never clamps there to "hold" a hover delay the way
                // the clamping left/top/bottom edges do. Open it instantly.
                if (m_expandDelay) m_expandDelay->stop();
                slideIn();
            } else if (m_expandDelay) {
                // enterEvent is the preferred trigger because it honours the
                // configured hover delay, but it can't be relied on everywhere
                // (e.g. the sliver sitting behind desktop icons). Drive the same
                // timer from the poll so it still opens; the !isActive() guard
                // keeps enterEvent and the poll from double-triggering, and keeps
                // the timer running across flicker instead of restarting it.
                if (!m_expandDelay->isActive())
                    m_expandDelay->start();
            } else {
                slideIn();
            }
        } else if (m_expandDelay && m_expandDelay->isActive()
                   && (!m_edgeSeen.isValid() || m_edgeSeen.elapsed() > EDGE_GRACE_MS)) {
            // Only cancel the pending open once the cursor has been away from
            // the edge for EDGE_GRACE_MS. On an edge shared with another monitor
            // the cursor doesn't clamp — it constantly skips a pixel or two onto
            // the neighbour and back — so cancelling on the first !atEdge poll
            // would keep resetting the timer and make the panel take seconds to
            // open. The grace lets the hover delay accumulate through that
            // jitter, matching how the left/top edges (which clamp) behave.
            m_expandDelay->stop();
        }
    } else {
        // Slide out once the cursor has left BOTH the panel itself and the edge
        // trigger zone. The !atEdge gate is essential on multi-monitor setups:
        // a monitor sitting beyond the docked edge shares that edge's coordinate
        // range, so without it the panel would slide out the moment the cursor
        // isn't over the panel and immediately re-trigger because it's still
        // "at the edge" — an open/close flicker loop across the whole second
        // monitor. cursorOverThisPanel() is a real hit test (not just geometry),
        // so the panel still slides out when an overlapping sibling — e.g. a
        // top-docked panel over our top region — is what the cursor is on.
        if (!atEdge && !cursorOverThisPanel()) {
            const qint64 remaining = 500 - m_slideInTime.elapsed();
            if (remaining <= 0) {
                slideOut();
            } else if (!m_slideOutDelay || !m_slideOutDelay->isActive()) {
                if (!m_slideOutDelay) {
                    m_slideOutDelay = new QTimer(this);
                    m_slideOutDelay->setSingleShot(true);
                    connect(m_slideOutDelay, &QTimer::timeout, this, [this]{
                        if (m_dockExpanded && !cursorAtDockEdge() && !cursorOverThisPanel())
                            slideOut();
                    });
                }
                m_slideOutDelay->start(remaining);
            }
        }
    }
}

void SleekPanel::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (m_dockSide != DockSide::None) {
        if (m_slideOutDelay) m_slideOutDelay->stop();
        if (!m_dockExpanded && m_expandDelay && !m_expandDelay->isActive())
            m_expandDelay->start();
    }
}

void SleekPanel::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_dockSide != DockSide::None) {
        // On Windows, WM_MOUSELEAVE fires when the cursor moves onto a child
        // widget's HWND, even though we're still within the panel's bounding box.
        // Guard: only suppress if the cursor is genuinely over this panel. A
        // plain rect().contains() check is wrong for docked panels that span
        // the full screen edge — the cursor can be within bounds but over a
        // sibling panel that sits on top.
        if (cursorOverThisPanel())
            return;

        if (m_expandDelay) m_expandDelay->stop();
        if (m_dockExpanded) {
            const qint64 remaining = 500 - m_slideInTime.elapsed();
            if (remaining <= 0) {
                slideOut();
            } else {
                if (!m_slideOutDelay) {
                    m_slideOutDelay = new QTimer(this);
                    m_slideOutDelay->setSingleShot(true);
                    connect(m_slideOutDelay, &QTimer::timeout, this, [this]{
                        if (m_dockExpanded && !cursorOverThisPanel())
                            slideOut();
                    });
                }
                if (!m_slideOutDelay->isActive())
                    m_slideOutDelay->start(remaining);
            }
        }
    }
}

// --- Serialization ---

QJsonObject SleekPanel::toJson() const
{
    QJsonObject obj;
    obj["title"]     = m_title;
    obj["watchPath"] = m_watchPath;
    obj["collapsed"] = m_collapsed;
    obj["dockSide"]    = static_cast<int>(m_dockSide);
    obj["fontSize"]    = m_fontSize;
    obj["iconSize"]    = m_iconSize;
    obj["opacity"]     = m_bgColor.alpha();
    obj["triggerZone"] = m_triggerZone;
    if (!m_fontFamily.isEmpty())
        obj["fontFamily"] = m_fontFamily;

    // When docked, save the pre-dock geometry so it can be restored on undock
    if (m_dockSide != DockSide::None) {
        obj["x"] = m_preDockGeom.x();
        obj["y"] = m_preDockGeom.y();
        obj["w"] = m_preDockGeom.width();
        obj["h"] = m_preDockGeom.height();
    } else {
        obj["x"] = pos().x();
        obj["y"] = pos().y();
        obj["w"] = width();
        obj["h"] = height();
    }

    // If no watch path, store individual file paths
    if (m_watchPath.isEmpty()) {
        QJsonArray files;
        for (auto* item : m_items)
            files.append(item->filePath());
        obj["files"] = files;
    }
    return obj;
}

SleekPanel* SleekPanel::fromJson(const QJsonObject& obj, QWidget* parent)
{
    QString title = obj["title"].toString("Untitled");
    QString watchPath = obj["watchPath"].toString();
    auto* panel = new SleekPanel(title, watchPath, parent);

    int x = obj["x"].toInt(100);
    int y = obj["y"].toInt(100);
    int w = obj["w"].toInt(300);
    int h = obj["h"].toInt(260);

    // Clamp to the nearest screen so panels saved off-screen come back into view
    QPoint center(x + w / 2, y + h / 2);
    QScreen *screen = nullptr;
    for (QScreen *s : QApplication::screens()) {
        if (s->geometry().contains(center)) {
            screen = s;
            break;
        }
    }
    if (!screen) screen = QApplication::primaryScreen();
    if (screen) {
        QRect sg = screen->geometry();
        x = qBound(sg.left(), x, sg.right()  - 80);
        y = qBound(sg.top(),  y, sg.bottom() - TITLE_HEIGHT);
    }

    panel->setGeometry(x, y, w, h);

    // Apply font settings BEFORE adding items so every item is born with the
    // correct font — avoids a two-pass update and removes the DEFAULT_FONT guard.
    panel->m_fontSize   = obj["fontSize"].toInt(DEFAULT_FONT);
    panel->m_iconSize   = obj["iconSize"].toInt(DEFAULT_ICON);
    panel->m_fontFamily = obj["fontFamily"].toString();
    panel->m_triggerZone = obj["triggerZone"].toInt(DEFAULT_TRIGGER);

    // Restore opacity
    int alpha = obj["opacity"].toInt(180);
    panel->setOpacity(alpha);

    if (watchPath.isEmpty()) {
        QJsonArray files = obj["files"].toArray();
        for (const auto& f : files)
            panel->addFile(f.toString());   // addFile now picks up m_fontSize/m_fontFamily/m_iconSize
    } else {
        // Watch-path panels were already loaded in the constructor before
        // font/icon settings were applied — reload so items pick them up.
        panel->reloadFromPath();
    }

    // Show search bar after all items are loaded, then relayout so the
    // scroll area is positioned below it (avoids overlap on launch).
    if (panel->m_items.size() >= 8 && panel->m_searchBox) {
        panel->m_searchBox->show();
        panel->relayout();
    }

    if (obj["collapsed"].toBool(false)) {
        panel->m_collapsed = true;
        panel->setFixedHeight(TITLE_HEIGHT);
    }

    // Restore dock side — setDockSide saves the current geometry as preDockGeom,
    // which was already set above, so this is the right call order.
    int dockInt = obj["dockSide"].toInt(0);
    if (dockInt != 0)
        panel->setDockSide(static_cast<DockSide>(dockInt));

    return panel;
}

// --- Painting ---

void SleekPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.setBrush(m_bgColor);
    p.setPen(QPen(m_borderColor, 1));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);

    // Title bar background
    QPainterPath titlePath;
    QRectF titleRect(1, 1, width() - 2, TITLE_HEIGHT);
    titlePath.addRoundedRect(QRectF(1, 1, width() - 2, TITLE_HEIGHT + 8), 8, 8);
    // Clip off the bottom rounding
    titlePath.addRect(QRectF(1, TITLE_HEIGHT - 2, width() - 2, 10));
    p.setClipRect(QRectF(0, 0, width(), TITLE_HEIGHT));
    p.setBrush(m_titleColor);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(1, 1, width() - 2, TITLE_HEIGHT + 8), 8, 8);
    p.setClipping(false);

    // Collapse indicator
    QString arrow = m_collapsed ? QStringLiteral("▶") : QStringLiteral("▼");
    p.setPen(QColor(150, 150, 150));
    p.setFont(QFont("Segoe UI", 8));
    p.drawText(QRect(width() - 24, 0, 20, TITLE_HEIGHT), Qt::AlignCenter, arrow);
}

// --- Dragging & Resizing ---

QRect SleekPanel::titleBarRect() const
{
    // Draggable area stops before the buttons
    const int cfgX = width() - 24 - 2 - BTN_SIZE - 2 - BTN_SIZE - 2 - BTN_SIZE;
    return QRect(0, 0, cfgX - 4, TITLE_HEIGHT);
}

int SleekPanel::hitTestEdge(const QPoint& pos) const
{
    int edges = None;
    if (pos.x() <= EDGE_MARGIN)               edges |= Left;
    if (pos.x() >= width() - EDGE_MARGIN)      edges |= Right;
    if (pos.y() <= EDGE_MARGIN)                edges |= Top;
    if (pos.y() >= height() - EDGE_MARGIN)      edges |= Bottom;
    return edges;
}

void SleekPanel::updateCursorShape(const QPoint& pos)
{
    // When docked, only show resize cursor on the inward-facing edge
    if (m_dockSide != DockSide::None) {
        if (m_dockExpanded) {
            const int e = hitTestEdge(pos);
            if ((m_dockSide == DockSide::Left   && (e & Right)) ||
                (m_dockSide == DockSide::Right   && (e & Left))  ||
                (m_dockSide == DockSide::Top     && (e & Bottom))||
                (m_dockSide == DockSide::Bottom  && (e & Top)))
            {
                bool horiz = (m_dockSide == DockSide::Left || m_dockSide == DockSide::Right);
                setCursor(horiz ? Qt::SizeHorCursor : Qt::SizeVerCursor);
                return;
            }
        }
        setCursor(Qt::ArrowCursor);
        return;
    }

    int e = hitTestEdge(pos);
    if ((e & Left && e & Top) || (e & Right && e & Bottom))
        setCursor(Qt::SizeFDiagCursor);
    else if ((e & Right && e & Top) || (e & Left && e & Bottom))
        setCursor(Qt::SizeBDiagCursor);
    else if (e & (Left | Right))
        setCursor(Qt::SizeHorCursor);
    else if (e & (Top | Bottom))
        setCursor(Qt::SizeVerCursor);
    else if (titleBarRect().contains(pos))
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::ArrowCursor);
}

void SleekPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    raise(); // bring to front

    // When docked: allow collapse arrow + thickness-edge resize (no dragging)
    if (m_dockSide != DockSide::None) {
        // Collapse arrow
        if (QRect(width() - 24, 0, 20, TITLE_HEIGHT).contains(event->pos())) {
            m_collapsed = !m_collapsed;
            if (m_collapsed) {
                m_expandedHeight = height();
                m_scrollArea->hide();
                setFixedHeight(TITLE_HEIGHT);
            } else {
                setMinimumHeight(MIN_H);
                setMaximumHeight(QWIDGETSIZE_MAX);
                resize(width(), m_expandedHeight);
                m_scrollArea->show();
            }
            update();
            return;
        }

        // Allow resizing the inward-facing edge when the panel is expanded
        if (m_dockExpanded) {
            const int edges = hitTestEdge(event->pos());
            const bool validEdge =
                (m_dockSide == DockSide::Left   && (edges & Right))  ||
                (m_dockSide == DockSide::Right   && (edges & Left))   ||
                (m_dockSide == DockSide::Top     && (edges & Bottom)) ||
                (m_dockSide == DockSide::Bottom  && (edges & Top));
            if (validEdge) {
                m_resizing     = true;
                m_resizeEdges  = edges;
                m_resizeOrigin = event->globalPosition().toPoint();
                m_resizeStartGeom = geometry();
            }
        }
        return;
    }

    // Click the collapse arrow → toggle collapsed
    if (QRect(width() - 24, 0, 20, TITLE_HEIGHT).contains(event->pos())) {
        m_collapsed = !m_collapsed;
        if (m_collapsed) {
            m_expandedHeight = height();
            m_scrollArea->hide();
            setFixedHeight(TITLE_HEIGHT);
        } else {
            setMinimumHeight(MIN_H);
            setMaximumHeight(16777215);
            resize(width(), m_expandedHeight);
            m_scrollArea->show();
        }
        update();
        return;
    }

    int edges = hitTestEdge(event->pos());
    if (edges != None && !m_collapsed) {
        m_resizing = true;
        m_resizeEdges = edges;
        m_resizeOrigin = event->globalPosition().toPoint();
        m_resizeStartGeom = geometry();
        return;
    }

    if (titleBarRect().contains(event->pos())) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void SleekPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        move(mapToParent(event->pos() - m_dragOffset));
        return;
    }

    if (m_resizing) {
        QPoint delta = event->globalPosition().toPoint() - m_resizeOrigin;
        QRect g = m_resizeStartGeom;

        if (m_resizeEdges & Right)
            g.setRight(g.right() + delta.x());
        if (m_resizeEdges & Bottom)
            g.setBottom(g.bottom() + delta.y());
        if (m_resizeEdges & Left)
            g.setLeft(g.left() + delta.x());
        if (m_resizeEdges & Top)
            g.setTop(g.top() + delta.y());

        if (g.width() >= MIN_W && g.height() >= MIN_H)
            setGeometry(g);
        return;
    }

    updateCursorShape(event->pos());
}

void SleekPanel::mouseReleaseEvent(QMouseEvent* event)
{
    bool wasMoving = m_dragging || m_resizing;
    m_dragging = false;
    m_resizing = false;
    m_resizeEdges = None;

    // After a docked resize, persist the new thickness into preDockGeom
    if (wasMoving && m_dockSide != DockSide::None) {
        if (m_dockSide == DockSide::Left || m_dockSide == DockSide::Right)
            m_preDockGeom.setWidth(width());
        else
            m_preDockGeom.setHeight(height());
    }

    updateCursorShape(event->pos());
    QWidget::mouseReleaseEvent(event);
    if (wasMoving)
        emit changed();
}

void SleekPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    // Double-click the title label → rename
    if (m_titleLabel->geometry().contains(event->pos())) {
        bool ok;
        QString newTitle = QInputDialog::getText(this, "Rename Panel", "New name:",
                                                  QLineEdit::Normal, m_title, &ok);
        if (ok && !newTitle.isEmpty())
            setTitle(newTitle);
    }
}

void SleekPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();

    // For top/bottom-docked panels, re-fit height whenever the content changes
    if ((m_dockSide == DockSide::Top || m_dockSide == DockSide::Bottom) && !m_collapsed) {
        QScreen *screen = panelScreen();
        if (screen) {
            const int contentW = width() - 8;
            const int contentH = m_flowLayout->heightForWidth(contentW);
            const int needed   = TITLE_HEIGHT + 8 + qMax(contentH, 50);
            const int capped   = qMin(needed, screen->geometry().height() / 3);
            if (capped != height())
                setFixedHeight(capped);
        }
    }
}

void SleekPanel::relayout()
{
    // Right-to-left: [arrow 20px][2][x 16px][2][+ 16px][2][gear 16px][gap][title]
    const int bY   = (TITLE_HEIGHT - BTN_SIZE) / 2;
    const int delX = width() - 24 - 2 - BTN_SIZE;    // x
    const int addX = delX - 2 - BTN_SIZE;             // +
    const int cfgX = addX - 2 - BTN_SIZE;             // gear

    m_deleteBtn  ->setGeometry(delX, bY, BTN_SIZE, BTN_SIZE);
    m_addBtn     ->setGeometry(addX, bY, BTN_SIZE, BTN_SIZE);
    m_settingsBtn->setGeometry(cfgX, bY, BTN_SIZE, BTN_SIZE);

    // Title-bar content runs from the left edge to just before the buttons.
    const int titleLeft  = 10;
    const int titleRight = cfgX - 4;                 // small gap before the gear
    const int headerW    = qMax(0, titleRight - titleLeft);

    // The search bar shares the title row only when there's room for both a
    // readable folder name and a usable filter box side by side. Otherwise it
    // drops onto its own row beneath the title bar so the two never overlap.
    // Use !isHidden() instead of isVisible() because isVisible() returns false
    // while the parent widget isn't shown yet (during fromJson loading).
    constexpr int MIN_TITLE_W  = 48;   // keep the folder name legible inline
    constexpr int MIN_SEARCH_W = 110;  // keep the filter box usable inline
    constexpr int INLINE_GAP   = 6;

    const bool wantSearch = m_searchBox && !m_searchBox->isHidden();
    const bool inlineSearch =
        wantSearch && headerW >= MIN_TITLE_W + INLINE_GAP + MIN_SEARCH_W;

    int contentTop = TITLE_HEIGHT + 2;

    if (inlineSearch) {
        const int sW = qMin(180, headerW - MIN_TITLE_W - INLINE_GAP);
        const int sX = titleRight - sW;
        const int sY = (TITLE_HEIGHT - SEARCH_HEIGHT) / 2;
        m_searchBox->setGeometry(sX, sY, sW, SEARCH_HEIGHT);
        m_titleLabel->setGeometry(titleLeft, 0,
                                  qMax(0, sX - INLINE_GAP - titleLeft), TITLE_HEIGHT);
    } else {
        m_titleLabel->setGeometry(titleLeft, 0, headerW, TITLE_HEIGHT);

        if (wantSearch) {
            // Stacked beneath the title — only if there's vertical room for it
            // plus a minimum content area, else hide it rather than swallow
            // the whole panel.
            const int minForSearch = TITLE_HEIGHT + SEARCH_HEIGHT + 4 + MIN_H;
            if (height() < minForSearch) {
                m_searchBox->hide();
            } else {
                m_searchBox->setGeometry(4, contentTop, width() - 8, SEARCH_HEIGHT);
                contentTop += SEARCH_HEIGHT + 2;
            }
        }
    }

    m_scrollArea->setGeometry(4, contentTop, width() - 8, height() - contentTop - 4);
}

// --- Context Menu ---

void SleekPanel::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    QAction* renameAct   = menu.addAction("Rename Panel...");
    QAction* reloadAct   = menu.addAction("Reload");
    menu.addSeparator();
    QAction* collapseAct = menu.addAction(m_collapsed ? "Expand" : "Collapse");

    // Toggle search bar
    QAction *searchAct = menu.addAction(m_searchBox->isVisible() ? "Hide Filter" : "Show Filter");

    menu.addSeparator();

    // Dock to edge submenu
    QMenu *dockMenu    = menu.addMenu("Dock to edge");
    QAction *dockNone  = dockMenu->addAction("Free floating");
    dockMenu->addSeparator();
    QAction *dockLeft   = dockMenu->addAction("Left");
    QAction *dockTop    = dockMenu->addAction("Top");
    QAction *dockRight  = dockMenu->addAction("Right");
    QAction *dockBottom = dockMenu->addAction("Bottom");

    const auto markCurrent = [&](QAction *act, DockSide side) {
        act->setCheckable(true);
        act->setChecked(m_dockSide == side);
    };
    markCurrent(dockNone,   DockSide::None);
    markCurrent(dockLeft,   DockSide::Left);
    markCurrent(dockTop,    DockSide::Top);
    markCurrent(dockRight,  DockSide::Right);
    markCurrent(dockBottom, DockSide::Bottom);

    menu.addSeparator();
    QAction* deleteAct = menu.addAction("Remove Panel");

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen) return;

    if      (chosen == renameAct)  {
        bool ok;
        QString newTitle = QInputDialog::getText(this, "Rename Panel", "Title:",
            QLineEdit::Normal, m_title, &ok);
        if (ok && !newTitle.isEmpty()) setTitle(newTitle);
    }
    else if (chosen == reloadAct)   { reloadFromPath(); }
    else if (chosen == collapseAct) {
        QMouseEvent fakeEvent(QEvent::MouseButtonDblClick, QPointF(5, 5),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        mouseDoubleClickEvent(&fakeEvent);
    }
    else if (chosen == searchAct) {
        m_searchBox->setVisible(!m_searchBox->isVisible());
        if (!m_searchBox->isVisible()) m_searchBox->clear();
        relayout();
    }
    else if (chosen == dockNone)    { setDockSide(DockSide::None); }
    else if (chosen == dockLeft)    { setDockSide(DockSide::Left); }
    else if (chosen == dockTop)     { setDockSide(DockSide::Top); }
    else if (chosen == dockRight)   { setDockSide(DockSide::Right); }
    else if (chosen == dockBottom)  { setDockSide(DockSide::Bottom); }
    else if (chosen == deleteAct)   { emit requestDelete(this); }
}

// --- Drag & Drop ---

void SleekPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() ||
        event->mimeData()->hasFormat("application/x-sleek-item")) {
        event->acceptProposedAction();
    }
}

void SleekPanel::dropEvent(QDropEvent* event)
{
    // Internal panel move
    if (event->mimeData()->hasFormat("application/x-sleek-item")) {
        QString path = QString::fromUtf8(event->mimeData()->data("application/x-sleek-item"));
        addFile(path);
        event->acceptProposedAction();
        return;
    }

    // External file drop
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile())
                addFile(url.toLocalFile());
        }
        event->acceptProposedAction();
    }
}


// =========================================================================
// FlowLayout (icon wrapping layout)
// =========================================================================

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size())
        return m_items.takeAt(index);
    return nullptr;
}

int FlowLayout::horizontalSpacing() const
{
    return m_hSpace >= 0 ? m_hSpace : 6;
}

int FlowLayout::verticalSpacing() const
{
    return m_vSpace >= 0 ? m_vSpace : 6;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const auto* item : m_items)
        size = size.expandedTo(item->minimumSize());
    auto m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effective = rect.adjusted(left, top, -right, -bottom);

    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;

    for (auto* item : m_items) {
        int spaceX = horizontalSpacing();
        int spaceY = verticalSpacing();

        QSize itemSize = item->sizeHint();
        int nextX = x + itemSize.width() + spaceX;

        if (nextX - spaceX > effective.right() && lineHeight > 0) {
            x = effective.x();
            y += lineHeight + spaceY;
            nextX = x + itemSize.width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), itemSize));

        x = nextX;
        lineHeight = qMax(lineHeight, itemSize.height());
    }

    return y + lineHeight - rect.y() + bottom;
}
