#pragma once

#include "LabsCore.h"

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <memory>

namespace Labs {

// Wraps a QSettings INI file at %APPDATA%/Labs/settings.ini.
// Plugins receive a SettingsManager* via PluginContext and prefix their
// own keys however they like (suggest "pluginName/key").
class LABSCORE_API SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject* parent = nullptr);
    ~SettingsManager() override;

    QVariant value(const QString& key, const QVariant& defaultValue = {}) const;
    void     setValue(const QString& key, const QVariant& value);
    void     remove(const QString& key);
    bool     contains(const QString& key) const;
    void     sync();

    QString  filePath() const;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace Labs
