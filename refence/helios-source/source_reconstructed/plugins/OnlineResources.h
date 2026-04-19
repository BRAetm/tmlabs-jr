#pragma once
// OnlineResources.h — Model browser + Discord ID auth management plugin

#include "../helios_core/IPlugin.h"
#include <QObject>
#include <QString>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace Helios {

struct ModelResource {
    QString name;
    QString version;
    QString description;
    QString downloadUrl;
    uint64_t sizeBytes;
    bool     installed;
};

class OnlineResourcesPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit OnlineResourcesPlugin(QObject* parent = nullptr);
    ~OnlineResourcesPlugin() override;

    QString  name()        const override { return "OnlineResources"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Model browser & Discord auth management"; }
    QString  version()     const override { return "1.0.0"; }
    bool     requiresAuthentication() const override { return true; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override {}
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

private slots:
    void fetchModelList();
    void onModelListReply(QNetworkReply* reply);
    void downloadModel(const ModelResource& model);
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();

signals:
    void modelListReady(const QList<ModelResource>& models);
    void downloadProgress(const QString& modelName, int percent);
    void downloadComplete(const QString& modelName);
    void downloadError(const QString& msg);

private:
    void populateTable(const QList<ModelResource>& models);
    QNetworkRequest buildAuthRequest(const QUrl& url) const;

    QNetworkAccessManager* m_nam         = nullptr;
    QNetworkReply*         m_activeDownload = nullptr;
    class QTableWidget*    m_table       = nullptr;
    class QProgressBar*    m_progress    = nullptr;
    QString                m_downloadTarget;
    PluginContext           m_ctx;
};

} // namespace Helios
