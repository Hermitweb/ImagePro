#include "ErrorHandler.h"
#include "Logger.h"

namespace yingtu {

ErrorHandler& ErrorHandler::instance()
{
    static ErrorHandler handler;
    return handler;
}

ErrorHandler::ErrorHandler(QObject* parent)
    : QObject(parent)
    , m_lastError(ErrorCode::UNKNOWN)
{
}

void ErrorHandler::report(const ImageProError& error, const QString& detail)
{
    {
        QMutexLocker locker(&m_mutex);
        m_lastError = error;
    }

    const QString logMessage = detail.isEmpty()
                                   ? QStringLiteral("错误码 %1 %2").arg(error.codeString(), error.message())
                                   : QStringLiteral("错误码 %1 %2 | 详情：%3")
                                         .arg(error.codeString(), error.message(), detail);

    switch (error.level()) {
    case ErrorLevel::Info:
        Logger::info(logMessage, QStringLiteral("Error"));
        break;
    case ErrorLevel::Warning:
        Logger::warning(logMessage, QStringLiteral("Error"));
        break;
    case ErrorLevel::Error:
        Logger::error(logMessage, QStringLiteral("Error"));
        break;
    case ErrorLevel::Fatal:
        Logger::fatal(logMessage, QStringLiteral("Error"));
        break;
    }

    emit errorOccurred(error, detail);
}

void ErrorHandler::report(ErrorCode code, const QString& detail)
{
    report(ImageProError(code), detail);
}

ImageProError ErrorHandler::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

} // namespace yingtu
