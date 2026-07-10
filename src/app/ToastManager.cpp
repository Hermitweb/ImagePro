#include "ToastManager.h"
#include "utils/ErrorLevel.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>

namespace yingtu {

ToastWidget::ToastWidget(ErrorLevel level, const QString& message, int timeoutMs, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_level(level)
    , m_message(message)
    , m_timeoutMs(timeoutMs)
{
    setupUI();

    if (m_timeoutMs > 0) {
        QTimer::singleShot(m_timeoutMs, this, &ToastWidget::closed);
        QTimer::singleShot(m_timeoutMs, this, &ToastWidget::close);
    }
}

void ToastWidget::setupUI()
{
    setFixedSize(ToastManager::TOAST_WIDTH, ToastManager::TOAST_HEIGHT);
    setAttribute(Qt::WA_TranslucentBackground);

    QWidget* container = new QWidget(this);
    container->setGeometry(0, 0, width(), height());

    const QColor borderColor = ErrorLevelHelper::color(m_level);
    const bool isDark = palette().window().color().lightness() < 128;
    const QString bgColor = isDark ? QStringLiteral("#2D2D2D") : QStringLiteral("#FFFFFF");
    const QString textColor = isDark ? QStringLiteral("#FFFFFF") : QStringLiteral("#333333");

    container->setStyleSheet(QStringLiteral(
        "background-color: %1;"
        "color: %2;"
        "border-left: 4px solid %3;"
        "border-radius: 6px;")
                                 .arg(bgColor, textColor, borderColor.name()));

    QHBoxLayout* layout = new QHBoxLayout(container);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);

    QLabel* iconLabel = new QLabel(container);
    QString iconText;
    switch (m_level) {
    case ErrorLevel::Info:
        iconText = QStringLiteral("ℹ");
        break;
    case ErrorLevel::Warning:
        iconText = QStringLiteral("⚠");
        break;
    case ErrorLevel::Error:
    case ErrorLevel::Fatal:
        iconText = QStringLiteral("✗");
        break;
    }
    iconLabel->setText(iconText);
    iconLabel->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: 16px;").arg(borderColor.name()));
    layout->addWidget(iconLabel);

    QLabel* messageLabel = new QLabel(m_message, container);
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet(QStringLiteral("border: none; font-size: 13px;"));
    layout->addWidget(messageLabel, 1);
}

void ToastWidget::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    emit closed();
    close();
}

ToastManager& ToastManager::instance()
{
    static ToastManager manager;
    return manager;
}

ToastManager::ToastManager(QObject* parent)
    : QObject(parent)
{
}

void ToastManager::show(ErrorLevel level, const QString& message, QWidget* parent)
{
    if (m_toasts.size() >= MAX_VISIBLE_TOASTS) {
        ToastWidget* oldest = m_toasts.takeFirst();
        oldest->close();
        oldest->deleteLater();
    }

    QWidget* hostWindow = parent ? parent->window() : nullptr;
    int timeoutMs = ErrorLevelHelper::timeoutMs(level);
    if (level == ErrorLevel::Error || level == ErrorLevel::Fatal)
        timeoutMs = 0;

    ToastWidget* toast = new ToastWidget(level, message, timeoutMs, hostWindow);
    m_toasts.append(toast);

    connect(toast, &ToastWidget::closed, this, [this, toast]() { removeToast(toast); });

    repositionToasts();
    toast->show();

    QPropertyAnimation* fadeIn = new QPropertyAnimation(toast, QByteArrayLiteral("windowOpacity"), toast);
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastManager::showInfo(const QString& message, QWidget* parent)
{
    show(ErrorLevel::Info, message, parent);
}

void ToastManager::showWarning(const QString& message, QWidget* parent)
{
    show(ErrorLevel::Warning, message, parent);
}

void ToastManager::showError(const QString& message, QWidget* parent)
{
    show(ErrorLevel::Error, message, parent);
}

void ToastManager::repositionToasts()
{
    QRect availableRect;
    if (!m_toasts.isEmpty() && m_toasts.first()->parentWidget()) {
        availableRect = m_toasts.first()->parentWidget()->rect();
    } else {
        QScreen* screen = QGuiApplication::primaryScreen();
        availableRect = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    }

    const int startX = availableRect.right() - TOAST_WIDTH - MARGIN;
    const int startY = availableRect.bottom() - TOAST_HEIGHT - MARGIN;

    for (int i = 0; i < m_toasts.size(); ++i) {
        ToastWidget* toast = m_toasts.at(i);
        const int y = startY - i * (TOAST_HEIGHT + TOAST_SPACING);
        toast->move(startX, y);
    }
}

void ToastManager::removeToast(ToastWidget* toast)
{
    if (!m_toasts.contains(toast))
        return;

    m_toasts.removeOne(toast);
    toast->deleteLater();
    repositionToasts();
}

} // namespace yingtu
