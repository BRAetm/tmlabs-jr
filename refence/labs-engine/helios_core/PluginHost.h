#pragma once
// PluginHost.h — loads, authenticates and manages plugins

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include "IPlugin.h"

namespace Helios {

class LicenseService;
class LoggingService;

class PluginHost : public QObject {
    Q_OBJECT

public:
    explicit PluginHost(QObject* parent = nullptr);
    ~PluginHost() override;

    // Load a plugin DLL by filename (relative to plugins/ dir)
    bool    loadPlugin(const QString& dllPath);
    void    unloadPlugin(const QString& name);
    IPlugin* plugin(const QString& name) const;
    QList<IPlugin*> plugins() const;

    // Called by main window to get dock widgets
    QList<IUIPlugin*> uiPlugins() const;

    // Settings: [PluginPaths] section of helios_settings.ini
    void    loadFromSettings();
    void    saveToSettings();

signals:
    // Fired when premium plugin needs auth
    // "Plugin %1 requires authentication. Please login."
    void authenticationRequired(const QString& pluginName);

    // Fired during auth sequence
    // "Authenticating premium plugin %1..."
    void authenticationStarted(const QString& pluginName);
    void authenticationTimeout(const QString& pluginName);
    void authenticationFailed(const QString& pluginName);

    void pluginLoaded(const QString& name);
    void pluginUnloaded(const QString& name);
    void heliosLaunching();

private:
    // Checks IPlugin::requiresAuthentication()
    // If true, fires authenticationRequired and blocks until token valid
    bool authenticatePlugin(IPlugin* plugin);

    QMap<QString, IPlugin*>  m_plugins;
    QMap<QString, void*>     m_handles;   // HMODULE per DLL
    LicenseService*          m_license  = nullptr;
    LoggingService*          m_logging  = nullptr;
};

} // namespace Helios
