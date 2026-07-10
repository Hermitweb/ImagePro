#include "ErrorLevel.h"

namespace yingtu {

QString ErrorLevelHelper::displayName(ErrorLevel level)
{
    switch (level) {
    case ErrorLevel::Info:
        return QStringLiteral("INFO");
    case ErrorLevel::Warning:
        return QStringLiteral("WARN");
    case ErrorLevel::Error:
        return QStringLiteral("ERROR");
    case ErrorLevel::Fatal:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

QColor ErrorLevelHelper::color(ErrorLevel level)
{
    switch (level) {
    case ErrorLevel::Info:
        return QColor(QStringLiteral("#2196F3"));
    case ErrorLevel::Warning:
        return QColor(QStringLiteral("#FFC107"));
    case ErrorLevel::Error:
        return QColor(QStringLiteral("#F44336"));
    case ErrorLevel::Fatal:
        return QColor(QStringLiteral("#B71C1C"));
    }
    return QColor(Qt::black);
}

int ErrorLevelHelper::timeoutMs(ErrorLevel level)
{
    switch (level) {
    case ErrorLevel::Info:
        return 3000;
    case ErrorLevel::Warning:
        return 5000;
    case ErrorLevel::Error:
    case ErrorLevel::Fatal:
        return 0;
    }
    return 0;
}

} // namespace yingtu
