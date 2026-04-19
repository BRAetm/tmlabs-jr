#pragma once
// SettingsManager.h — helios_settings.ini wrapper

#include <QObject>
#include <QString>
#include <QVariant>

namespace Helios {

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject* parent = nullptr);
    ~SettingsManager() override = default;

    // Path: %APPDATA%\HeliosProject\Helios\helios_settings.ini
    bool load(const QString& path = QString());
    void save();

    QVariant value(const QString& key, const QVariant& defaultValue = {}) const;
    void     setValue(const QString& key, const QVariant& value);

    bool contains(const QString& key) const;
    void remove(const QString& key);

    // Section helpers ([PluginPaths], [General], [Theme], ...)
    QStringList keys(const QString& section = QString()) const;

signals:
    void settingsChanged(const QString& key);

private:
    class QSettings* m_settings = nullptr;
};

} // namespace Helios
