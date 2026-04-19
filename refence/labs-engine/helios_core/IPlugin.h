#pragma once
// IPlugin.h — Helios plugin interface
// com.helios.IPlugin/1.0

#include <QString>
#include <QObject>

namespace Helios {

class LoggingService;
class SharedMemoryManager;
class LicenseService;
class SettingsManager;

struct PluginContext {
    LoggingService*      logging;
    SharedMemoryManager* sharedMemory;
    LicenseService*      license;
    SettingsManager*     settings;
};

class IPlugin {
public:
    IPlugin() = default;
    IPlugin(const IPlugin&) = default;
    virtual ~IPlugin() = default;

    virtual QString     name()        const = 0;
    virtual QString     author()      const = 0;
    virtual QString     description() const = 0;
    virtual QString     version()     const = 0;

    // Set to true if plugin requires Helios auth
    virtual bool        requiresAuthentication() const { return false; }

    virtual void        initialize(const PluginContext& ctx) = 0;
    virtual void        shutdown() = 0;
    virtual QObject*    qobject() = 0;
};

class IUIPlugin : public IPlugin {
public:
    IUIPlugin() = default;
    IUIPlugin(const IUIPlugin&) = default;
    IUIPlugin(IUIPlugin&&) = default;
    virtual ~IUIPlugin() = default;

    // Return the dock widget for this plugin
    virtual QWidget*    createWidget(QWidget* parent) = 0;
};

// Plugin factory export — every plugin DLL must export this
// extern "C" __declspec(dllexport) IPlugin* createPlugin();

} // namespace Helios
