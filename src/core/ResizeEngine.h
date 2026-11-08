#pragma once

#include "utils/ResizeSettings.h"
#include <QImage>
#include <QObject>
#include <QStringList>

#include <atomic>

namespace yingtu {

class ResizeEngine : public QObject
{
    Q_OBJECT
public:
    explicit ResizeEngine(QObject* parent = nullptr);

    void setSettings(const ResizeSettings& settings) { m_settings = settings; }

    /** 取消标志（可选）：置位后剩余文件跳过，已完成部分正常返回。 */
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    QStringList process(const QStringList& filePaths, bool* ok = nullptr);
    static QImage resize(const QImage& source, const ResizeSettings& settings);
    static QSize outputSize(const QImage& source, const ResizeSettings& settings);
    static QSize outputSize(const QSize& sourceSize, const ResizeSettings& settings);

signals:
    void progress(int percent);
    void finished(const QStringList& outputPaths);
    void error(const QString& message);

private:
    QString processSingle(const QString& filePath);
    bool cancelRequested() const { return m_cancelFlag && m_cancelFlag->load(); }

    ResizeSettings m_settings;
    std::atomic<bool>* m_cancelFlag = nullptr;
};

} // namespace yingtu
