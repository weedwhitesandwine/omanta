#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QVector>

class QQuickItem;
class QQuickWindow;
class QWheelEvent;

// Touchpad scrolling that keeps gliding after the fingers lift, as GTK apps
// (Nautilus) do. Qt Quick's Flickable only glides where the platform sends
// momentum events (macOS); on Wayland a two-finger scroll stops dead on
// release. This watches every window's phased, pixel-precise wheel events —
// what a touchpad sends — without consuming them, and at ScrollEnd flicks the
// Flickable under the pointer at the speed the fingers were moving. Lifting
// after a pause gives no glide; touching the pad again stops one.
class KineticScroll : public QObject
{
    Q_OBJECT

public:
    explicit KineticScroll(QObject *parent = nullptr);

    // The innermost visible Flickable under a window position that can scroll
    // along one of the given orientations. Public for the test.
    static QQuickItem *flickableAt(QQuickWindow *window, const QPointF &position,
                                   Qt::Orientations orientations);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Sample
    {
        quint64 ms;
        QPointF delta;
    };

    void handleWheel(QQuickWindow *window, const QWheelEvent *event);
    void glide(QQuickWindow *window, const QPointF &position, quint64 endMs);

    QVector<Sample> m_samples;
    QPointer<QQuickItem> m_gliding;
};
