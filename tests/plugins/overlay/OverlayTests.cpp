#include "Overlay.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

namespace
{
MediaEvent IdleEvent(const bool idle)
{
    MediaEvent e;
    e.type = MediaEventType::PropertyChange;
    e.property.prop = PlayerProperty::IdleActive;
    e.property.type = PropertyType::Flag;
    e.property.value.flag = idle ? 1 : 0;
    return e;
}
} // namespace

class OverlayTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void StartsIdle()
    {
        const Overlay o;
        QVERIFY(o.IsIdle());
    }

    void IdleActivePropertyTogglesIdleState()
    {
        Overlay o;
        o.OnMediaEvent(IdleEvent(false));
        QVERIFY(!(o.IsIdle()));
        o.OnMediaEvent(IdleEvent(true));
        QVERIFY(o.IsIdle());
    }

    void ShowCommandWhileIdleDoesNotChangeIdleState()
    {
        Overlay o;
        QVERIFY(o.IsIdle());             // no file loaded
        o.ShowCommand("File not found"); // e.g. selecting a missing history item — must not crash
        QVERIFY(o.IsIdle());
    }

    void NonPropertyEventsAreIgnored()
    {
        Overlay o;
        MediaEvent e;
        e.type = MediaEventType::EndFile; // not a PropertyChange
        o.OnMediaEvent(e);
        QVERIFY(o.IsIdle()); // unchanged
    }
};

namespace
{
const ::framelift::test::Registrar<OverlayTest> kRegisterOverlayTest{"OverlayTest"};
}

#include "OverlayTests.moc"
