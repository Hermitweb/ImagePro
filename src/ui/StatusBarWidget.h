#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QWidget>

namespace yingtu {

class StatusBarWidget : public QStatusBar
{
    Q_OBJECT
public:
    explicit StatusBarWidget(QWidget* parent = nullptr);

    enum class State {
        Ready,
        Processing,
        Error,
        Warning
    };

    void setMessage(const QString& message);
    void setState(State state);
    void setImageCount(int total, int selected);
    void setOutputSize(const QSize& size);
    void setTotalFileSize(qint64 bytes);
    void setProgressVisible(bool visible);
    void setProgress(int percent);

private:
    QLabel* m_stateLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_fileSizeLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
};

} // namespace yingtu
