#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QTimer>
#include <qrect.h>

class SleekPanel;
class QWidget;

// Manages the collection of panels: creation, deletion, persistence.
class SleekManager : public QObject
{
    Q_OBJECT

public:
    explicit SleekManager(QWidget *parentWidget, QObject *parent = nullptr);
    ~SleekManager() override;

    SleekPanel *createPanel(const QString &title,
                             const QString &watchPath = QString(),
                             const QRect &geometry = QRect());

    void removePanel(SleekPanel *panel);

    void saveToFile(const QString &path) const;
    void loadFromFile(const QString &path);

    // Call once after construction so the manager can auto-save on changes
    void setSavePath(const QString &path);

    void createDefaults();

    const QList<SleekPanel *> &panels() const { return m_panels; }

private:
    void connectPanel(SleekPanel *panel);
    void scheduleSave();           // debounced: saves 1 s after last change

    QWidget *m_parentWidget = nullptr;
    QList<SleekPanel *> m_panels;
    QString  m_savePath;
    QTimer  *m_saveTimer = nullptr;
};
