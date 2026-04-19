#pragma once
// LoggingService.h — Helios application logging

#include <QObject>
#include <QString>
#include <QFile>
#include <QMutex>

namespace Helios {

enum class LogLevel { Debug, Info, Warning, Error };

class LoggingService : public QObject {
    Q_OBJECT
public:
    explicit LoggingService(QObject* parent = nullptr);
    ~LoggingService() override;

    void setLogFile(const QString& path);
    void setMinLevel(LogLevel level);

    void debug(const QString& msg);
    void info(const QString& msg);
    void warning(const QString& msg);
    void error(const QString& msg);

signals:
    void logMessage(const QString& formatted);
    void logError(const QString& formatted);

private:
    void write(LogLevel level, const QString& msg);

    QFile    m_file;
    LogLevel m_minLevel = LogLevel::Info;
    QMutex   m_mutex;
};

} // namespace Helios
