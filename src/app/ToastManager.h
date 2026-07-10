#pragma once

#include "utils/ErrorLevel.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

namespace yingtu {

class ToastWidget : public QWidget
{
    Q_OBJECT
public:
    ToastWidget(ErrorLevel level, const QString& message, int timeoutMs, QWidget* parent = nullptr);

signals:
    void closed();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUI();

    ErrorLevel m_level;
    QString m_message;
    int m_timeoutMs;
};

class ToastManager : public QObject
{
    Q_OBJECT
public:
    static ToastManager& instance();

    void show(ErrorLevel level, const QString& message, QWidget* parent = nullptr);
    void showInfo(const QString& message, QWidget* parent = nullptr);
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
