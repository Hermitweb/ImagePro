#include "StitchPreviewState.h"

#include <QThread>

namespace yingtu {

StitchPreviewState::StitchPreviewState(QObject* parent)
    : QObject(parent)
{
}

StitchPreviewState& StitchPreviewState::instance()
{
    static StitchPreviewState state;
    return state;
}

StitchPreviewState::State StitchPreviewState::state() const
{
    return m_state;
}

int StitchPreviewState::highlightedIndex() const
{
    return m_highlightedIndex;
}

void StitchPreviewState::setHighlightedIndex(int index)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, index]() {
            setHighlightedIndex(index);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_highlightedIndex == index)
        return;

    m_highlightedIndex = index;
    emit highlightedIndexChanged(index);
}

bool StitchPreviewState::isIdle() const
{
    return m_state == State::Idle;
}

bool StitchPreviewState::isReady() const
{
    return m_state == State::Ready;
}

bool StitchPreviewState::isSynthesizing() const
{
    return m_state == State::Synthesizing;
}

bool StitchPreviewState::hasHighlight() const
{
    return m_state == State::Highlighted || m_state == State::Spotlight;
}

bool StitchPreviewState::isSpotlight() const
{
    return m_state == State::Spotlight;
}

void StitchPreviewState::transitionTo(State state, int highlightedIndex)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, state, highlightedIndex]() {
            transitionTo(state, highlightedIndex);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_state == state && m_highlightedIndex == highlightedIndex)
        return;

    m_state = state;
    if (highlightedIndex >= 0)
        m_highlightedIndex = highlightedIndex;

    emit stateChanged(state, m_highlightedIndex);
}

} // namespace yingtu
