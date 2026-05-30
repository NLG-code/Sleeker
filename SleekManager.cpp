#include "SleekManager.h"
#include "SleekPanel.h"

#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QStandardPaths>

SleekManager::SleekManager(QWidget *parentWidget, QObject *parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
{
}

SleekManager::~SleekManager()
{
    // Flush any pending save immediately on destruction
    if (m_saveTimer && m_saveTimer->isActive())
        saveToFile(m_savePath);
}

// ---------------------------------------------------------------------------
// Auto-save
// ---------------------------------------------------------------------------

void SleekManager::setSavePath(const QString &path)
{
    m_savePath = path;

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000); // 1 second after last change
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        saveToFile(m_savePath);
    });
}

void SleekManager::scheduleSave()
{
    if (m_saveTimer)
        m_saveTimer->start(); // restarts the 1 s countdown
}

// ---------------------------------------------------------------------------
// Panel wiring
// ---------------------------------------------------------------------------

void SleekManager::connectPanel(SleekPanel *panel)
{
    connect(panel, &SleekPanel::requestDelete, this, &SleekManager::removePanel);

    connect(panel, &SleekPanel::requestNewPanel, this, [this](SleekPanel *) {
        bool ok;
        QString title = QInputDialog::getText(m_parentWidget, "New Panel", "Panel name:",
                                               QLineEdit::Normal, "New Panel", &ok);
        if (!ok || title.isEmpty()) return;
        createPanel(title, QString());
    });

    // Save whenever panel content or position changes
    connect(panel, &SleekPanel::changed, this, &SleekManager::scheduleSave);
}

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------

SleekPanel *SleekManager::createPanel(const QString &title,
                                       const QString &watchPath,
                                       const QRect &geometry)
{
    auto *panel = new SleekPanel(title, watchPath, m_parentWidget);

    if (geometry.isValid()) {
        panel->setGeometry(geometry);
    } else {
        int offset = m_panels.size() * 40;
        panel->setGeometry(100 + offset, 100 + offset, 320, 280);
    }

    connectPanel(panel);
    panel->show();
    m_panels.append(panel);

    scheduleSave();
    return panel;
}

void SleekManager::removePanel(SleekPanel *panel)
{
    m_panels.removeAll(panel);
    panel->deleteLater();
    scheduleSave();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void SleekManager::saveToFile(const QString &path) const
{
    if (path.isEmpty()) return;

    QJsonArray arr;
    for (auto *panel : m_panels)
        arr.append(panel->toJson());

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.flush();
    }
}

void SleekManager::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return;

    for (const auto &val : doc.array()) {
        auto *panel = SleekPanel::fromJson(val.toObject(), m_parentWidget);
        connectPanel(panel);
        panel->show();
        m_panels.append(panel);
    }
}

void SleekManager::createDefaults()
{
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    createPanel("Desktop", desktop, QRect(60, 60, 360, 300));
}
