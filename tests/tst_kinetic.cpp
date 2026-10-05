#include "KineticScroll.h"

#include <QGuiApplication>
#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>

#include <memory>

// Touchpad scrolls glide on after the fingers lift (this fork's addition).
// The swipes here are synthetic phased wheel events, as Qt's Wayland backend
// makes them from a two-finger scroll.
class TestKinetic : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void flingKeepsScrolling();
    void pauseBeforeLiftDoesNotGlide();
    void touchingAgainStopsTheGlide();
    void mouseWheelIsLeftAlone();

private:
    void send(Qt::ScrollPhase phase, qreal dy, quint64 ms);
    void swipe(qreal dyPerEvent, int events, quint64 &ms);
    qreal contentY() const { return m_list->property("contentY").toReal(); }

    QQmlEngine m_engine;
    KineticScroll m_kinetic;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_list = nullptr;
};

void TestKinetic::init()
{
    qApp->installEventFilter(&m_kinetic);
    QQmlComponent component(&m_engine);
    component.setData(R"(
        import QtQuick
        Window {
            width: 300; height: 300; visible: true
            ListView {
                objectName: "list"
                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                model: 2000
                delegate: Rectangle { width: 300; height: 20 }
            }
        })", QUrl());
    m_window.reset(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY2(m_window, qPrintable(component.errorString()));
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_list = m_window->findChild<QQuickItem *>("list");
    QVERIFY(m_list);
    m_list->setProperty("contentY", 4000);
}

void TestKinetic::cleanup()
{
    qApp->removeEventFilter(&m_kinetic);
    m_window.reset();
}

void TestKinetic::send(Qt::ScrollPhase phase, qreal dy, quint64 ms)
{
    const QPointF at(150, 150);
    QWheelEvent event(at, m_window->mapToGlobal(at), QPoint(0, qRound(dy)), QPoint(0, qRound(dy)),
                      Qt::NoButton, Qt::NoModifier, phase, false,
                      Qt::MouseEventSynthesizedBySystem); // what Wayland sets for fingers
    event.setTimestamp(ms);
    QCoreApplication::sendEvent(m_window.get(), &event);
}

// Fingers moving up the pad 8ms apart: the page scrolls down (contentY grows).
void TestKinetic::swipe(qreal dyPerEvent, int events, quint64 &ms)
{
    send(Qt::ScrollBegin, 0, ms);
    for (int i = 0; i < events; ++i) {
        ms += 8;
        send(Qt::ScrollUpdate, dyPerEvent, ms);
    }
}

void TestKinetic::flingKeepsScrolling()
{
    quint64 ms = 1000;
    swipe(-30, 10, ms); // ~3750 px/s
    QTest::qWait(20);
    const qreal atLift = contentY();
    QVERIFY(atLift > 4000);
    send(Qt::ScrollEnd, 0, ms + 4);
    QTRY_VERIFY(m_list->property("flicking").toBool());
    QTRY_VERIFY(!m_list->property("moving").toBool());
    // A glide of v²/2a at Flickable's default deceleration is hundreds of px.
    QVERIFY2(contentY() > atLift + 300, qPrintable(QString::number(contentY() - atLift)));
}

void TestKinetic::pauseBeforeLiftDoesNotGlide()
{
    quint64 ms = 1000;
    swipe(-30, 10, ms);
    QTest::qWait(20);
    const qreal atLift = contentY();
    send(Qt::ScrollEnd, 0, ms + 200); // held still, then lifted
    QTest::qWait(300);
    QCOMPARE(contentY(), atLift);
}

void TestKinetic::touchingAgainStopsTheGlide()
{
    quint64 ms = 1000;
    swipe(-30, 10, ms);
    send(Qt::ScrollEnd, 0, ms + 4);
    QTRY_VERIFY(m_list->property("flicking").toBool());
    ms += 50;
    send(Qt::ScrollBegin, 0, ms);
    QTRY_VERIFY(!m_list->property("flicking").toBool());
    const qreal caught = contentY();
    QTest::qWait(200);
    QCOMPARE(contentY(), caught);
}

void TestKinetic::mouseWheelIsLeftAlone()
{
    const QPointF at(150, 150);
    const qreal before = contentY();
    for (int i = 0; i < 3; ++i) {
        QWheelEvent event(at, m_window->mapToGlobal(at), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window.get(), &event);
    }
    QTRY_VERIFY(contentY() > before);
    QTest::qWait(100);
    QVERIFY(!m_list->property("flicking").toBool());
}

QTEST_MAIN(TestKinetic)
#include "tst_kinetic.moc"
