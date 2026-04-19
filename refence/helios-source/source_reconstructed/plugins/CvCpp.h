#pragma once
// CvCpp.h — C++ DLL script host plugin (launches CVCppWrapper.exe)

#include "../helios_core/IPlugin.h"
#include <QObject>
#include <QString>
#include <QProcess>
#include <QTimer>
#include <QFileInfo>

namespace Helios {

class CvCppPlugin : public QObject, public IUIPlugin {
    Q_OBJECT

public:
    explicit CvCppPlugin(QObject* parent = nullptr);
    ~CvCppPlugin() override;

    // IPlugin
    QString  name()        const override { return "CvCpp"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "C++ DLL CV script host"; }
    QString  version()     const override { return "1.0.0"; }
    bool     requiresAuthentication() const override { return true; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    // IUIPlugin
    QWidget* createWidget(QWidget* parent) override;

    void loadDll(const QString& dllPath);
    void unloadDll();

signals:
    void dllLoaded(const QString& path);
    void dllUnloaded();
    void workerOutput(const QString& line);
    void workerError(const QString& msg);

private slots:
    void onWrapperFinished(int exitCode, QProcess::ExitStatus status);
    void onWrapperOutput();
    void onWrapperError();

private:
    // CVCppWrapper.exe: delegates to loaded DLL's createWorker() export
    // createWorker signature: extern "C" IPlugin* createWorker(PluginContext*)
    QString resolveWrapper() const;

    QString       m_dllPath;
    QProcess*     m_wrapper    = nullptr;
    PluginContext  m_ctx;
};

} // namespace Helios
