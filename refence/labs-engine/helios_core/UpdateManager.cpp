// UpdateManager.cpp — checks, downloads and installs Helios updates

#include "UpdateManager.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>

namespace Helios {

// ── UpdateDownloader ──────────────────────────────────────────────────────────

UpdateDownloader::UpdateDownloader(QObject* parent) : QObject(parent) {}

void UpdateDownloader::download(const QList<DownloadItem>& items)
{
    auto* nam = new QNetworkAccessManager(this);
    int total = items.size();
    int* completed = new int(0);
    int* totalBytes = new int(0);
    int* receivedBytes = new int(0);

    for (const auto& item : items) {
        QNetworkRequest req(item.url);
        req.setHeader(QNetworkRequest::UserAgentHeader, "Helios/2.0");
        QNetworkReply* reply = nam->get(req);

        connect(reply, &QNetworkReply::downloadProgress,
                [=](qint64 recv, qint64 tot) {
                    Q_UNUSED(recv); Q_UNUSED(tot);
                    // Aggregate progress across all items
                });

        connect(reply, &QNetworkReply::finished, [=]() {
            if (reply->error() == QNetworkReply::NoError) {
                QFile f(item.path);
                QDir().mkpath(QFileInfo(item.path).absolutePath());
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(reply->readAll());
                    f.close();
                }
            } else {
                emit error(reply->errorString());
            }
            reply->deleteLater();
            (*completed)++;
            emit progress(*completed * 100 / total);
            if (*completed == total) {
                delete completed;
                delete totalBytes;
                delete receivedBytes;
                emit finished();
            }
        });
    }
}

// ── UpdateInstaller ───────────────────────────────────────────────────────────

UpdateInstaller::UpdateInstaller(QObject* parent) : QObject(parent) {}

void UpdateInstaller::install(const UpdateInfo& info)
{
    Q_UNUSED(info);

    QString dir = QCoreApplication::applicationDirPath();
    // lib/HeliosUpdater.exe → HeliosUpdater.exe fallback
    QString updater = dir + "/lib/HeliosUpdater.exe";
    if (!QFile::exists(updater))
        updater = dir + "/HeliosUpdater.exe";

    if (!QFile::exists(updater)) {
        emit error("HeliosUpdater.exe not found");
        return;
    }

    // Launch updater with update manifest path
    QString manifestPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + "/helios_update_manifest.json";

    // Write manifest
    QJsonObject manifest;
    manifest["version"]     = info.version;
    manifest["releaseDate"] = info.releaseDate;
    manifest["downloadUrl"] = info.downloadUrl.toString();
    manifest["totalSize"]   = static_cast<qint64>(info.totalSize);
    manifest["totalFiles"]  = static_cast<int>(info.totalFiles);

    QFile f(manifestPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(manifest).toJson());
        f.close();
    }

    QProcess::startDetached(updater, {manifestPath,
                                      QString::number(QCoreApplication::applicationPid())});
    emit finished();
}

// ── UpdateManager ─────────────────────────────────────────────────────────────

UpdateManager::UpdateManager(QObject* parent) : QObject(parent)
    , m_downloader(new UpdateDownloader(this))
    , m_installer(new UpdateInstaller(this))
{
    connect(m_downloader, &UpdateDownloader::progress,  this, &UpdateManager::downloadProgress);
    connect(m_downloader, &UpdateDownloader::finished, [this]() {
        m_state = State::Installing;
        emit stateChanged(m_state);
        m_installer->install(m_info);
    });
    connect(m_downloader, &UpdateDownloader::error, this, &UpdateManager::updateError);
    connect(m_installer,  &UpdateInstaller::finished, [this]() {
        m_state = State::Complete;
        emit stateChanged(m_state);
        emit updateComplete();
    });
    connect(m_installer, &UpdateInstaller::error, this, &UpdateManager::updateError);
}

UpdateManager::~UpdateManager() {}

void UpdateManager::checkForUpdates()
{
    m_state = State::Checking;
    emit stateChanged(m_state);

    // GET https://helios.inputsense.com/api/manifest → version_info.json
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://helios.inputsense.com/api/manifest"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "Helios/2.0");
    req.setRawHeader("X-Helios-API-Version", "4");

    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, [=]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_state = State::Error;
            emit stateChanged(m_state);
            emit updateError(reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj   = doc.object();

        m_info.version      = obj["version"].toString();
        m_info.releaseDate  = obj["release_date"].toString();
        m_info.releaseNotes = obj["release_notes"].toString();
        m_info.downloadUrl  = QUrl(obj["download_url"].toString());
        m_info.totalSize    = obj["total_size"].toVariant().toULongLong();
        m_info.totalFiles   = obj["total_files"].toInt();

        QString current = QCoreApplication::applicationVersion();
        if (m_info.version == current || m_info.version.isEmpty()) {
            m_state = State::Idle;
            emit stateChanged(m_state);
            emit noUpdateAvailable();
            return;
        }

        // Check if protected directory
        if (isProtectedDirectory()) {
            emit updateError(
                "Please restart Helios as Administrator to complete the update, "
                "or move Helios to a user directory like Desktop or Documents.");
            m_state = State::Error;
            emit stateChanged(m_state);
            return;
        }

        emit updateAvailable(m_info);
        m_state = State::Downloading;
        emit stateChanged(m_state);

        // Build download items from manifest
        QList<DownloadItem> items;
        DownloadItem item;
        item.url      = m_info.downloadUrl;
        item.path     = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + "/helios_update.zip";
        item.size     = m_info.totalSize;
        item.complete = false;
        items.append(item);

        m_downloader->download(items);
    });
}

UpdateManager::State UpdateManager::state() const { return m_state; }
UpdateInfo UpdateManager::updateInfo() const { return m_info; }

bool UpdateManager::isProtectedDirectory() const
{
    QString appDir = QCoreApplication::applicationDirPath();
    // Check if under Program Files
    QString progFiles = qEnvironmentVariable("ProgramFiles");
    QString progFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    return appDir.startsWith(progFiles, Qt::CaseInsensitive) ||
           appDir.startsWith(progFilesX86, Qt::CaseInsensitive);
}

} // namespace Helios
