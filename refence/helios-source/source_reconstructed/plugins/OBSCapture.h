#pragma once
// OBSCapture.h — OBS plugin integration (virtual camera source)
// Reads from OBS Virtual Camera output or OBS plugin pipe
// Includes PowerShell installer for OBS plugin

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>

namespace Helios {

class OBSCaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit OBSCaptureWorker(QObject* parent = nullptr);
    ~OBSCaptureWorker();

public slots:
    // Connect to OBS Virtual Camera (DirectShow: "OBS Virtual Camera")
    void startVirtualCamera(int width, int height);
    // Connect to OBS via named pipe (obs-helios-bridge plugin)
    void startPipe(const QString& pipeName);
    void stop();

signals:
    void frameReady(int width, int height, QByteArray bgrData);
    void captureError(const QString& msg);
    void obsConnected();
    void obsDisconnected();

private:
    void grabFromDS();
    void grabFromPipe();

    void* m_dsCap   = nullptr; // cv::VideoCapture* for DS virtual cam
    void* m_pipe    = nullptr; // HANDLE for named pipe
    QTimer* m_timer = nullptr;
    bool m_running  = false;
    bool m_usePipe  = false;
    int  m_width = 0, m_height = 0;
};

class OBSCapturePlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit OBSCapturePlugin(QObject* parent = nullptr);
    ~OBSCapturePlugin() override;

    QString  name()        const override { return "OBSCapture"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "OBS Virtual Camera / pipe capture"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

    // Install obs-helios-bridge plugin via PowerShell
    void installOBSPlugin();

signals:
    void frameReceived(int width, int height, QByteArray bgrData);
    void captureError(const QString& msg);
    void obsPluginInstalled();
    void obsPluginInstallError(const QString& msg);

    void _startVC(int width, int height);
    void _startPipe(const QString& name);
    void _stop();

private slots:
    void onFrame(int width, int height, QByteArray data);

private:
    QThread*          m_thread = nullptr;
    OBSCaptureWorker* m_worker = nullptr;
    VideoRingBufferWriter m_ringWriter;
    PluginContext       m_ctx;
};

} // namespace Helios
