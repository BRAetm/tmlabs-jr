#pragma once
// OpencvCapture.h — OpenCV VideoCapture plugin

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>

namespace Helios {

class OpencvCaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit OpencvCaptureWorker(QObject* parent = nullptr);
    ~OpencvCaptureWorker();

public slots:
    void start(int deviceIndex, int width, int height, int fps);
    void startFile(const QString& path);
    void stop();

signals:
    void frameReady(int width, int height, QByteArray bgrData);
    void captureError(const QString& msg);

private:
    void grab();
    void* m_cap     = nullptr; // cv::VideoCapture*
    QTimer* m_timer = nullptr;
    bool m_running  = false;
};

class OpencvCapturePlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit OpencvCapturePlugin(QObject* parent = nullptr);
    ~OpencvCapturePlugin() override;

    QString  name()        const override { return "OpencvCapture"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "OpenCV VideoCapture source"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

signals:
    void frameReceived(int width, int height, QByteArray bgrData);
    void captureError(const QString& msg);

    void _start(int deviceIndex, int width, int height, int fps);
    void _startFile(const QString& path);
    void _stop();

private slots:
    void onFrame(int width, int height, QByteArray bgrData);

private:
    QThread*              m_thread = nullptr;
    OpencvCaptureWorker*  m_worker = nullptr;
    VideoRingBufferWriter m_ringWriter;
    PluginContext          m_ctx;
};

} // namespace Helios
