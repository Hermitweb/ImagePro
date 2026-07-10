#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointF>

namespace yingtu {

class PreviewInteractionState : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Idle,
        Panning,
        Zooming,
        Spotlight
    };
    Q_ENUM(State)

    explicit PreviewInteractionState(QObject* parent = nullptr);

    static PreviewInteractionState& instance();

    State state() const;

    qreal zoomFactor() const;
    void setZoomFactor(qreal factor);

    QPointF panOffset() const;
    void setPanOffset(const QPointF& offset);

    bool isIdle() const;
    bool isPanning() const;
    bool isZooming() const;
    bool isSpotlight() const;

    void reset();
    void transitionTo(State state);

signals:
    void stateChanged(State state);
    void zoomChanged(qreal factor);
    void panChanged(const QPointF& offset);
    void spotlightChanged(bool active);

private:
    State m_state = State::Idle;
    qreal m_zoomFactor = 1.0;
    QPointF m_panOffset;
};

} // namespace yingtu
