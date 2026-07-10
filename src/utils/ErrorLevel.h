#pragma once

#include <QColor>
#include <QString>

namespace yingtu {

enum class ErrorLevel
{
    Info,    // 蓝色，3 秒自动消失
    Warning, // 黄色，5 秒自动消失
    Error,   // 红色，持续显示
    Fatal    // 深红色，阻塞操作
};

class ErrorLevelHelper
{
public:
    static QString displayName(ErrorLevel level);
    static QColor color(ErrorLevel level);
    static int timeoutMs(ErrorLevel level);
};

} // namespace yingtu
