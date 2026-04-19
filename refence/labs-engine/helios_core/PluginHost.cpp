// PluginHost.cpp — loads, authenticates and manages plugins

#include "PluginHost.h"
#include "LicenseService.h"
#include "LoggingService.h"
#include "SettingsManager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QCoreApplication>
#include <QDir>

namespace Helios {

typedef IPlugin* (*CreatePluginFn)();

PluginHost::PluginHost(QObject* parent) : QObject(parent) {}

PluginHost::~PluginHost()
{
    // Unload all in reverse order
    QStringList names = m_plugins.keys();
    std::reverse(names.begin(), names.end());
    for (const QString& name : names)
        unloadPlugin(name);
}

bool PluginHost::loadPlugin(const QString& dllPath)
{
    // Resolve path relative to plugins/ dir
    QString fullPath = dllPath;
    if (!QFile::exists(fullPath)) {
        QString pluginsDir = QCoreApplication::applicationDirPath() + "/plugins/";
        fullPath = pluginsDir + dllPath;
    }

    if (m_logging)
        m_logging->info("PluginHost: loading " + fullPath);

    HMODULE hMod = LoadLibraryW(fullPath.toStdWString().c_str());
    if (!hMod) {
        if (m_logging)
            m_logging->error(QString("PluginHost: LoadLibrary failed for %1 (error %2)")
                             .arg(fullPath).arg(GetLastError()));
        return false;
    }

    auto createPlugin = reinterpret_cast<CreatePluginFn>(
        GetProcAddress(hMod, "createPlugin"));
    if (!createPlugin) {
        if (m_logging)
            m_logging->error("PluginHost: no createPlugin export in " + fullPath);
        FreeLibrary(hMod);
        return false;
    }

    IPlugin* plugin = createPlugin();
    if (!plugin) {
        FreeLibrary(hMod);
        return false;
    }

    // Authenticate if required
    if (plugin->requiresAuthentication()) {
        if (!authenticatePlugin(plugin)) {
            delete plugin;
            FreeLibrary(hMod);
            return false;
        }
    }

    m_plugins[plugin->name()] = plugin;
    m_handles[plugin->name()] = hMod;

    emit pluginLoaded(plugin->name());
    if (m_logging)
        m_logging->info("PluginHost: loaded plugin '" + plugin->name() + "'");

    return true;
}

void PluginHost::unloadPlugin(const QString& name)
{
    IPlugin* plugin = m_plugins.take(name);
    if (!plugin) return;

    plugin->shutdown();
    delete plugin;

    HMODULE hMod = reinterpret_cast<HMODULE>(m_handles.take(name));
    if (hMod) FreeLibrary(hMod);

    emit pluginUnloaded(name);
}

IPlugin* PluginHost::plugin(const QString& name) const
{
    return m_plugins.value(name, nullptr);
}

QList<IPlugin*> PluginHost::plugins() const
{
    return m_plugins.values();
}

QList<IUIPlugin*> PluginHost::uiPlugins() const
{
    QList<IUIPlugin*> result;
    for (IPlugin* p : m_plugins.values()) {
        if (auto* ui = dynamic_cast<IUIPlugin*>(p))
            result.append(ui);
    }
    return result;
}

void PluginHost::loadFromSettings()
{
    // [PluginPaths] section of helios_settings.ini
    // Each key is a plugin filename, value = 1 (enabled) or 0 (disabled)
    // Example:
    //   [PluginPaths]
    //   CvPython.dll=1
    //   DS5Input.dll=1
    //   ViGEmOutput.dll=1

    // Settings are injected via PluginContext; check if available
    emit heliosLaunching();
}

void PluginHost::saveToSettings()
{
    // Write currently loaded plugin names back to [PluginPaths]
}

bool PluginHost::authenticatePlugin(IPlugin* plugin)
{
    if (!m_license) {
        emit authenticationRequired(plugin->name());
        return false;
    }

    emit authenticationStarted(plugin->name());

    if (!m_license->isAuthenticated()) {
        // Block up to 30s for auth to complete
        emit authenticationRequired(
            QString("Plugin %1 requires authentication. Please login.").arg(plugin->name()));

        // Wait for signal
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(m_license, &LicenseService::authenticationSucceeded,
                         &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(30000);
        loop.exec();

        if (!m_license->isAuthenticated()) {
            emit authenticationFailed(plugin->name());
            return false;
        }
    }

    if (m_logging)
        m_logging->info(
            QString("Authenticating premium plugin %1...").arg(plugin->name()));

    return true;
}

} // namespace Helios
