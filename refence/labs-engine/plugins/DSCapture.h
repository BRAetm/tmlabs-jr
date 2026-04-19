#pragma once
// DSCapture.h — DirectShow video capture plugin
// Captures BGR24, BGRA8, NV12 from DirectShow devices (capture cards, webcams)

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QString>
#include <QList>
#include <cstdint>

// DirectShow/COM forward declarations
struct IGraphBuilder;
struct ICaptureGraphBuilder2;
struct IBaseFilter;
struct ISampleGrabber;
struct IMediaControl;

namespace Helios {

struct DSCaptureDevice {
    QString name;
    QString devicePath;
};

enum class DSPixelFormat {
    BGR24  = 0,
    BGRA8  = 1,
    NV12   = 2,
};

class DSCaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit DSCaptureWorker(QObject* parent = nullptr);
    ~DSCaptureWorker();

public slots:
    void start(const QString& devicePath, int width, int height, DSPixelFormat fmt);
    void stop();

signals:
    void frameReady(int width, int height, int stride, DSPixelFormat fmt, QByteArray data);
    void captureError(const QString& msg);
    void deviceListReady(const QList<DSCaptureDevice>& devices);

private:
    void buildGraph(const QString& devicePath, int width, int height, DSPixelFormat fmt);
    void teardown();

    IGraphBuilder*         m_graph        = nullptr;
    ICaptureGraphBuilder2* m_captureGraph = nullptr;
    IBaseFilter*           m_sourceFilter = nullptr;
    ISampleGrabber*        m_grabber      = nullptr;
    IMediaControl*         m_control      = nullptr;
    bool m_running = false;
};

class DSCapturePlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit DSCapturePlugin(QObject* parent = nullptr);
    ~DSCapturePlugin() override;

    QString  name()        const override { return "DSCapture"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "DirectShow video capture (BGR24/BGRA8/NV12)"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

    static QList<DSCaptureDevice> enumerateDevices();

signals:
    void frameReceived(int width, int height, int stride, DSPixelFormat fmt, QByteArray data);
    void captureStarted();
    void captureStopped();
    void captureError(const QString& msg);

    void _start(const QString& devicePath, int width, int height, DSPixelFormat fmt);
    void _stop();

private slots:
    void onFrameReady(int width, int height, int stride, DSPixelFormat fmt, QByteArray data);

private:
    void writeFrameToSharedMemory(int width, int height, int stride,
                                  DSPixelFormat fmt, const QByteArray& data);

    QThread*          m_thread  = nullptr;
    DSCaptureWorker*  m_worker  = nullptr;
    VideoRingBufferWriter m_ringWriter;
    PluginContext      m_ctx;
};

} // namespace Helios
