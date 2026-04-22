#include "SettingsManager.h"

#include <QDir>
#include <QStandardPaths>

namespace Labs {

SettingsManager::SettingsManager(QObject* parent) : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/settings.ini");
    m_settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
}

SettingsManager::~SettingsManager() = default;

QVariant SettingsManager::value(const QString& key, const QVariant& defaultValue) const
{
    return m_settings->value(key, defaultValue);
}

void SettingsManager::setValue(const QString& key, const QVariant& value)
{
    m_settings->setValue(key, value);
}

void SettingsManager::remove(const QString& key)  { m_settings->remove(key); }
bool SettingsManager::contains(const QString& key) const { return m_settings->contains(key); }
void SettingsManager::sync() { m_settings->sync(); }

QString SettingsManager::filePath() const { return m_settings->fileName(); }

} // namespace Labs
