#pragma once

#include "ErrorLevel.h"
#include <QMutex>
#include <QString>

namespace yingtu {

class Logger
{
public:
    static void info(const QString& message, const QString& module = QString());
    static void warning(const QString& message, const QString& module = QString());
    static void error(const QString& message, const QString& module = QString());
    static void fatal(const QString& message, const QString& module = QString());

    static QString logDirectory();
    static QString logFilePath();

private:
    static void write(ErrorLevel level, const QString& message, const QString& module);
    static void rotateIfNeeded();
    static void cleanupOldLogs();

    static QMutex s_mutex;
};

} // namespace yingtu
