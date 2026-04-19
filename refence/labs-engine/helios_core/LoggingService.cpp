// LoggingService.cpp

#include "LoggingService.h"
#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>
#include <QStandardPaths>

namespace Helios {

LoggingService::LoggingService(QObject* parent) : QObject(parent) {}

LoggingService::~LoggingService()
{
    if (m_file.isOpen()) m_file.close();
}

void LoggingService::setLogFile(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen()) m_file.close();
    m_file.setFileName(path);
    m_file.open(QIODevice::Append | QIODevice::Text);
}

void LoggingService::setMinLevel(LogLevel level) { m_minLevel = level; }

void LoggingService::debug(const QString& msg)   { write(LogLevel::Debug,   msg); }
void LoggingService::info(const QString& msg)    { write(LogLevel::Info,    msg); }
void LoggingService::warning(const QString& msg) { write(LogLevel::Warning, msg); }
void LoggingService::error(const QString& msg)   { write(LogLevel::Error,   msg); }

void LoggingService::write(LogLevel level, const QString& msg)
{
    if (level < m_minLevel) return;

    static const char* levelStr[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString line = QString("[%1] [%2] %3").arg(ts).arg(levelStr[static_cast<int>(level)]).arg(msg);

    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen()) {
        QTextStream s(&m_file);
        s << line << "\n";
        s.flush();
    }

    if (level >= LogLevel::Error)
        emit logError(line);
    else
        emit logMessage(line);
}

} // namespace Helios
