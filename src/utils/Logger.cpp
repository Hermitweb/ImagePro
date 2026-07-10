#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace yingtu {

namespace {
constexpr qint64 MAX_LOG_SIZE = 10 * 1024 * 1024; // 10 MB
constexpr int LOG_KEEP_DAYS = 30;
} // namespace

QMutex Logger::s_mutex;

QString Logger::logDirectory()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dirPath = QDir(baseDir).absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(dirPath);
    return dirPath;
}

QString Logger::logFilePath()
{
    const QString dateString = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"));
    return QDir(logDirectory()).absoluteFilePath(dateString + QStringLiteral(".log"));
}

void Logger::info(const QString& message, const QString& module)
{
    write(ErrorLevel::Info, message, module);
}

void Logger::warning(const QString& message, const QString& module)
{
    write(ErrorLevel::Warning, message, module);
}

void Logger::error(const QString& message, const QString& module)
{
    write(ErrorLevel::Error, message, module);
}

void Logger::fatal(const QString& message, const QString& module)
{
    write(ErrorLevel::Fatal, message, module);
}

void Logger::write(ErrorLevel level, const QString& message, const QString& module)
{
    QMutexLocker locker(&s_mutex);

    rotateIfNeeded();
    cleanupOldLogs();

    const QString filePath = logFilePath();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const QString levelString = ErrorLevelHelper::displayName(level);
    const QString moduleString = module.isEmpty() ? QStringLiteral("General") : module;

    QTextStream stream(&file);
    stream << QStringLiteral("[%1] [%2] [%3] %4\n")
                  .arg(timestamp, levelString, moduleString, message);
    stream.flush();
}

void Logger::rotateIfNeeded()
{
    const QString currentPath = logFilePath();
    QFileInfo info(currentPath);
    if (!info.exists() || info.size() < MAX_LOG_SIZE)
        return;

    const QString baseName = info.completeBaseName();
    int index = 1;
    QString rotatedPath;
    do {
        rotatedPath = QDir(logDirectory()).absoluteFilePath(
            QStringLiteral("%1.%2.log").arg(baseName).arg(index));
        ++index;
    } while (QFile::exists(rotatedPath));

    QFile::rename(currentPath, rotatedPath);
}

void Logger::cleanupOldLogs()
{
    const QString dirPath = logDirectory();
    const QDir dir(dirPath);
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.log"),
                                                  QDir::Files,
                                                  QDir::Time);

    const QDateTime now = QDateTime::currentDateTime();
    for (const QFileInfo& fileInfo : files) {
        if (fileInfo.lastModified().daysTo(now) > LOG_KEEP_DAYS) {
            QFile::remove(fileInfo.absoluteFilePath());
        }
    }
}

} // namespace yingtu
