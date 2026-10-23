#include "Logger.h"
#include "AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>

namespace yingtu {

namespace {
constexpr qint64 MAX_LOG_SIZE = 10LL * 1024 * 1024; // 10 MB
constexpr int LOG_KEEP_DAYS = 30;

// 过期日志清理每天最多执行一次。cleanupOldLogs 需要枚举整个日志目录并对每个
// 文件计算天数，放在每次 write() 里会让所有 QtConcurrent 工作线程在全局互斥锁
// 后面被目录 I/O 串行化，批处理时吞吐明显下降。由 s_mutex 保护。
//
// 同样用函数内 static：命名空间作用域的对象具有静态存储期，
// 其构造若抛异常无法被捕获，进程会直接终止。
QDate& lastCleanupDate()
{
    static QDate date;
    return date;
}
} // namespace

QMutex& Logger::mutex()
{
    static QMutex m;
    return m;
}

QString Logger::logDirectory()
{
    const QString baseDir = appDataDir();
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

void Logger::error(const QString& message, const QString& module, const QString& context)
{
    write(ErrorLevel::Error, message, module, context);
}

void Logger::fatal(const QString& message, const QString& module, const QString& context)
{
    write(ErrorLevel::Fatal, message, module, context);
}

void Logger::write(ErrorLevel level, const QString& message, const QString& module,
                   const QString& context)
{
    QMutexLocker locker(&mutex());

    rotateIfNeeded();

    // 目录扫描每天只做一次，避免每行日志都触发一次全目录枚举。
    const QDate today = QDateTime::currentDateTime().date();
    if (lastCleanupDate() != today) {
        cleanupOldLogs();
        lastCleanupDate() = today;
    }

    const QString filePath = logFilePath();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const QString levelString = ErrorLevelHelper::displayName(level);
    const QString moduleString = module.isEmpty() ? QStringLiteral("General") : module;
    // 线程 ID：多线程图像处理（QtConcurrent worker）诊断必需，便于定位跨线程问题。
    const QString tidString =
        QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);

    QString line = QStringLiteral("[%1] [%2] [%3] [%4] %5")
                       .arg(timestamp, levelString, moduleString, tidString, message);
    // ERROR/FATAL 附带 function:line 上下文，满足"含堆栈"规格的可移植诊断需求。
    if (!context.isEmpty())
        line += QStringLiteral(" @ ") + context;
    line += QLatin1Char('\n');

    QTextStream stream(&file);
    stream << line;
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
        rotatedPath = QDir(logDirectory())
                          .absoluteFilePath(QStringLiteral("%1.%2.log").arg(baseName).arg(index));
        ++index;
    } while (QFile::exists(rotatedPath));

    QFile::rename(currentPath, rotatedPath);
}

void Logger::cleanupOldLogs()
{
    const QString dirPath = logDirectory();
    const QDir dir(dirPath);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("*.log"), QDir::Files, QDir::Time);

    const QDateTime now = QDateTime::currentDateTime();
    for (const QFileInfo& fileInfo : files) {
        if (fileInfo.lastModified().daysTo(now) > LOG_KEEP_DAYS) {
            QFile::remove(fileInfo.absoluteFilePath());
        }
    }
}

} // namespace yingtu
