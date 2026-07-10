#pragma once

#include "ImageProError.h"
#include <QMutex>
#include <QObject>
#include <QString>

namespace yingtu {

class ErrorHandler : public QObject
{
    Q_OBJECT
public:
    static ErrorHandler& instance();

    void report(const ImageProError& error, const QString& detail = QString());
    void report(ErrorCode code, const QString& detail = QString());

    ImageProError lastError() const;

signals:
    void errorOccurred(const yingtu::ImageProError& error, const QString& detail);

private:
    explicit ErrorHandler(QObject* parent = nullptr);

    mutable QMutex m_mutex;
    ImageProError m_lastError;
};

} // namespace yingtu
