// OnlineResources.cpp — Model browser + Discord auth management plugin

#include "OnlineResources.h"
#include "../helios_core/LicenseService.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

namespace Helios {

OnlineResourcesPlugin::OnlineResourcesPlugin(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &OnlineResourcesPlugin::onModelListReply);
}

OnlineResourcesPlugin::~OnlineResourcesPlugin() {}

void OnlineResourcesPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
}

QNetworkRequest OnlineResourcesPlugin::buildAuthRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Helios/2.0");
    req.setRawHeader("X-Helios-API-Version", "4");
    req.setRawHeader("Origin",  "https://www.inputsense.com");
    req.setRawHeader("Referer", "https://www.inputsense.com/");

    if (m_ctx.license && m_ctx.license->isAuthenticated()) {
        req.setRawHeader("Authorization",
                         ("Bearer " + m_ctx.license->sessionToken()).toUtf8());
        req.setRawHeader("X-Discord-Username",
                         m_ctx.license->discordId().toUtf8());
    }
    return req;
}

QWidget* OnlineResourcesPlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);

    auto* bar       = new QHBoxLayout;
    auto* btnRefresh = new QPushButton("Refresh", w);
    auto* lblStatus  = new QLabel("", w);
    bar->addWidget(btnRefresh);
    bar->addWidget(lblStatus, 1);
    lay->addLayout(bar);

    m_table = new QTableWidget(0, 5, w);
    m_table->setHorizontalHeaderLabels({"Name", "Version", "Size", "Status", "Action"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table);

    m_progress = new QProgressBar(w);
    m_progress->setVisible(false);
    lay->addWidget(m_progress);

    connect(btnRefresh, &QPushButton::clicked, this, &OnlineResourcesPlugin::fetchModelList);
    connect(this, &OnlineResourcesPlugin::modelListReady,
            this, &OnlineResourcesPlugin::populateTable);

    connect(this, &OnlineResourcesPlugin::downloadProgress,
            [=](const QString& name, int pct) {
                lblStatus->setText(QString("Downloading %1...").arg(name));
                m_progress->setVisible(true);
                m_progress->setValue(pct);
            });
    connect(this, &OnlineResourcesPlugin::downloadComplete,
            [=](const QString& name) {
                lblStatus->setText(name + " installed.");
                m_progress->setVisible(false);
                fetchModelList();
            });

    // Auto-load on first show
    QTimer::singleShot(0, this, &OnlineResourcesPlugin::fetchModelList);

    return w;
}

void OnlineResourcesPlugin::fetchModelList()
{
    // GET https://helios.inputsense.com/api/models
    QUrl url("https://helios.inputsense.com/api/models");
    m_nam->get(buildAuthRequest(url));
}

void OnlineResourcesPlugin::onModelListReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit downloadError(reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray arr = doc.array();
    QList<ModelResource> models;

    // Check installed models in %APPDATA%\HeliosProject\Helios\models\
    QString modelsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";

    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        ModelResource m;
        m.name        = obj["name"].toString();
        m.version     = obj["version"].toString();
        m.description = obj["description"].toString();
        m.downloadUrl = obj["download_url"].toString();
        m.sizeBytes   = obj["size"].toVariant().toULongLong();
        m.installed   = QFile::exists(modelsDir + "/" + m.name + ".onnx");
        models.append(m);
    }

    emit modelListReady(models);
}

void OnlineResourcesPlugin::populateTable(const QList<ModelResource>& models)
{
    m_table->setRowCount(models.size());
    for (int i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        m_table->setItem(i, 0, new QTableWidgetItem(m.name));
        m_table->setItem(i, 1, new QTableWidgetItem(m.version));
        m_table->setItem(i, 2, new QTableWidgetItem(
            QString("%1 MB").arg(m.sizeBytes / (1024*1024))));
        m_table->setItem(i, 3, new QTableWidgetItem(m.installed ? "Installed" : "Not installed"));

        auto* btn = new QPushButton(m.installed ? "Update" : "Install");
        const ModelResource model = m;
        connect(btn, &QPushButton::clicked, [=]() { downloadModel(model); });
        m_table->setCellWidget(i, 4, btn);
    }
}

void OnlineResourcesPlugin::downloadModel(const ModelResource& model)
{
    if (m_activeDownload) {
        m_activeDownload->abort();
        m_activeDownload = nullptr;
    }

    m_downloadTarget = model.name;
    QNetworkReply* reply = m_nam->get(buildAuthRequest(QUrl(model.downloadUrl)));
    m_activeDownload     = reply;

    connect(reply, &QNetworkReply::downloadProgress,
            this, &OnlineResourcesPlugin::onDownloadProgress);
    connect(reply, &QNetworkReply::finished,
            this, &OnlineResourcesPlugin::onDownloadFinished);
}

void OnlineResourcesPlugin::onDownloadProgress(qint64 received, qint64 total)
{
    if (total > 0)
        emit downloadProgress(m_downloadTarget, static_cast<int>(received * 100 / total));
}

void OnlineResourcesPlugin::onDownloadFinished()
{
    if (!m_activeDownload) return;
    if (m_activeDownload->error() == QNetworkReply::NoError) {
        QString modelsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
        QDir().mkpath(modelsDir);
        QString outPath = modelsDir + "/" + m_downloadTarget + ".onnx";
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(m_activeDownload->readAll());
            f.close();
        }
        emit downloadComplete(m_downloadTarget);
    } else {
        emit downloadError(m_activeDownload->errorString());
    }
    m_activeDownload->deleteLater();
    m_activeDownload = nullptr;
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::OnlineResourcesPlugin();
}
