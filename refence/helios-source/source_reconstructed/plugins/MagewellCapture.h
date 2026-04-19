#pragma once
// MagewellCapture.h — Magewell hardware capture card plugin
// Uses Magewell SDK (LibMWCapture.dll)

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>

namespace Helios {

struct MagewellDevice {
    int     channelIndex;
    QString boardName;
    QString channelName;
};

class MagewellCaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit MagewellCaptureWorker(QObject* parent = nullptr);
    ~MagewellCaptureWorker();

public slots:
    void start(int channelIndex, int width, int height);
    void stop();

signals:
    void frameReady(int width, int height, QByteArray bgrData);
    void captureError(const QString& msg);

private:
    void grab();
    void* m_channel = nullptr; // HCHANNEL
    void* m_capture = nullptr; // HNOTIFY
    QTimer* m_timer = nullptr;
    bool m_running  = false;
    int  m_width = 0, m_height = 0;
};

class MagewellCapturePlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit MagewellCapturePlugin(QObject* parent = nullptr);
    ~MagewellCapturePlugin() override;

    QString  name()        const override { return "MagewellCapture"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Magewell hardware capture card"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

    static QList<MagewellDevice> enumerateDevices();

signals:
    void frameReceived(int width, int height, QByteArray bgrData);
    void captureError(const QString& msg);

    void _start(int channelIndex, int width, int height);
    void _stop();

private slots:
    void onFrame(int width, int height, QByteArray bgrData);

private:
    QThread*                m_thread = nullptr;
    MagewellCaptureWorker*  m_worker = nullptr;
    VideoRingBufferWriter   m_ringWriter;
    PluginContext            m_ctx;
};

} // namespace Helios
