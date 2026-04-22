#include "PSRemotePlayPlugin.h"
#include "PairPSDialog.h"
#include "SettingsManager.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>

#include <cstring>
#include <vector>

extern "C" {
#include <labs/session.h>
#include <labs/controller.h>
#include <labs/log.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace Labs {

// ── Impl ────────────────────────────────────────────────────────────────────

struct PSRemotePlayPlugin::Impl {
    PSRemotePlayPlugin* owner = nullptr;

    // Chiaki
    LabsLog       log {};
    LabsSession   session {};
    bool          sessionInited = false;

    // FFmpeg
    const AVCodec*   codec = nullptr;
    AVCodecContext*  codecCtx = nullptr;
    AVPacket*        packet = nullptr;
    AVFrame*         frame = nullptr;
    SwsContext*      sws = nullptr;
    std::vector<uint8_t> bgraBuf;
    int              bgraWidth = 0;
    int              bgraHeight = 0;

    // Controller cache — protected by mutex
    QMutex              ctrlMutex;
    LabsControllerState ctrlState {};

    static void logCb(LabsLogLevel /*lv*/, const char* msg, void* /*user*/)
    {
        qInfo().noquote() << "[labs]" << msg;
    }

    static bool videoSampleCb(uint8_t* buf, size_t size, int32_t frames_lost,
                              bool /*frame_recovered*/, void* user)
    {
        auto* self = static_cast<Impl*>(user);
        return self->feedNal(buf, size, frames_lost);
    }

    static void eventCb(LabsEvent* event, void* user)
    {
        auto* self = static_cast<Impl*>(user);
        switch (event->type) {
            case LABS_EVENT_CONNECTED:
                qInfo() << "[PS] connected";
                break;
            case LABS_EVENT_QUIT:
                qWarning() << "[PS] quit:" << labs_quit_reason_string(event->quit.reason);
                self->owner->m_running.store(false);
                break;
            default:
                break;
        }
    }

    bool feedNal(const uint8_t* buf, size_t size, int32_t /*frames_lost*/)
    {
        if (!codecCtx || !packet || !frame) return false;

        packet->data = const_cast<uint8_t*>(buf);
        packet->size = static_cast<int>(size);

        int rc = avcodec_send_packet(codecCtx, packet);
        if (rc < 0 && rc != AVERROR(EAGAIN)) return false;

        while (true) {
            rc = avcodec_receive_frame(codecCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
            if (rc < 0) return false;

            publishFrame(frame);
            av_frame_unref(frame);
        }
        return true;
    }

    void publishFrame(AVFrame* src)
    {
        const int w = src->width;
        const int h = src->height;
        if (w <= 0 || h <= 0) return;

        if (w != bgraWidth || h != bgraHeight || !sws) {
            if (sws) { sws_freeContext(sws); sws = nullptr; }
            sws = sws_getContext(w, h, (AVPixelFormat)src->format,
                                 w, h, AV_PIX_FMT_BGRA,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
            bgraWidth  = w;
            bgraHeight = h;
            bgraBuf.resize(size_t(w) * size_t(h) * 4);
        }
        if (!sws) return;

        uint8_t* dstData[4] = { bgraBuf.data(), nullptr, nullptr, nullptr };
        int      dstStride[4] = { w * 4, 0, 0, 0 };
        sws_scale(sws, src->data, src->linesize, 0, h, dstData, dstStride);

        Frame f;
        f.width  = w;
        f.height = h;
        f.stride = w * 4;
        f.format = PixelFormat::BGRA8;
        f.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        f.data = QByteArray(reinterpret_cast<const char*>(bgraBuf.data()),
                            int(bgraBuf.size()));

        owner->m_frameCount.fetch_add(1, std::memory_order_relaxed);
        if (owner->m_sink) owner->m_sink->pushFrame(f);
    }

    bool initFfmpeg(LabsCodec cc)
    {
        AVCodecID id = AV_CODEC_ID_H264;
        switch (cc) {
            case LABS_CODEC_H264:  id = AV_CODEC_ID_H264;  break;
            case LABS_CODEC_H265:  // fallthrough
            case LABS_CODEC_H265_HDR: id = AV_CODEC_ID_HEVC; break;
            default: id = AV_CODEC_ID_H264;
        }
        codec = avcodec_find_decoder(id);
        if (!codec) return false;

        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) return false;
        codecCtx->thread_count = 0;         // auto
        codecCtx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
        codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

        if (avcodec_open2(codecCtx, codec, nullptr) < 0) return false;

        packet = av_packet_alloc();
        frame  = av_frame_alloc();
        return packet && frame;
    }

    void finiFfmpeg()
    {
        if (sws)      { sws_freeContext(sws); sws = nullptr; }
        if (frame)    { av_frame_free(&frame); }
        if (packet)   { av_packet_free(&packet); }
        if (codecCtx) { avcodec_free_context(&codecCtx); }
        codec = nullptr;
        bgraBuf.clear(); bgraWidth = bgraHeight = 0;
    }
};

// ── Plugin ──────────────────────────────────────────────────────────────────

PSRemotePlayPlugin::PSRemotePlayPlugin()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;
}

PSRemotePlayPlugin::~PSRemotePlayPlugin() { stop(); }

void PSRemotePlayPlugin::initialize(const PluginContext& ctx)
{
    m_settings = ctx.settings;
}

void PSRemotePlayPlugin::shutdown() { stop(); }

bool PSRemotePlayPlugin::pair(QWidget* parent)
{
    if (!m_settings) return false;
    PairPSDialog dlg(m_settings, parent);
    return dlg.exec() == QDialog::Accepted;
}

bool PSRemotePlayPlugin::start()
{
    if (m_running.load()) return true;
    if (!m_settings) { qWarning() << "PS: no settings"; return false; }

    const QString host       = m_settings->value(QStringLiteral("ps/hostIp")).toString();
    const QByteArray registK = QByteArray::fromBase64(m_settings->value(QStringLiteral("ps/registKey")).toByteArray());
    const QByteArray morning = QByteArray::fromBase64(m_settings->value(QStringLiteral("ps/morning")).toByteArray());
    const bool    isPs5   = m_settings->value(QStringLiteral("ps/isPs5"), true).toBool();
    const int     width   = m_settings->value(QStringLiteral("ps/width"),  1280).toInt();
    const int     height  = m_settings->value(QStringLiteral("ps/height"), 720).toInt();
    const int     fps     = m_settings->value(QStringLiteral("ps/fps"),    60).toInt();
    const int     bitrate = m_settings->value(QStringLiteral("ps/bitrate"),15000).toInt();
    const int     codec   = m_settings->value(QStringLiteral("ps/codec"),  (int)LABS_CODEC_H264).toInt();

    // psn_account_id is not required for LAN when we have registered-host creds.
    // LabsSharp zeros it and streaming works — we replicate that.
    if (host.isEmpty() || registK.size() != LABS_SESSION_AUTH_SIZE
        || morning.size() != 0x10) {
        qWarning() << "PS: settings incomplete. Need ps/hostIp, ps/registKey (base64, 16 bytes),"
                      "ps/morning (base64, 16 bytes), ps/isPs5 (bool)";
        return false;
    }

    // Set up log.
    labs_log_init(&m_impl->log, LABS_LOG_ALL, &Impl::logCb, m_impl.get());

    // Build connect info — mirrors LabsSharp's StreamSession.Start() which is
    // known to stream successfully.
    LabsConnectInfo ci {};
    ci.ps5 = isPs5;
    std::memcpy(ci.regist_key, registK.constData(), LABS_SESSION_AUTH_SIZE);
    std::memcpy(ci.morning,    morning.constData(), 0x10);
    // psn_account_id zeroed — not needed on LAN with a registered host.
    std::memset(ci.psn_account_id, 0, LABS_PSN_ACCOUNT_ID_SIZE);
    ci.video_profile.width   = width;
    ci.video_profile.height  = height;
    ci.video_profile.max_fps = fps;
    ci.video_profile.bitrate = bitrate;
    ci.video_profile.codec   = (LabsCodec)codec;
    ci.video_profile_auto_downgrade = true;
    ci.enable_keyboard  = false;
    ci.enable_dualsense = true;
    ci.audio_video_disabled = (LabsDisableAudioVideo)0;
    ci.auto_regist = false;
    ci.packet_loss_max = 0.05;
    ci.enable_idr_on_fec_failure = true;

    // Init session.
    const QByteArray hostUtf8 = host.toUtf8();
    ci.host = hostUtf8.constData(); // ensure alive during init

    if (labs_session_init(&m_impl->session, &ci, &m_impl->log) != LABS_ERR_SUCCESS) {
        qWarning() << "PS: labs_session_init failed";
        return false;
    }
    m_impl->sessionInited = true;

    labs_session_set_event_cb(&m_impl->session, &Impl::eventCb, m_impl.get());
    labs_session_set_video_sample_cb(&m_impl->session, &Impl::videoSampleCb, m_impl.get());

    // FFmpeg decoder.
    if (!m_impl->initFfmpeg((LabsCodec)codec)) {
        qWarning() << "PS: FFmpeg decoder init failed";
        labs_session_fini(&m_impl->session);
        m_impl->sessionInited = false;
        return false;
    }

    // Start.
    if (labs_session_start(&m_impl->session) != LABS_ERR_SUCCESS) {
        qWarning() << "PS: labs_session_start failed";
        m_impl->finiFfmpeg();
        labs_session_fini(&m_impl->session);
        m_impl->sessionInited = false;
        return false;
    }

    m_targetLabel = isPs5 ? QStringLiteral("PS5 @ %1").arg(host)
                          : QStringLiteral("PS4 @ %1").arg(host);
    m_running.store(true);
    return true;
}

void PSRemotePlayPlugin::stop()
{
    if (!m_running.exchange(false) && !m_impl->sessionInited) return;
    if (m_impl->sessionInited) {
        labs_session_stop(&m_impl->session);
        labs_session_join(&m_impl->session);
        labs_session_fini(&m_impl->session);
        m_impl->sessionInited = false;
    }
    m_impl->finiFfmpeg();
}

void PSRemotePlayPlugin::pushState(const ControllerState& state)
{
    if (!m_running.load() || !m_impl->sessionInited) return;

    QMutexLocker lock(&m_impl->ctrlMutex);
    LabsControllerState& cs = m_impl->ctrlState;
    labs_controller_state_set_idle(&cs);

    uint32_t buttons = 0;
    if (state.buttons & ButtonA)              buttons |= LABS_CONTROLLER_BUTTON_CROSS;
    if (state.buttons & ButtonB)              buttons |= LABS_CONTROLLER_BUTTON_MOON;
    if (state.buttons & ButtonX)              buttons |= LABS_CONTROLLER_BUTTON_BOX;
    if (state.buttons & ButtonY)              buttons |= LABS_CONTROLLER_BUTTON_PYRAMID;
    if (state.buttons & ButtonDpadUp)         buttons |= LABS_CONTROLLER_BUTTON_DPAD_UP;
    if (state.buttons & ButtonDpadDown)       buttons |= LABS_CONTROLLER_BUTTON_DPAD_DOWN;
    if (state.buttons & ButtonDpadLeft)       buttons |= LABS_CONTROLLER_BUTTON_DPAD_LEFT;
    if (state.buttons & ButtonDpadRight)      buttons |= LABS_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (state.buttons & ButtonLeftShoulder)   buttons |= LABS_CONTROLLER_BUTTON_L1;
    if (state.buttons & ButtonRightShoulder)  buttons |= LABS_CONTROLLER_BUTTON_R1;
    if (state.buttons & ButtonLeftThumb)      buttons |= LABS_CONTROLLER_BUTTON_L3;
    if (state.buttons & ButtonRightThumb)     buttons |= LABS_CONTROLLER_BUTTON_R3;
    if (state.buttons & ButtonStart)          buttons |= LABS_CONTROLLER_BUTTON_OPTIONS;
    if (state.buttons & ButtonBack)           buttons |= LABS_CONTROLLER_BUTTON_SHARE;
    if (state.buttons & ButtonGuide)          buttons |= LABS_CONTROLLER_BUTTON_PS;

    cs.buttons  = buttons;
    cs.l2_state = state.leftTrigger;
    cs.r2_state = state.rightTrigger;
    cs.left_x   = state.leftThumbX;
    cs.left_y   = state.leftThumbY;
    cs.right_x  = state.rightThumbX;
    cs.right_y  = state.rightThumbY;

    labs_session_set_controller_state(&m_impl->session, &cs);
}

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::PSRemotePlayPlugin();
}
