#pragma once

#include "utils/ErrorLevel.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

#include <functional>

namespace yingtu {

/** 轻提示上的可选动作按钮（如「打开文件夹」「查看结果」）。 */
struct ToastAction {
    QString text;
    std::function<void()> triggered;
};

class ToastWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ToastWidget(ErrorLevel level, const QString& message, int timeoutMs,
                         QWidget* parent = nullptr, const ToastAction& action = {});

signals:
    void closed();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setupUI();

    ErrorLevel m_level;
    QString m_message;
    int m_timeoutMs;
    ToastAction m_action;
    class QTimer* m_timeoutTimer = nullptr;
};

class ToastManager : public QObject
{
    Q_OBJECT
public:
    static ToastManager& instance();

    void show(ErrorLevel level, const QString& message, QWidget* parent = nullptr);
    void show(ErrorLevel level, const QString& message, const ToastAction& action,
              QWidget* parent = nullptr);
    void showInfo(const QString& message, QWidget* parent = nullptr);
    void showInfo(const QString& message, const ToastAction& action, QWidget* parent = nullptr);
    void showWarning(const QString& message, QWidget* parent = nullptr);
    void showError(const QString& message, QWidget* parent = nullptr);

public:
    static constexpr int TOAST_WIDTH = 280;
    static constexpr int TOAST_HEIGHT = 56;

private:
    explicit ToastManager(QObject* parent = nullptr);
    void repositionToasts();
    void removeToast(ToastWidget* toast);

    QList<ToastWidget*> m_toasts;

    static constexpr int MAX_VISIBLE_TOASTS = 3;
    static constexpr int TOAST_SPACING = 8;
    static constexpr int MARGIN = 16;
};

} // namespace yingtu
