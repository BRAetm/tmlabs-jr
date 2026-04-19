// XboxRemotePlay.cpp — Xbox cloud gaming remote play plugin

#include "XboxRemotePlay.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>

namespace Helios {

XboxRemotePlayPlugin::XboxRemotePlayPlugin(QObject* parent) : QObject(parent) {}
XboxRemotePlayPlugin::~XboxRemotePlayPlugin() { shutdown(); }

void XboxRemotePlayPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 0);
}

void XboxRemotePlayPlugin::shutdown()
{
    m_ringWriter.close();
    // Disconnect active stream session if any
    if (m_streamSession) {
        // xgamestreaming teardown
        m_streamSession = nullptr;
        setState(XboxRPState::Disconnected);
    }
}

QWidget* XboxRemotePlayPlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);

    m_lblState = new QLabel("Disconnected", w);
    m_lblState->setStyleSheet("font-weight:bold; color:#aaa;");
    lay->addWidget(m_lblState);

    auto* grp  = new QGroupBox("Xbox Cloud Gaming", w);
    auto* grid = new QGridLayout(grp);

    grid->addWidget(new QLabel("Title ID / Game ID:", w), 0, 0);
    m_edtGameId = new QLineEdit(w);
    m_edtGameId->setPlaceholderText("e.g. NBA 2K24");
    grid->addWidget(m_edtGameId, 0, 1);

    lay->addWidget(grp);

    auto* bar = new QHBoxLayout;
    m_btnConnect    = new QPushButton("Connect", w);
    m_btnDisconnect = new QPushButton("Disconnect", w);
    m_btnDisconnect->setEnabled(false);
    bar->addStretch();
    bar->addWidget(m_btnConnect);
    bar->addWidget(m_btnDisconnect);
    lay->addLayout(bar);
    lay->addStretch();

    connect(m_btnConnect,    &QPushButton::clicked, this, &XboxRemotePlayPlugin::onConnectClicked);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &XboxRemotePlayPlugin::onDisconnectClicked);

    connect(this, &XboxRemotePlayPlugin::stateChanged, [=](XboxRPState s) {
        static const QMap<XboxRPState, QString> labels = {
            {XboxRPState::Disconnected,  "Disconnected"},
            {XboxRPState::Authenticating,"Authenticating (MSA)..."},
            {XboxRPState::Connecting,    "Connecting (WebRTC)..."},
            {XboxRPState::Streaming,     "Streaming"},
            {XboxRPState::Error,         "Error"},
        };
        m_lblState->setText(labels.value(s, "Unknown"));
        bool connected = (s == XboxRPState::Streaming);
        m_btnConnect->setEnabled(!connected && s != XboxRPState::Connecting);
        m_btnDisconnect->setEnabled(connected || s == XboxRPState::Connecting);
    });

    return w;
}

void XboxRemotePlayPlugin::onConnectClicked()
{
    setState(XboxRPState::Authenticating);

    // Xbox Game Streaming requires:
    // 1. MSA OAuth2 token (MSAL or WebView2 login flow)
    // 2. xgamestreaming session init with game title ID
    // 3. ICE/DTLS/SRTP via WebRTC (xgamestreaming.dll handles internally)
    // 4. HEVC video decode → ring buffer
    // 5. XInput report encoding → xgamestreaming input API

    // Stub: transition to error state (SDK integration required)
    setState(XboxRPState::Error);
    emit streamError("Xbox Game Streaming SDK not initialized. "
                     "Ensure GameStreaming.dll is present and MSA login is complete.");
}

void XboxRemotePlayPlugin::onDisconnectClicked()
{
    m_streamSession = nullptr;
    setState(XboxRPState::Disconnected);
}

void XboxRemotePlayPlugin::setState(XboxRPState s)
{
    m_state = s;
    emit stateChanged(s);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::XboxRemotePlayPlugin();
}
