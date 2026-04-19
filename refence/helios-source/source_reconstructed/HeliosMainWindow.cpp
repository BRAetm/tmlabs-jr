// HeliosMainWindow.cpp — Helios II main application window

#include "HeliosMainWindow.h"
#include "helios_core/LicenseService.h"
#include "helios_core/PluginHost.h"
#include "helios_core/LoggingService.h"
#include "helios_core/SettingsManager.h"
#include "helios_core/UpdateManager.h"
#include "helios_core/SharedMemory.h"
#include "helios_core/IPlugin.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QDockWidget>
#include <QSettings>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QStyleFactory>

HeliosMainWindow::HeliosMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Helios II — v" + QCoreApplication::applicationVersion());
    setMinimumSize(1280, 720);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks);

    setupServices();
    setupMenuBar();
    setupStatusBar();
    applyTheme();
    loadPlugins();
    restoreLayout();

    // Kick off license check on startup
    QTimer::singleShot(500, m_license, &Helios::LicenseService::performLicenseCheck);

    // Check for updates after 5 seconds
    QTimer::singleShot(5000, m_updater, &Helios::UpdateManager::checkForUpdates);
}

HeliosMainWindow::~HeliosMainWindow()
{
    saveLayout();
}

void HeliosMainWindow::setupServices()
{
    m_logging  = new Helios::LoggingService(this);
    m_settings = new Helios::SettingsManager(this);
    m_license  = new Helios::LicenseService(this);
    m_plugins  = new Helios::PluginHost(this);
    m_updater  = new Helios::UpdateManager(this);
    m_shm      = Helios::SharedMemoryManager::instance();

    // Configure logging
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);
    m_logging->setLogFile(logDir + "/helios.log");

    // Load settings
    m_settings->load();

    // Wire license signals
    connect(m_license, &Helios::LicenseService::authenticationSucceeded,
            [this](const Helios::LicenseInfo&) { onAuthenticationSucceeded(); });
    connect(m_license, &Helios::LicenseService::authenticationFailed,
            this, &HeliosMainWindow::onAuthenticationFailed);

    // Wire plugin signals
    connect(m_plugins, &Helios::PluginHost::pluginLoaded,   this, &HeliosMainWindow::onPluginLoaded);
    connect(m_plugins, &Helios::PluginHost::pluginUnloaded, this, &HeliosMainWindow::onPluginUnloaded);
    connect(m_plugins, &Helios::PluginHost::authenticationRequired,
            [this](const QString& msg) {
                m_logging->warning("Auth required: " + msg);
                onShowLoginDialog();
            });

    // Wire update signals
    connect(m_updater, &Helios::UpdateManager::updateAvailable,
            this, &HeliosMainWindow::onUpdateAvailable);
}

void HeliosMainWindow::setupMenuBar()
{
    // File menu
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* actLogin  = fileMenu->addAction("&Login / Account");
    fileMenu->addSeparator();
    auto* actQuit   = fileMenu->addAction("&Quit");
    actQuit->setShortcut(QKeySequence::Quit);

    connect(actLogin, &QAction::triggered, this, &HeliosMainWindow::onShowLoginDialog);
    connect(actQuit,  &QAction::triggered, qApp, &QApplication::quit);

    // Plugins menu
    auto* pluginsMenu = menuBar()->addMenu("&Plugins");
    auto* actReload = pluginsMenu->addAction("&Reload All");
    connect(actReload, &QAction::triggered, [this]() { loadPlugins(); });

    // View menu — toggle dock widgets
    auto* viewMenu = menuBar()->addMenu("&View");
    connect(viewMenu, &QMenu::aboutToShow, [=]() {
        viewMenu->clear();
        for (auto it = m_docks.begin(); it != m_docks.end(); ++it) {
            auto* act = viewMenu->addAction(it.key());
            act->setCheckable(true);
            act->setChecked(it.value()->isVisible());
            QDockWidget* dock = it.value();
            connect(act, &QAction::toggled, dock, &QDockWidget::setVisible);
        }
    });

    // Help menu
    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* actUpdate = helpMenu->addAction("Check for &Updates");
    auto* actAbout  = helpMenu->addAction("&About Helios");

    connect(actUpdate, &QAction::triggered, this, &HeliosMainWindow::onCheckForUpdates);
    connect(actAbout,  &QAction::triggered, this, &HeliosMainWindow::onAbout);
}

void HeliosMainWindow::setupStatusBar()
{
    m_lblAuthStatus = new QLabel("Not authenticated", this);
    m_lblVersion    = new QLabel("Helios v" + QCoreApplication::applicationVersion(), this);

    statusBar()->addWidget(m_lblAuthStatus);
    statusBar()->addPermanentWidget(m_lblVersion);
}

void HeliosMainWindow::loadPlugins()
{
    // Build PluginContext for all plugins
    Helios::PluginContext ctx;
    ctx.logging     = m_logging;
    ctx.sharedMemory = m_shm;
    ctx.license     = m_license;
    ctx.settings    = m_settings;

    // Determine plugins to load from settings [PluginPaths]
    QString pluginsDir = QCoreApplication::applicationDirPath() + "/plugins/";

    // Default plugin load order
    static const QStringList kDefaultPlugins = {
        "OutputPanel.dll",
        "FileExplorer.dll",
        "OnlineResources.dll",
        "DS5Input.dll",
        "DS4Input.dll",
        "XInputInput.dll",
        "ViGEmOutput.dll",
        "TitanBridge.dll",
        "GPC3.dll",
        "DSCapture.dll",
        "MFCapture.dll",
        "OpencvCapture.dll",
        "MagewellCapture.dll",
        "OBSCapture.dll",
        "OpenGLDisplay.dll",
        "DeviceMonitor.dll",
        "PSRemotePlay.dll",
        "XboxRemotePlay.dll",
        "CvPython.dll",
        "CvCpp.dll",
    };

    for (const QString& dll : kDefaultPlugins) {
        QString key = "PluginPaths/" + dll;
        bool enabled = m_settings->value(key, true).toBool();
        if (!enabled) continue;

        if (!m_plugins->loadPlugin(dll)) {
            m_logging->warning("Failed to load plugin: " + dll);
            continue;
        }

        // Initialize with context
        Helios::IPlugin* p = m_plugins->plugin(
            dll.left(dll.lastIndexOf('.')));
        if (p) p->initialize(ctx);
    }

    // Add UI plugins as docks
    for (Helios::IUIPlugin* ui : m_plugins->uiPlugins())
        addPluginDock(ui);
}

void HeliosMainWindow::addPluginDock(Helios::IUIPlugin* plugin)
{
    if (!plugin) return;
    if (m_docks.contains(plugin->name())) return;

    auto* dock = new QDockWidget(plugin->name(), this);
    dock->setObjectName(plugin->name() + "Dock");
    dock->setWidget(plugin->createWidget(dock));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    addDockWidget(Qt::RightDockWidgetArea, dock);
    m_docks[plugin->name()] = dock;
}

void HeliosMainWindow::restoreLayout()
{
    QByteArray state = m_settings->value("MainWindow/state").toByteArray();
    QByteArray geom  = m_settings->value("MainWindow/geometry").toByteArray();
    if (!geom.isEmpty())  restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

void HeliosMainWindow::saveLayout()
{
    m_settings->setValue("MainWindow/state",    saveState());
    m_settings->setValue("MainWindow/geometry", saveGeometry());
    m_settings->save();
}

void HeliosMainWindow::applyTheme()
{
    // Dark theme matching Helios visual style
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(30, 30, 30));
    dark.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    dark.setColor(QPalette::Base,            QColor(20, 20, 20));
    dark.setColor(QPalette::AlternateBase,   QColor(40, 40, 40));
    dark.setColor(QPalette::ToolTipBase,     QColor(50, 50, 50));
    dark.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
    dark.setColor(QPalette::Text,            QColor(220, 220, 220));
    dark.setColor(QPalette::Button,          QColor(45, 45, 45));
    dark.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    dark.setColor(QPalette::BrightText,      Qt::red);
    dark.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 100, 100));
    dark.setColor(QPalette::Disabled, QPalette::Text,       QColor(100, 100, 100));

    qApp->setPalette(dark);

    // Stylesheet for dock title bars and other widgets
    qApp->setStyleSheet(R"(
        QMainWindow::separator { background: #222; width: 2px; height: 2px; }
        QDockWidget::title { background: #1a1a1a; padding: 4px; font-weight: bold; }
        QDockWidget { titlebar-close-icon: url(:/icons/close.png); }
        QMenuBar { background: #1a1a1a; }
        QMenuBar::item:selected { background: #2a82da; }
        QMenu { background: #2a2a2a; border: 1px solid #444; }
        QMenu::item:selected { background: #2a82da; }
        QGroupBox { border: 1px solid #444; margin-top: 8px; border-radius: 4px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }
        QPushButton { background: #3a3a3a; border: 1px solid #555; border-radius: 3px; padding: 4px 12px; }
        QPushButton:hover { background: #4a4a4a; }
        QPushButton:pressed { background: #2a82da; }
        QComboBox { background: #2a2a2a; border: 1px solid #555; border-radius: 3px; padding: 2px 6px; }
        QLineEdit { background: #1a1a1a; border: 1px solid #555; border-radius: 3px; padding: 2px 6px; }
        QTextEdit { background: #111; border: 1px solid #333; }
        QScrollBar:vertical { background: #1a1a1a; width: 12px; }
        QScrollBar::handle:vertical { background: #444; border-radius: 6px; }
        QStatusBar { background: #1a1a1a; }
    )");
}

void HeliosMainWindow::closeEvent(QCloseEvent* event)
{
    saveLayout();
    m_plugins->~PluginHost(); // Graceful plugin unload
    event->accept();
}

void HeliosMainWindow::onAuthenticationSucceeded()
{
    m_lblAuthStatus->setText("Authenticated ✓ — " + m_license->discordId());
    m_logging->info("Authentication successful, Discord ID: " + m_license->discordId());
}

void HeliosMainWindow::onAuthenticationFailed(const QString& reason)
{
    m_lblAuthStatus->setText("Auth failed: " + reason);
    m_logging->error("Authentication failed: " + reason);
    onShowLoginDialog();
}

void HeliosMainWindow::onUpdateAvailable(const Helios::UpdateInfo& info)
{
    int result = QMessageBox::question(this,
        "Helios Update Available",
        QString("Version %1 is available (you have %2).\n\nRelease notes:\n%3\n\nUpdate now?")
            .arg(info.version)
            .arg(QCoreApplication::applicationVersion())
            .arg(info.releaseNotes.left(500)),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes)
        m_updater->checkForUpdates(); // triggers download + install
}

void HeliosMainWindow::onPluginLoaded(const QString& name)
{
    statusBar()->showMessage("Plugin loaded: " + name, 2000);
}

void HeliosMainWindow::onPluginUnloaded(const QString& name)
{
    if (m_docks.contains(name)) {
        delete m_docks.take(name);
    }
}

void HeliosMainWindow::onShowLoginDialog()
{
    // The login flow opens a WebView2 window to Discord OAuth
    // discord.com/oauth2/authorize → redirect → session token stored in session.dat
    QMessageBox::information(this, "Helios Login",
        "Please login via the Helios web portal at inputsense.com\n"
        "Your session will be saved automatically after authentication.");

    // In production: embed WebView2 widget with PSN/Discord OAuth URL
    m_license->performLicenseCheck();
}

void HeliosMainWindow::onCheckForUpdates()
{
    m_updater->checkForUpdates();
    statusBar()->showMessage("Checking for updates...", 3000);
}

void HeliosMainWindow::onAbout()
{
    QMessageBox::about(this, "About Helios II",
        "<h3>Helios II v" + QCoreApplication::applicationVersion() + "</h3>"
        "<p>NBA 2K AI assistance and PS5 Remote Play platform</p>"
        "<p>© InputSense / HeliosProject</p>"
        "<p><a href='https://www.inputsense.com'>www.inputsense.com</a></p>");
}
