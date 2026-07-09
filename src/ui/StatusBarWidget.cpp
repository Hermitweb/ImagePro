#include "StatusBarWidget.h"

namespace yingtu {

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

StatusBarWidget::StatusBarWidget(QWidget* parent)
    : QStatusBar(parent)
{
    setFixedHeight(28);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setFixedWidth(80);
    setState(State::Ready);

    m_messageLabel = new QLabel(tr("Ready"), this);
    m_countLabel = new QLabel(tr("0 images"), this);
    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setVisible(false);
    m_fileSizeLabel = new QLabel(this);
    m_fileSizeLabel->setVisible(false);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setMaximumWidth(160);
    m_progressBar->setVisible(false);

    addWidget(m_stateLabel);
    addWidget(m_messageLabel, 1);
    addWidget(m_progressBar);
    addWidget(m_sizeLabel);
    addWidget(m_fileSizeLabel);
    addWidget(m_countLabel);
}

void StatusBarWidget::setState(State state)
{
    switch (state) {
    case State::Ready:
        m_stateLabel->setText(QStringLiteral("✓ ") + tr("Ready"));
        m_stateLabel->setStyleSheet(QStringLiteral("color: #67C23A;"));
        break;
    case State::Processing:
        m_stateLabel->setText(QStringLiteral("⟳ ") + tr("Processing"));
        m_stateLabel->setStyleSheet(QStringLiteral("color: #409EFF;"));
        break;
    case State::Error:
        m_stateLabel->setText(QStringLiteral("✕ ") + tr("Error"));
        m_stateLabel->setStyleSheet(QStringLiteral("color: #F56C6C;"));
        break;
    case State::Warning:
        m_stateLabel->setText(QStringLiteral("⚠ ") + tr("Warning"));
        m_stateLabel->setStyleSheet(QStringLiteral("color: #E6A23C;"));
        break;
    }
}

void StatusBarWidget::setMessage(const QString& message)
{
    m_messageLabel->setText(message);
}

void StatusBarWidget::setImageCount(int total, int selected)
{
    if (selected > 0)
        m_countLabel->setText(tr("%1 images, %2 selected").arg(total).arg(selected));
    else
        m_countLabel->setText(tr("%1 images").arg(total));
}

void StatusBarWidget::setOutputSize(const QSize& size)
{
    if (size.isValid() && size.width() > 0 && size.height() > 0) {
        m_sizeLabel->setText(tr("%1 x %2").arg(size.width()).arg(size.height()));
        m_sizeLabel->setVisible(true);
    } else {
        m_sizeLabel->setVisible(false);
    }
}

void StatusBarWidget::setTotalFileSize(qint64 bytes)
{
    if (bytes > 0) {
        m_fileSizeLabel->setText(formatFileSize(bytes));
        m_fileSizeLabel->setVisible(true);
    } else {
        m_fileSizeLabel->setVisible(false);
    }
}

void StatusBarWidget::setProgressVisible(bool visible)
{
    m_progressBar->setVisible(visible);
}

void StatusBarWidget::setProgress(int percent)
{
    m_progressBar->setValue(qBound(0, percent, 100));
}

} // namespace yingtu
