#include "PluginHost.h"
#include "IPlugin.h"

#include <QDir>
#include <QLibrary>

namespace Labs {

using CreatePluginFn = IPlugin* (*)();

PluginHost::PluginHost(QObject* parent) : QObject(parent) {}

PluginHost::~PluginHost() { unloadAll(); }

int PluginHost::loadAll(const QString& pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) return 0;

    const auto entries = dir.entryInfoList({"*.dll"}, QDir::Files);
    int loaded = 0;
    for (const auto& info : entries) {
        QLibrary lib(info.absoluteFilePath());
        if (!lib.load()) {
            qWarning().nospace() << "PluginHost: failed to load " << info.fileName()
                                  << " — " << lib.errorString();
            continue;
        }

        auto fn = reinterpret_cast<CreatePluginFn>(lib.resolve("createPlugin"));
        if (!fn) {
            qWarning().nospace() << "PluginHost: " << info.fileName()
                                  << " has no createPlugin export";
            lib.unload();
            continue;
        }

        IPlugin* p = nullptr;
        try {
            p = fn();
        } catch (const std::exception& e) {
            qWarning().nospace() << "PluginHost: " << info.fileName()
                                  << " threw during createPlugin: " << e.what();
            continue;
        } catch (...) {
            qWarning().nospace() << "PluginHost: " << info.fileName()
                                  << " threw unknown exception during createPlugin";
            continue;
        }
        if (p) {
            m_plugins.push_back(p);
            ++loaded;
        }
    }
    return loaded;
}

void PluginHost::unloadAll()
{
    for (IPlugin* p : m_plugins) {
        if (p) { p->shutdown(); delete p; }
    }
    m_plugins.clear();
}

} // namespace Labs
