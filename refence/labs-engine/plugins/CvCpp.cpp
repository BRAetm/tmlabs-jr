// CvCpp.cpp — C++ DLL script host plugin

#include "CvCpp.h"
#include "../helios_core/LicenseService.h"
#include "../helios_core/SharedMemory.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDir>

namespace Helios {

CvCppPlugin::CvCppPlugin(QObject* parent) : QObject(parent) {}

CvCppPlugin::~CvCppPlugin() { shutdown(); }

void CvCppPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
}

void CvCppPlugin::shutdown()
{
    unloadDll();
}

QWidget* CvCppPlugin::createWidget(QWidget* parent)
{
    auto* w      = new QWidget(parent);
    auto* layout = new QVBoxLayout(w);
    auto* bar    = new QHBoxLayout;
    auto* lbl    = new QLabel("No DLL loaded", w);
    auto* btnOpen = new QPushButton("Load DLL", w);
    auto* btnStop = new QPushButton("Unload", w);
    auto* output  = new QTextEdit(w);
    output->setReadOnly(true);
    output->setFontFamily("Consolas");

    bar->addWidget(lbl, 1);
    bar->addWidget(btnOpen);
    bar->addWidget(btnStop);
    layout->addLayout(bar);
    layout->addWidget(output);

    connect(btnOpen, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(w, "Select C++ Worker DLL",
            QString(), "DLL Files (*.dll)");
        if (!path.isEmpty()) {
            loadDll(path);
            lbl->setText(QFileInfo(path).fileName());
        }
    });
    connect(btnStop, &QPushButton::clicked, this, &CvCppPlugin::unloadDll);
    connect(this, &CvCppPlugin::workerOutput, output, &QTextEdit::append);

    return w;
}

void CvCppPlugin::loadDll(const QString& dllPath)
{
    unloadDll();
    m_dllPath = dllPath;

    m_wrapper = new QProcess(this);
    connect(m_wrapper, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CvCppPlugin::onWrapperFinished);
    connect(m_wrapper, &QProcess::readyReadStandardOutput, this, &CvCppPlugin::onWrapperOutput);
    connect(m_wrapper, &QProcess::readyReadStandardError,  this, &CvCppPlugin::onWrapperError);

    // CVCppWrapper.exe loads the DLL and calls createWorker()
    // Pass SHM name and session token as args
    QString shmName = SharedMemoryManager::blockName("VIDEO");
    QString token;
    if (m_ctx.license && m_ctx.license->isAuthenticated())
        token = m_ctx.license->sessionToken();

    m_wrapper->start(resolveWrapper(), {dllPath, shmName, token});
    emit dllLoaded(dllPath);
}

void CvCppPlugin::unloadDll()
{
    if (m_wrapper) {
        if (m_wrapper->state() != QProcess::NotRunning) {
            m_wrapper->terminate();
            if (!m_wrapper->waitForFinished(2000))
                m_wrapper->kill();
        }
        m_wrapper->deleteLater();
        m_wrapper = nullptr;
        emit dllUnloaded();
    }
}

QString CvCppPlugin::resolveWrapper() const
{
    QString dir = QCoreApplication::applicationDirPath();
    // lib/CVCppWrapper.exe → CVCppWrapper.exe
    QString primary = dir + "/lib/CVCppWrapper.exe";
    if (QFile::exists(primary)) return primary;
    return dir + "/CVCppWrapper.exe";
}

void CvCppPlugin::onWrapperFinished(int exitCode, QProcess::ExitStatus)
{
    emit workerOutput(QString("CVCppWrapper exited with code %1").arg(exitCode));
}

void CvCppPlugin::onWrapperOutput()
{
    if (!m_wrapper) return;
    while (m_wrapper->canReadLine())
        emit workerOutput(QString::fromUtf8(m_wrapper->readLine()).trimmed());
}

void CvCppPlugin::onWrapperError()
{
    if (!m_wrapper) return;
    emit workerError(QString::fromUtf8(m_wrapper->readAllStandardError()).trimmed());
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::CvCppPlugin();
}
