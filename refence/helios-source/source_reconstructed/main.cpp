// main.cpp — Helios II application entry point

#include "HeliosMainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QSharedMemory>
#include <QMessageBox>

// Single-instance guard
static bool ensureSingleInstance()
{
    static QSharedMemory guard("Helios2_SingleInstance");
    if (guard.create(1)) return true;
    guard.attach();
    return false;
}

int main(int argc, char* argv[])
{
    // Enable High DPI scaling
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    app.setApplicationName("Helios");
    app.setOrganizationName("HeliosProject");
    app.setApplicationVersion("0.9.6.9");

    // Single-instance check
    if (!ensureSingleInstance()) {
        QMessageBox::warning(nullptr, "Helios II",
            "Helios is already running.\nCheck the system tray.");
        return 1;
    }

    // Ensure app data directories exist
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/scripts");
    QDir().mkpath(appData + "/models");
    QDir().mkpath(appData + "/logs");
    QDir().mkpath(appData + "/userdata/settings");

    HeliosMainWindow window;
    window.show();

    return app.exec();
}
