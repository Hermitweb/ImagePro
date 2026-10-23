#include "ErrorHandler.h"
#include "Logger.h"

#include <QCoreApplication>

namespace yingtu {

ErrorHandler& ErrorHandler::instance()
{
    // 单例必须归属主线程：若首次在工作线程调用 instance()，
    // 对象会创建在没有事件循环的线程里，之后所有排队的 errorOccurred
    // 投递都无处处理，错误被静默丢弃。挂到 QCoreApplication 上可保证线程归属。
    //
    // 这里刻意使用「故意泄漏」的指针单例，而不是静态局部对象：
    // 静态局部对象会在进程退出时被析构，而它的父对象（QCoreApplication，
    // 在 main 中于栈上创建）会先析构并 delete 掉这个子对象 ——
    // 随后静态析构又对同一块已释放内存调用析构函数，造成双重释放与堆损坏。
    // 用指针则静态部分无需析构，对象只由父对象删除一次，生命周期唯一。
    static ErrorHandler* handler = new ErrorHandler(QCoreApplication::instance());
    return *handler;
}

ErrorHandler::ErrorHandler(QObject* parent) : QObject(parent), m_lastError(ErrorCode::UNKNOWN)
{
    // Q_DECLARE_METATYPE 只提供编译期声明，跨线程队列连接还需要运行时注册，
    // 否则 errorOccurred 的投递会被静默拒绝。
    qRegisterMetaType<ImageProError>("yingtu::ImageProError");
}

void ErrorHandler::report(const ImageProError& error, const QString& detail)
{
    {
        QMutexLocker locker(&m_mutex);
        m_lastError = error;
    }

    const QString logMessage =
        detail.isEmpty() ? QStringLiteral("错误码 %1 %2").arg(error.codeString(), error.message())
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
