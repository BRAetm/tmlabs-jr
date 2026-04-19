#pragma once
// MFCapture.h — Media Foundation video capture plugin

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QThread>
#include <QString>
#include <QList>

namespace Helios {

struct MFCaptureDevice {
    QString name;
    QString symbolicLink;
};

class MFCaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit MFCaptureWorker(QObject* parent = nullptr);
    ~MFCaptureWorker();

public slots:
    void start(const QString& symbolicLink, int width, int height);
    void stop();

signals:
    void frameReady(int width, int height, int stride, QByteArray data);
    void captureError(const QString& msg);

private:
    void* m_source  = nullptr; // IMFMediaSource*
    void* m_reader  = nullptr; // IMFSourceReader*
    bool  m_running = false;
    void readLoop();
};

class MFCapturePlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit MFCapturePlugin(QObject* parent = nullptr);
    ~MFCapturePlugin() override;

    QString  name()        const override { return "MFCapture"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Media Foundation video capture"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

    static QList<MFCaptureDevice> enumerateDevices();

signals:
    void frameReceived(int width, int height, int stride, QByteArray data);
    void captureError(const QString& msg);

    void _start(const QString& link, int width, int height);
    void _stop();

private slots:
    void onFrame(int width, int height, int stride, QByteArray data);

private:
    QThread*         m_thread = nullptr;
    MFCaptureWorker* m_worker = nullptr;
    VideoRingBufferWriter m_ringWriter;
    PluginContext     m_ctx;
};

} // namespace Helios
