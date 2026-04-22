#include "WgcCapturePlugin.h"

namespace Labs {

WgcCapturePlugin::WgcCapturePlugin()
    : m_source(std::make_unique<WgcSource>(this)) {}

WgcCapturePlugin::~WgcCapturePlugin() = default;

void WgcCapturePlugin::initialize(const PluginContext& ctx)
{
    if (m_source) m_source->setSettings(ctx.settings);
}

void WgcCapturePlugin::shutdown()
{
    if (m_source) m_source->stop();
}

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::WgcCapturePlugin();
}
