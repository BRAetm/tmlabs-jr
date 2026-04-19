#pragma once
// XboxRemotePlay.h — Xbox cloud gaming / xCloud remote play plugin
// Uses Xbox Game Streaming SDK (GameStreaming.dll / xgamestreaming.dll)

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QString>
#include <QUrl>

namespace Helios {

enum class XboxRPState {
    Disconnected,
    Authenticating,
    Connecting,
    Streaming,
    Error,
};

class XboxRemotePlayPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit XboxRemotePlayPlugin(QObject* parent = nullptr);
    ~XboxRemotePlayPlugin() override;

    QString  name()        const override { return "XboxRemotePlay"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Xbox cloud gaming remote play (xCloud)"; }
    QString  version()     const override { return "1.0.0"; }
    bool     requiresAuthentication() const override { return true; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

signals:
    void stateChanged(XboxRPState state);
    void videoFrame(int width, int height, QByteArray data);
    void streamError(const QString& msg);

private slots:
    void onConnectClicked();
    void onDisconnectClicked();

private:
    void setState(XboxRPState s);

    // Xbox auth: MSA OAuth2 via WebView2
    // xCloud stream: WebRTC-based (ICE/DTLS/SRTP)
    // Game input: XInput report → xgamestreaming API
    void* m_streamSession = nullptr;

    XboxRPState m_state = XboxRPState::Disconnected;
    VideoRingBufferWriter m_ringWriter;

    class QLabel*      m_lblState = nullptr;
    class QPushButton* m_btnConnect = nullptr;
    class QPushButton* m_btnDisconnect = nullptr;
    class QLineEdit*   m_edtGameId = nullptr;

    PluginContext m_ctx;
};

} // namespace Helios
