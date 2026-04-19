// PSRemotePlay.cpp — PS5 Remote Play plugin (Chiaki-ng fork)
//
// Protocol stack:
//   1. PSN OAuth2 via embedded WebView2 (Microsoft Edge WebView2 runtime)
//   2. PS5 discovery: UDP broadcast on port 987 / unicast on 9295
//   3. Registration: HTTPS POST to PS5 (PIN exchange, account ID binding)
//   4. ICE/STUN negotiation via libjuice for NAT traversal
//   5. RUDP: reliable ordered UDP transport (Sony proprietary)
//   6. Takion: Sony's session-layer protocol (control + data multiplexing)
//   7. GKCrypt: AEAD cipher for Takion payload encryption (AES-GCM derived)
//   8. Video: HEVC (H.265) or AVC (H.264) NAL stream → decode → ring buffer
//   9. Audio: Opus (48kHz stereo) → PCM
//  10. Input: DS4/DS5 report → Takion → PS5

#include "PSRemotePlay.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>
#include <QSettings>
#include <QMessageBox>

// Chiaki-ng shared library (chiaki.dll / libchiaki.so)
// Exports C API matching the Chiaki session lifecycle
#include <chiaki/session.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>

namespace Helios {

// ── GKCryptContext ────────────────────────────────────────────────────────────

bool GKCryptContext::init(const uint8_t* key, size_t keyLen)
{
    if (keyLen < 32) return false;
    memcpy(m_key, key, 32);
    // IV is derived from the first 12 bytes of key material XOR'd with session nonce
    memcpy(m_iv, key + 32, 12 < (keyLen - 32) ? 12 : (keyLen - 32));
    m_counter = 0;
    return true;
}

bool GKCryptContext::decrypt(const uint8_t* in, size_t inLen, uint8_t* out, size_t& outLen)
{
    // GKCrypt decryption: AES-256-GCM with incrementing counter in IV
    // Actual implementation uses Windows BCrypt or OpenSSL
    // Stub: pass-through (real impl in chiaki.dll)
    if (inLen < 16) return false; // need at least GCM tag
    memcpy(out, in, inLen - 16);
    outLen = inLen - 16;
    m_counter++;
    return true;
}

bool GKCryptContext::encrypt(const uint8_t* in, size_t inLen, uint8_t* out, size_t& outLen)
{
    memcpy(out, in, inLen);
    memset(out + inLen, 0, 16); // GCM tag placeholder
    outLen = inLen + 16;
    m_counter++;
    return true;
}

void GKCryptContext::reset() { m_counter = 0; }

// ── TakionSession ─────────────────────────────────────────────────────────────

TakionSession::TakionSession(QObject* parent) : QObject(parent) {}

TakionSession::~TakionSession() { disconnect(); }

bool TakionSession::connect(const PSRPTarget& target, const PSNAccount& account)
{
    ChiakiConnectInfo info = {};
    info.host = target.host.toUtf8().constData();
    info.regist_key[0] = '\0'; // populated after registration
    info.morning[0]    = '\0';

    // Account ID is a 64-bit integer stored as hex string
    bool ok = false;
    uint64_t accountId = account.accountId.toULongLong(&ok, 16);
    if (!ok) { emit sessionError("Invalid PSN account ID"); return false; }
    info.psn_account_id = accountId;

    // Video callback → emit videoFrame signal
    info.video_profile.width     = 1920;
    info.video_profile.height    = 1080;
    info.video_profile.max_fps   = 60;
    info.video_profile.bitrate   = 15000;
    info.video_profile.codec     = CHIAKI_CODEC_H265; // HEVC preferred

    ChiakiLog* log = new ChiakiLog;
    chiaki_log_init(log, CHIAKI_LOG_ALL, [](ChiakiLogLevel level, const char* msg, void* user) {
        Q_UNUSED(level); Q_UNUSED(user);
        // Forward to Helios logging service if available
        qDebug() << "[Chiaki]" << msg;
    }, nullptr);
    m_log = log;

    ChiakiSession* session = new ChiakiSession;
    ChiakiErrorCode err = chiaki_session_init(session, &info, log);
    if (err != CHIAKI_ERR_SUCCESS) {
        emit sessionError(QString("chiaki_session_init failed: %1").arg(chiaki_error_string(err)));
        delete session;
        return false;
    }

    // Register callbacks
    chiaki_session_set_video_sample_cb(session, TakionSession::videoFrameCb, this);
    chiaki_session_set_audio_frame_cb(session, TakionSession::audioFrameCb, this);

    err = chiaki_session_start(session);
    if (err != CHIAKI_ERR_SUCCESS) {
        emit sessionError(QString("chiaki_session_start failed: %1").arg(chiaki_error_string(err)));
        chiaki_session_fini(session);
        delete session;
        return false;
    }

    m_session = session;
    emit sessionConnected();
    return true;
}

void TakionSession::disconnect()
{
    if (m_session) {
        chiaki_session_stop(reinterpret_cast<ChiakiSession*>(m_session));
        chiaki_session_join(reinterpret_cast<ChiakiSession*>(m_session));
        chiaki_session_fini(reinterpret_cast<ChiakiSession*>(m_session));
        delete reinterpret_cast<ChiakiSession*>(m_session);
        m_session = nullptr;
        emit sessionDisconnected();
    }
    if (m_log) {
        delete reinterpret_cast<ChiakiLog*>(m_log);
        m_log = nullptr;
    }
}

void TakionSession::sendControllerReport(const QByteArray& report)
{
    if (!m_session) return;
    ChiakiControllerState state = {};
    // Map DS5 raw report bytes to ChiakiControllerState fields
    const uint8_t* d = reinterpret_cast<const uint8_t*>(report.constData());
    if (report.size() < 10) return;

    state.left_x  = d[0];
    state.left_y  = d[1];
    state.right_x = d[2];
    state.right_y = d[3];
    state.l2_trigger = d[4];
    state.r2_trigger = d[5];
    // Buttons in d[6..8]
    uint16_t btns = d[6] | (d[7] << 8);
    if (btns & (1 << 4)) state.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
    if (btns & (1 << 5)) state.buttons |= CHIAKI_CONTROLLER_BUTTON_CIRCLE;
    if (btns & (1 << 6)) state.buttons |= CHIAKI_CONTROLLER_BUTTON_SQUARE;
    if (btns & (1 << 7)) state.buttons |= CHIAKI_CONTROLLER_BUTTON_TRIANGLE;

    chiaki_session_set_controller_state(
        reinterpret_cast<ChiakiSession*>(m_session), &state);
}

void TakionSession::videoFrameCb(uint8_t* buf, size_t size, void* user)
{
    auto* self = reinterpret_cast<TakionSession*>(user);
    QByteArray data(reinterpret_cast<const char*>(buf), static_cast<int>(size));
    // Emit with dummy dimensions; real dims come from SPS NAL
    emit self->videoFrame(1920, 1080, CHIAKI_CODEC_H265, data);
}

void TakionSession::audioFrameCb(int16_t* buf, int samples, void* user)
{
    auto* self = reinterpret_cast<TakionSession*>(user);
    QByteArray data(reinterpret_cast<const char*>(buf), samples * 2 * sizeof(int16_t));
    emit self->audioFrame(48000, 2, data);
}

// ── PSRemotePlayPlugin ────────────────────────────────────────────────────────

PSRemotePlayPlugin::PSRemotePlayPlugin(QObject* parent) : QObject(parent) {}

PSRemotePlayPlugin::~PSRemotePlayPlugin() { shutdown(); }

void PSRemotePlayPlugin::initialize(const PluginContext& ctx)
{
    m_ctx    = ctx;
    m_thread = new QThread(this);
    m_session = new TakionSession;
    m_session->moveToThread(m_thread);

    connect(this, &PSRemotePlayPlugin::_connectSession,  m_session, &TakionSession::connect);
    connect(this, &PSRemotePlayPlugin::_disconnectSession, m_session, &TakionSession::disconnect);
    connect(this, &PSRemotePlayPlugin::_sendController,  m_session, &TakionSession::sendControllerReport);

    connect(m_session, &TakionSession::videoFrame,         this, &PSRemotePlayPlugin::onVideoFrame);
    connect(m_session, &TakionSession::sessionConnected,   this, &PSRemotePlayPlugin::onSessionConnected);
    connect(m_session, &TakionSession::sessionDisconnected, this, &PSRemotePlayPlugin::onSessionDisconnected);
    connect(m_session, &TakionSession::sessionError,       this, &PSRemotePlayPlugin::onSessionError);

    m_thread->start();
    m_ringWriter.open(SharedMemoryManager::blockName("VIDEO"), 1920, 1080, 0 /*BGR24*/);
}

void PSRemotePlayPlugin::shutdown()
{
    m_ringWriter.close();
    if (m_thread) {
        emit _disconnectSession();
        m_thread->quit();
        m_thread->wait(5000);
        delete m_session;
        m_session = nullptr;
    }
}

QWidget* PSRemotePlayPlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);

    // Status
    m_lblState = new QLabel("Disconnected", w);
    m_lblState->setStyleSheet("font-weight: bold; color: #aaa;");
    lay->addWidget(m_lblState);

    // Connection group
    auto* grp  = new QGroupBox("PS5 Connection", w);
    auto* grid = new QGridLayout(grp);

    grid->addWidget(new QLabel("PS5 Host:", w), 0, 0);
    m_edtHost = new QLineEdit(w);
    m_edtHost->setPlaceholderText("192.168.1.x or hostname");
    grid->addWidget(m_edtHost, 0, 1);

    grid->addWidget(new QLabel("PIN (8 digits):", w), 1, 0);
    m_edtPin = new QLineEdit(w);
    m_edtPin->setPlaceholderText("12345678");
    m_edtPin->setMaxLength(8);
    grid->addWidget(m_edtPin, 1, 1);

    lay->addWidget(grp);

    auto* bar = new QHBoxLayout;
    m_btnConnect    = new QPushButton("Connect", w);
    m_btnDisconnect = new QPushButton("Disconnect", w);
    m_btnDisconnect->setEnabled(false);
    auto* btnLoadReg = new QPushButton("Load from Registry", w);
    bar->addWidget(btnLoadReg);
    bar->addStretch();
    bar->addWidget(m_btnConnect);
    bar->addWidget(m_btnDisconnect);
    lay->addLayout(bar);
    lay->addStretch();

    connect(m_btnConnect, &QPushButton::clicked, [=]() {
        PSRPTarget target;
        target.host = m_edtHost->text().trimmed();
        target.registrationCode = m_edtPin->text().trimmed();
        if (target.host.isEmpty()) {
            QMessageBox::warning(w, "Helios", "Enter PS5 host address.");
            return;
        }
        PSNAccount account = loadPSNAccount();
        if (account.accountId.isEmpty()) {
            QMessageBox::warning(w, "Helios",
                "No PSN account found. Authenticate via Helios first.");
            return;
        }
        setState(PSRPState::Connecting);
        emit _connectSession(target, account);
    });

    connect(m_btnDisconnect, &QPushButton::clicked, [=]() {
        emit _disconnectSession();
    });

    connect(btnLoadReg, &QPushButton::clicked, [=]() {
        if (loadFromRegistry()) {
            m_lblState->setText("Registry settings loaded.");
        } else {
            QMessageBox::information(w, "Helios",
                "No Chiaki registry settings found.\n"
                "HKCU\\Software\\Chiaki\\Chiaki");
        }
    });

    connect(this, &PSRemotePlayPlugin::stateChanged, [=](PSRPState s) {
        static const QMap<PSRPState, QString> labels = {
            {PSRPState::Disconnected,  "Disconnected"},
            {PSRPState::Authenticating,"Authenticating..."},
            {PSRPState::Connecting,    "Connecting..."},
            {PSRPState::Registering,   "Registering..."},
            {PSRPState::Streaming,     "Streaming"},
            {PSRPState::Error,         "Error"},
        };
        m_lblState->setText(labels.value(s, "Unknown"));
        bool connected = (s == PSRPState::Streaming);
        m_btnConnect->setEnabled(!connected && s != PSRPState::Connecting);
        m_btnDisconnect->setEnabled(connected || s == PSRPState::Connecting);
    });

    return w;
}

bool PSRemotePlayPlugin::loadFromRegistry()
{
    // Read from HKCU\Software\Chiaki\Chiaki
    QSettings reg("HKEY_CURRENT_USER\\Software\\Chiaki\\Chiaki",
                  QSettings::NativeFormat);
    if (reg.allKeys().isEmpty()) return false;

    QString host = reg.value("host").toString();
    if (!host.isEmpty() && m_edtHost)
        m_edtHost->setText(host);

    return true;
}

PSNAccount PSRemotePlayPlugin::loadPSNAccount() const
{
    // PSN account stored in %APPDATA%\HeliosProject\Helios after OAuth flow
    QSettings s("HeliosProject", "Helios");
    PSNAccount acct;
    acct.accountId    = s.value("psn/accountId").toString();
    acct.accessToken  = s.value("psn/accessToken").toString();
    acct.refreshToken = s.value("psn/refreshToken").toString();
    acct.username     = s.value("psn/username").toString();
    return acct;
}

PSRPTarget PSRemotePlayPlugin::loadTarget() const
{
    PSRPTarget t;
    t.host = m_edtHost ? m_edtHost->text().trimmed() : QString();
    return t;
}

void PSRemotePlayPlugin::setState(PSRPState s)
{
    m_state = s;
    emit stateChanged(s);
}

void PSRemotePlayPlugin::onVideoFrame(int width, int height, int codec, QByteArray nalData)
{
    Q_UNUSED(codec);
    // In a real implementation: HEVC/AVC decode → BGR24 → ring buffer
    // Stub: write raw NAL as placeholder
    VideoFrameData frame = {};
    frame.width  = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = 0;
    frame.data   = reinterpret_cast<uint8_t*>(nalData.data());
    frame.size   = nalData.size();
    m_ringWriter.write(frame);

    emit videoFrame(width, height, nalData);
}

void PSRemotePlayPlugin::onSessionConnected()
{
    setState(PSRPState::Streaming);
}

void PSRemotePlayPlugin::onSessionDisconnected()
{
    setState(PSRPState::Disconnected);
}

void PSRemotePlayPlugin::onSessionError(const QString& msg)
{
    setState(PSRPState::Error);
    emit sessionError(msg);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::PSRemotePlayPlugin();
}
