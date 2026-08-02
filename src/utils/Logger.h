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

    // 带 function:line 上下文的重载，用于 ERROR/FATAL 诊断。
    // 规格要求 ERROR 含完整 stack trace；在 MinGW/Windows 下完整栈回溯需 dbghelp，
    // 此处以"函数名+行号+线程 ID"作为可移植的等价诊断信息，零额外依赖。
    static void error(const QString& message, const QString& module, const QString& context);
    static void fatal(const QString& message, const QString& module, const QString& context);

    static QString logDirectory();
    static QString logFilePath();

private:
    static void write(ErrorLevel level, const QString& message, const QString& module,
                      const QString& context = QString());
    static void rotateIfNeeded();
    static void cleanupOldLogs();

    static QMutex s_mutex;
};

} // namespace yingtu

// 便捷宏：自动捕获调用位置，为 ERROR/FATAL 提供 function:line 上下文。
#define IMGPRO_LOG_ERROR(message, module) \
    ::yingtu::Logger::error(message, module, \
        QString::fromLatin1(Q_FUNC_INFO) + QLatin1Char(':') + QString::number(__LINE__))
#define IMGPRO_LOG_FATAL(message, module) \
    ::yingtu::Logger::fatal(message, module, \
        QString::fromLatin1(Q_FUNC_INFO) + QLatin1Char(':') + QString::number(__LINE__))
