// SettingsManager.cpp

#include "SettingsManager.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

namespace Helios {

SettingsManager::SettingsManager(QObject* parent) : QObject(parent) {}

bool SettingsManager::load(const QString& path)
{
    QString settingsPath = path;
    if (settingsPath.isEmpty()) {
        // %APPDATA%\HeliosProject\Helios\helios_settings.ini
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        settingsPath = dir + "/helios_settings.ini";
    }
    delete m_settings;
    m_settings = new QSettings(settingsPath, QSettings::IniFormat, this);
    return m_settings->status() == QSettings::NoError;
}

void SettingsManager::save()
{
    if (m_settings) m_settings->sync();
}

QVariant SettingsManager::value(const QString& key, const QVariant& defaultValue) const
{
    if (!m_settings) return defaultValue;
    return m_settings->value(key, defaultValue);
}

void SettingsManager::setValue(const QString& key, const QVariant& value)
{
    if (!m_settings) return;
    m_settings->setValue(key, value);
    emit settingsChanged(key);
}

bool SettingsManager::contains(const QString& key) const
{
    return m_settings && m_settings->contains(key);
}

void SettingsManager::remove(const QString& key)
{
    if (m_settings) m_settings->remove(key);
}

QStringList SettingsManager::keys(const QString& section) const
{
    if (!m_settings) return {};
    if (!section.isEmpty()) {
        m_settings->beginGroup(section);
        QStringList k = m_settings->childKeys();
        m_settings->endGroup();
        return k;
    }
    return m_settings->allKeys();
}

} // namespace Helios
