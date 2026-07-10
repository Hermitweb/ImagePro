#pragma once

#include <QMetaObject>
#include <QObject>

namespace yingtu {

class StitchPreviewState : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Idle,
        Synthesizing,
        Ready,
        Highlighted,
        Spotlight
    };
    Q_ENUM(State)

    explicit StitchPreviewState(QObject* parent = nullptr);

    static StitchPreviewState& instance();

    State state() const;

    int highlightedIndex() const;
    void setHighlightedIndex(int index);

    bool isIdle() const;
    bool isReady() const;
    bool isSynthesizing() const;
    bool hasHighlight() const;
    bool isSpotlight() const;

    void transitionTo(State state, int highlightedIndex = -1);

signals:
    void stateChanged(State state, int highlightedIndex);
    void highlightedIndexChanged(int index);

private:
    State m_state = State::Idle;
    int m_highlightedIndex = -1;
};

} // namespace yingtu
