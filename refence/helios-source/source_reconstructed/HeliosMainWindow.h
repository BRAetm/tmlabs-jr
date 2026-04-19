#pragma once
// HeliosMainWindow.h — Helios II main application window

#include <QMainWindow>
#include <QDockWidget>
#include <QMap>
#include <QString>

namespace Helios {
    class LicenseService;
    class PluginHost;
    class LoggingService;
    class SettingsManager;
    class UpdateManager;
    class SharedMemoryManager;
    class IUIPlugin;
}

class HeliosMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit HeliosMainWindow(QWidget* parent = nullptr);
    ~HeliosMainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAuthenticationSucceeded();
    void onAuthenticationFailed(const QString& reason);
    void onUpdateAvailable(const Helios::UpdateInfo& info);
    void onPluginLoaded(const QString& name);
    void onPluginUnloaded(const QString& name);
    void onShowLoginDialog();
    void onCheckForUpdates();
    void onAbout();

private:
    void setupServices();
    void setupMenuBar();
    void setupStatusBar();
    void loadPlugins();
    void addPluginDock(Helios::IUIPlugin* plugin);
    void restoreLayout();
    void saveLayout();
    void applyTheme();

    // Core services
    Helios::LicenseService*   m_license  = nullptr;
    Helios::PluginHost*       m_plugins  = nullptr;
    Helios::LoggingService*   m_logging  = nullptr;
    Helios::SettingsManager*  m_settings = nullptr;
    Helios::UpdateManager*    m_updater  = nullptr;
    Helios::SharedMemoryManager* m_shm   = nullptr;

    // Dock widgets per plugin
    QMap<QString, QDockWidget*> m_docks;

    // Status bar labels
    class QLabel* m_lblAuthStatus = nullptr;
    class QLabel* m_lblVersion    = nullptr;
};
