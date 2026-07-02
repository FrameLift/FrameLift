// Unit tests for PlayerEventSink — the MediaEvent queue + observed-property set
// behind FFmpegPlayer's event surface. The header is libav/Qt-free so it builds
// in the standalone native test suite.

#include "PlayerEventSink.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

namespace
{
MediaEvent Lifecycle(MediaEventType type)
{
    MediaEvent e;
    e.type = type;
    return e;
}
} // namespace

class PlayerEventSinkTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void PollOnEmptyReturnsNone()
    {
        PlayerEventSink sink;
        QCOMPARE(sink.Poll().type, MediaEventType::None);
    }

    void QueueThenPollIsFifo()
    {
        PlayerEventSink sink;
        sink.Queue(Lifecycle(MediaEventType::FileLoaded));
        sink.Queue(Lifecycle(MediaEventType::EndFile));
        QCOMPARE(sink.Poll().type, MediaEventType::FileLoaded);
        QCOMPARE(sink.Poll().type, MediaEventType::EndFile);
        QCOMPARE(sink.Poll().type, MediaEventType::None);
    }

    void QueueFiresWakeupOncePerEvent()
    {
        PlayerEventSink sink;
        int fired = 0;
        sink.SetWakeupCallback(
            [](void* ud)
            {
                ++*static_cast<int*>(ud);
            },
            &fired
        );
        sink.Queue(Lifecycle(MediaEventType::FileLoaded));
        sink.Queue(Lifecycle(MediaEventType::EndFile));
        QCOMPARE(fired, 2);
        // Poll never fires the wakeup.
        (void)sink.Poll();
        QCOMPARE(fired, 2);
    }

    void WakeupMayReenterPoll()
    {
        // The wakeup fires outside the sink's lock, so a callback that drains
        // the queue synchronously (as a host might) must not deadlock.
        PlayerEventSink sink;
        MediaEvent seen{};
        struct Ctx
        {
            PlayerEventSink* sink;
            MediaEvent* seen;
        } ctx{&sink, &seen};
        sink.SetWakeupCallback(
            [](void* ud)
            {
                auto* c = static_cast<Ctx*>(ud);
                *c->seen = c->sink->Poll();
            },
            &ctx
        );
        sink.Queue(Lifecycle(MediaEventType::FileLoaded));
        QCOMPARE(seen.type, MediaEventType::FileLoaded);
        QCOMPARE(sink.Poll().type, MediaEventType::None);
    }

    void EmitIsGatedOnObserve()
    {
        PlayerEventSink sink;
        sink.EmitFlag(PlayerProperty::Pause, true);
        QCOMPARE(sink.Poll().type, MediaEventType::None); // not observed → dropped

        sink.Observe(PlayerProperty::Pause);
        QVERIFY(sink.IsObserved(PlayerProperty::Pause));
        QVERIFY(!sink.IsObserved(PlayerProperty::Duration));

        sink.EmitFlag(PlayerProperty::Pause, true);
        const MediaEvent e = sink.Poll();
        QCOMPARE(e.type, MediaEventType::PropertyChange);
        QCOMPARE(e.property.prop, PlayerProperty::Pause);
        QCOMPARE(e.property.type, PropertyType::Flag);
        QCOMPARE(e.property.value.flag, 1);
    }

    void EmitDoubleCarriesValue()
    {
        PlayerEventSink sink;
        sink.Observe(PlayerProperty::Duration);
        sink.EmitDouble(PlayerProperty::Duration, 42.5);
        const MediaEvent e = sink.Poll();
        QCOMPARE(e.type, MediaEventType::PropertyChange);
        QCOMPARE(e.property.prop, PlayerProperty::Duration);
        QCOMPARE(e.property.type, PropertyType::Double);
        QCOMPARE(e.property.value.dbl, 42.5);
    }

    void OutOfRangeObserveIsIgnored()
    {
        PlayerEventSink sink;
        const auto bogus = static_cast<PlayerProperty>(PlayerEventSink::kPropCount + 7);
        sink.Observe(bogus); // must not write out of bounds
        QVERIFY(!sink.IsObserved(bogus));
        sink.EmitFlag(bogus, true);
        QCOMPARE(sink.Poll().type, MediaEventType::None);
    }
};

namespace
{
const ::framelift::test::Registrar<PlayerEventSinkTests> kRegisterPlayerEventSinkTests{"PlayerEventSinkTests"};
}

#include "PlayerEventSinkTests.moc"
