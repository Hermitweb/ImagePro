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

    // 当前显示的输出尺寸文本（为空表示未显示）。供测试与排障读取。
    QString outputSizeText() const;

protected:
    // 语言切换：重新套用状态栏上的全部文本。
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    QLabel* m_stateLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_fileSizeLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // 重译所需的当前值（语言切换后据此还原文本）。
    State m_state = State::Ready;
    int m_imageCount = 0;
    int m_selectedCount = 0;
    QSize m_outputSize;
};

} // namespace yingtu
