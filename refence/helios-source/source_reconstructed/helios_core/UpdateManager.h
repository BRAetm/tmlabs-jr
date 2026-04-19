#pragma once
// UpdateManager.h — checks, downloads and installs Helios updates

#include <QObject>
#include <QString>
#include <QUrl>

namespace Helios {

struct UpdateInfo {
    UpdateInfo() = default;
    UpdateInfo(const UpdateInfo&) = default;
    UpdateInfo(UpdateInfo&&) = default;
    ~UpdateInfo() = default;

    QString version;
    QString releaseDate;
    QString releaseNotes;
    QUrl    downloadUrl;
    uint64_t totalSize;
    uint32_t totalFiles;
};

struct DownloadItem {
    QString path;
    QUrl    url;
    uint64_t size;
    bool    complete;
};

class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    explicit UpdateDownloader(QObject* parent = nullptr);
    void download(const QList<DownloadItem>& items);

signals:
    void progress(int percent);
    void finished();
    void error(const QString& msg);
};

class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject* parent = nullptr);
    // Launches HeliosUpdater.exe with update manifest
    // Falls back: lib/HeliosUpdater.exe → HeliosUpdater.exe
    void install(const UpdateInfo& info);

signals:
    void finished();
    void error(const QString& msg);
};

class UpdateManager : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Checking,
        Downloading,
        Installing,
        Complete,
        Error,
    };

    explicit UpdateManager(QObject* parent = nullptr);
    ~UpdateManager() override;

    // Manifest: GET https://helios.inputsense.com/api/manifest
    // Format: version_info.json
    void checkForUpdates();

    State       state()      const;
    UpdateInfo  updateInfo() const;

signals:
    void stateChanged(State state);
    void updateAvailable(const UpdateInfo& info);
    void noUpdateAvailable();
    void downloadProgress(int percent);
    void updateComplete();
    void updateError(const QString& msg);

private:
    // If Helios is in protected dir (Program Files):
    //   "Please restart Helios as Administrator to complete the update,
    //    or move Helios to a user directory like Desktop or Documents."
    bool isProtectedDirectory() const;

    State        m_state = State::Idle;
    UpdateInfo   m_info;
    UpdateDownloader* m_downloader = nullptr;
    UpdateInstaller*  m_installer  = nullptr;
};

} // namespace Helios
