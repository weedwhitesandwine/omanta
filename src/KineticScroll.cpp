#include "KineticScroll.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>
#include <functional>

namespace {

// Only the last stretch of the swipe sets the speed, like a finger flick.
constexpr quint64 kVelocityWindowMs = 100;
// Fingers held still this long before lifting means "stop here", no glide.
constexpr quint64 kPauseBeforeLiftMs = 60;
// Slower than this (px/s) is a placement, not a flick.
constexpr qreal kMinimumVelocity = 150;
// The fingers' speed alone glides noticeably shorter than GTK's kinetic
// scrolling; this carries the release speed up to match (distance grows with
// its square under Flickable's constant deceleration).
constexpr qreal kGain = 1.6;
// Flickable's default ceiling (2500 px/s) cuts a brisk swipe short.
constexpr qreal kMaximumVelocity = 12000;

bool canScroll(QQuickItem *item, Qt::Orientations orientations)
{
    return ((orientations & Qt::Vertical)
            && item->property("contentHeight").toReal() > item->height())
        || ((orientations & Qt::Horizontal)
            && item->property("contentWidth").toReal() > item->width());
}

} // namespace

KineticScroll::KineticScroll(QObject *parent)
    : QObject(parent)
{
}

QQuickItem *KineticScroll::flickableAt(QQuickWindow *window, const QPointF &position,
                                       Qt::Orientations orientations)
{
    if (!window || !window->contentItem())
        return nullptr;

    // Every Flickable whose area holds the point, keeping the deepest: the
    // views put overlays beside themselves, so the topmost item under the
    // pointer is often not inside the Flickable at all.
    QQuickItem *best = nullptr;
    int bestDepth = -1;
    std::function<void(QQuickItem *, int)> walk = [&](QQuickItem *item, int depth) {
        if (!item->isVisible())
            return;
        if (item->inherits("QQuickFlickable") && canScroll(item, orientations)
            && item->contains(item->mapFromScene(position)) && depth >= bestDepth) {
            best = item;
            bestDepth = depth;
        }
        for (QQuickItem *child : item->childItems())
            walk(child, depth + 1);
    };
    walk(window->contentItem(), 0);
    return best;
}

bool KineticScroll::eventFilter(QObject *watched, QEvent *event)
{
    // Qt Quick re-sends the event to items; the window sees each one once.
    if (event->type() == QEvent::Wheel) {
        if (auto *window = qobject_cast<QQuickWindow *>(watched))
            handleWheel(window, static_cast<QWheelEvent *>(event));
    }
    return QObject::eventFilter(watched, event);
}

void KineticScroll::handleWheel(QQuickWindow *window, const QWheelEvent *event)
{
    switch (event->phase()) {
    case Qt::ScrollBegin:
        // Fingers back on the pad: catch the page, as GTK does.
        m_samples.clear();
        if (m_gliding)
            QMetaObject::invokeMethod(m_gliding, "cancelFlick");
        m_gliding = nullptr;
        break;
    case Qt::ScrollUpdate:
        if (!event->pixelDelta().isNull())
            m_samples.append({event->timestamp(), QPointF(event->pixelDelta())});
        break;
    case Qt::ScrollEnd:
        glide(window, event->position(), event->timestamp());
        m_samples.clear();
        break;
    default:
        // A mouse wheel (no phase) or the platform's own momentum.
        break;
    }
}

void KineticScroll::glide(QQuickWindow *window, const QPointF &position, quint64 endMs)
{
    if (m_samples.size() < 2 || endMs - m_samples.last().ms > kPauseBeforeLiftMs)
        return;

    const quint64 from = m_samples.last().ms > kVelocityWindowMs
        ? m_samples.last().ms - kVelocityWindowMs : 0;
    int first = m_samples.size() - 1;
    while (first > 0 && m_samples.at(first - 1).ms >= from)
        --first;
    if (first == m_samples.size() - 1)
        --first;

    // Each delta is the movement since the sample before it, so the first
    // sample only marks the start time.
    QPointF distance;
    for (int i = first + 1; i < m_samples.size(); ++i)
        distance += m_samples.at(i).delta;
    const qreal seconds = qMax<quint64>(m_samples.last().ms - m_samples.at(first).ms, 8) / 1000.0;
    QPointF velocity = distance / seconds * kGain;
    if (std::hypot(velocity.x(), velocity.y()) < kMinimumVelocity)
        return;

    Qt::Orientations orientations;
    if (velocity.y() != 0)
        orientations |= Qt::Vertical;
    if (velocity.x() != 0)
        orientations |= Qt::Horizontal;
    QQuickItem *flickable = flickableAt(window, position, orientations);
    if (!flickable)
        return;

    velocity.setX(qBound(-kMaximumVelocity, velocity.x(), kMaximumVelocity));
    velocity.setY(qBound(-kMaximumVelocity, velocity.y(), kMaximumVelocity));
    if (flickable->property("maximumFlickVelocity").toReal() < kMaximumVelocity)
        flickable->setProperty("maximumFlickVelocity", kMaximumVelocity);

    // After the Flickable has seen this ScrollEnd itself: it ends its own
    // wheel movement there, which would stop a flick started first.
    m_gliding = flickable;
    QPointer<QQuickItem> target = flickable;
    QTimer::singleShot(0, flickable, [target, velocity] {
        if (target)
            QMetaObject::invokeMethod(target, "flick", Q_ARG(qreal, velocity.x()),
                                      Q_ARG(qreal, velocity.y()));
    });
}
