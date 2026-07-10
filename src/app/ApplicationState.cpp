#include "ApplicationState.h"

#include <QThread>

namespace yingtu {

ApplicationState::ApplicationState(QObject* parent)
    : QObject(parent)
{
}

ApplicationState& ApplicationState::instance()
{
    static ApplicationState state;
    return state;
}

ApplicationState::State ApplicationState::state() const
{
    return m_state;
}

QString ApplicationState::reason() const
{
    return m_reason;
}

bool ApplicationState::canProcess() const
{
    return m_state == State::Idle || m_state == State::Success
           || m_state == State::Failed || m_state == State::Cancelled;
}

bool ApplicationState::isBusy() const
{
    return m_state == State::Loading || m_state == State::Processing;
}

bool ApplicationState::isIdle() const
{
    return m_state == State::Idle;
}

bool ApplicationState::isFinished() const
{
    return m_state == State::Success || m_state == State::Failed
           || m_state == State::Cancelled;
}

void ApplicationState::transitionTo(State state, const QString& reason)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, state, reason]() {
            transitionTo(state, reason);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_state == state && m_reason == reason)
        return;

    m_state = state;
    m_reason = reason;
    emit stateChanged(state, reason);
}

} // namespace yingtu
