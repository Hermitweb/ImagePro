#pragma once

#include <QMetaObject>
#include <QObject>
#include <QString>

namespace yingtu {

class ApplicationState : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Idle,
        Loading,
        Processing,
        Success,
        Failed,
        Cancelled
    };
    Q_ENUM(State)

    explicit ApplicationState(QObject* parent = nullptr);

    static ApplicationState& instance();

    State state() const;
    QString reason() const;

    bool canProcess() const;
    bool isBusy() const;
    bool isIdle() const;
    bool isFinished() const;

    void transitionTo(State state, const QString& reason = QString());

signals:
    void stateChanged(State state, QString reason);

private:
    State m_state = State::Idle;
    QString m_reason;
};

} // namespace yingtu
