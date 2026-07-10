#include "PreviewInteractionState.h"

#include <QThread>

namespace yingtu {

PreviewInteractionState::PreviewInteractionState(QObject* parent)
    : QObject(parent)
{
}

PreviewInteractionState& PreviewInteractionState::instance()
{
    static PreviewInteractionState state;
    return state;
}

PreviewInteractionState::State PreviewInteractionState::state() const
{
    return m_state;
}

qreal PreviewInteractionState::zoomFactor() const
{
    return m_zoomFactor;
}

void PreviewInteractionState::setZoomFactor(qreal factor)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, factor]() {
            setZoomFactor(factor);
        }, Qt::QueuedConnection);
        return;
    }

    if (qFuzzyCompare(m_zoomFactor, factor))
        return;

    m_zoomFactor = factor;
    emit zoomChanged(factor);
}

QPointF PreviewInteractionState::panOffset() const
{
    return m_panOffset;
}

void PreviewInteractionState::setPanOffset(const QPointF& offset)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, offset]() {
            setPanOffset(offset);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_panOffset == offset)
        return;

    m_panOffset = offset;
    emit panChanged(offset);
}

bool PreviewInteractionState::isIdle() const
{
    return m_state == State::Idle;
}

bool PreviewInteractionState::isPanning() const
{
    return m_state == State::Panning;
}

bool PreviewInteractionState::isZooming() const
{
    return m_state == State::Zooming;
}

bool PreviewInteractionState::isSpotlight() const
{
    return m_state == State::Spotlight;
}

void PreviewInteractionState::reset()
{
    setZoomFactor(1.0);
    setPanOffset(QPointF());
    transitionTo(State::Idle);
}

void PreviewInteractionState::transitionTo(State state)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, state]() {
            transitionTo(state);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_state == state)
        return;

    const bool wasSpotlight = m_state == State::Spotlight;
    m_state = state;
    emit stateChanged(state);

    const bool isSpotlightNow = m_state == State::Spotlight;
    if (wasSpotlight != isSpotlightNow)
        emit spotlightChanged(isSpotlightNow);
}

} // namespace yingtu
