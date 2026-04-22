#pragma once

#include "LabsCore.h"
#include "FrameTypes.h"
#include "InputTypes.h"

#include <QObject>
#include <QString>

class QWidget;

namespace Labs {

class SettingsManager;

struct PluginContext {
    SettingsManager* settings = nullptr;
};

class LABSCORE_API IPlugin {
public:
    IPlugin() = default;
    virtual ~IPlugin() = default;

    virtual QString name()        const = 0;
    virtual QString author()      const = 0;
    virtual QString description() const = 0;
    virtual QString version()     const = 0;

    virtual void initialize(const PluginContext& ctx) = 0;
    virtual void shutdown() = 0;

    virtual QObject* qobject() = 0;
};

class LABSCORE_API IUIPlugin : public virtual IPlugin {
public:
    ~IUIPlugin() override = default;
    virtual QWidget* createWidget(QWidget* parent) = 0;
};

class LABSCORE_API IFrameSourcePlugin : public virtual IPlugin {
public:
    ~IFrameSourcePlugin() override = default;
    virtual IFrameSource* frameSource() = 0;
};

class LABSCORE_API IFrameSinkPlugin : public virtual IPlugin {
public:
    ~IFrameSinkPlugin() override = default;
    virtual IFrameSink* frameSink() = 0;
};

class LABSCORE_API IControllerSourcePlugin : public virtual IPlugin {
public:
    ~IControllerSourcePlugin() override = default;
    virtual IControllerSource* controllerSource() = 0;
};

class LABSCORE_API IControllerSinkPlugin : public virtual IPlugin {
public:
    ~IControllerSinkPlugin() override = default;
    virtual IControllerSink* controllerSink() = 0;
};

// Opt-in — plugin presents its own pairing / setup UI. Engine offers a
// "Pair…" toolbar action that invokes this when the plugin supports it.
class LABSCORE_API IPairablePlugin : public virtual IPlugin {
public:
    ~IPairablePlugin() override = default;
    virtual bool pair(QWidget* parent) = 0;
};

} // namespace Labs

// Every plugin DLL must export:
//   extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin();
