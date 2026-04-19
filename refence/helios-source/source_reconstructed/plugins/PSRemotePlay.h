#pragma once
// PSRemotePlay.h — PS5 Remote Play plugin
// Protocol: PSN OAuth → WebView2 → ICE/STUN (libjuice) → RUDP → Takion → GKCrypt → HEVC/AVC
// Based on Chiaki-ng fork; reads HKCU\Software\Chiaki\Chiaki registry keys

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QString>
#include <QUrl>
#include <QTimer>
#include <cstdint>

// Forward declarations for Chiaki-ng types
struct ChiakiSession;
struct ChiakiConnectInfo;
struct ChiakiLog;

namespace Helios {

enum class PSRPState {
    Disconnected,
    Authenticating,     // PSN OAuth via WebView2
    Connecting,         // ICE/STUN negotiation
    Registering,        // PS5 registration (PIN exchange)
    Streaming,          // Active session
    Error,
};

struct PSRPTarget {
    QString host;           // PS5 IP address or hostname
    uint16_t port = 9295;   // Default PS5 remote play port
    QString registrationCode; // 8-digit PIN
    bool    useStun = false;
    QString stunServer;
};

struct PSNAccount {
    QString accountId;      // 64-bit account ID (hex)
    QString accessToken;    // OAuth2 access token
    QString refreshToken;
    QString username;
};

class GKCryptContext {
public:
    // GKCrypt: custom AEAD cipher used by Sony's Takion protocol
    // Key exchange via DH; per-session AES-GCM derived keys
    bool init(const uint8_t* key, size_t keyLen);
    bool decrypt(const uint8_t* in, size_t inLen, uint8_t* out, size_t& outLen);
    bool encrypt(const uint8_t* in, size_t inLen, uint8_t* out, size_t& outLen);
    void reset();
private:
    uint8_t m_key[32]   = {};
    uint8_t m_iv[12]    = {};
    uint64_t m_counter  = 0;
};

class TakionSession : public QObject {
    Q_OBJECT
public:
    explicit TakionSession(QObject* parent = nullptr);
    ~TakionSession();

    bool connect(const PSRPTarget& target, const PSNAccount& account);
    void disconnect();

signals:
    void videoFrame(int width, int height, int codec, QByteArray nalData);
    void audioFrame(int sampleRate, int channels, QByteArray pcmData);
    void hapticFeedback(QByteArray hapticData);
    void sessionConnected();
    void sessionDisconnected();
    void sessionError(const QString& msg);

public slots:
    void sendControllerReport(const QByteArray& report);

private:
    void* m_session  = nullptr; // ChiakiSession*
    void* m_log      = nullptr; // ChiakiLog*
    GKCryptContext m_crypt;

    static void videoFrameCb(uint8_t* buf, size_t size, void* user);
    static void audioFrameCb(int16_t* buf, int samples, void* user);
    static void hapticCb(uint8_t* buf, size_t size, void* user);
};

class PSRemotePlayPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit PSRemotePlayPlugin(QObject* parent = nullptr);
    ~PSRemotePlayPlugin() override;

    QString  name()        const override { return "PSRemotePlay"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "PS5 Remote Play (Chiaki-ng fork)"; }
    QString  version()     const override { return "1.0.0"; }
    bool     requiresAuthentication() const override { return true; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

    // Read PS5 connection settings from registry
    // HKCU\Software\Chiaki\Chiaki
    bool loadFromRegistry();

signals:
    void stateChanged(PSRPState state);
    void videoFrame(int width, int height, QByteArray data);
    void sessionError(const QString& msg);

    void _connectSession(const PSRPTarget& target, const PSNAccount& account);
    void _disconnectSession();
    void _sendController(const QByteArray& report);

private slots:
    void onVideoFrame(int width, int height, int codec, QByteArray nalData);
    void onSessionConnected();
    void onSessionDisconnected();
    void onSessionError(const QString& msg);
    void performPSNAuth();

private:
    PSNAccount   loadPSNAccount() const;
    PSRPTarget   loadTarget() const;
    void         setState(PSRPState s);

    QThread*      m_thread  = nullptr;
    TakionSession* m_session = nullptr;
    PSRPState      m_state   = PSRPState::Disconnected;

    // Video decode + write to ring buffer
    void*         m_decoder  = nullptr; // HEVC/AVC decoder handle
    VideoRingBufferWriter m_ringWriter;

    // UI
    class QLabel*       m_lblState    = nullptr;
    class QLineEdit*    m_edtHost     = nullptr;
    class QLineEdit*    m_edtPin      = nullptr;
    class QPushButton*  m_btnConnect  = nullptr;
    class QPushButton*  m_btnDisconnect = nullptr;

    PluginContext m_ctx;
};

} // namespace Helios
