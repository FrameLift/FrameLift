#include "DebugOverlay.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

class DebugOverlayTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void TogglesVisibility()
    {
        DebugOverlay d;
        QVERIFY(!(d.IsOpen())); // closed by default
        d.Toggle();
        QVERIFY(d.IsOpen());
        d.Toggle();
        QVERIFY(!(d.IsOpen()));
    }

    void MediaEventsDoNotCrashWhileClosed()
    {
        DebugOverlay d;
        MediaEvent e;
        e.type = MediaEventType::PropertyChange;
        e.property.prop = PlayerProperty::IdleActive;
        e.property.type = PropertyType::Flag;
        e.property.value.flag = 1;
        d.OnMediaEvent(e); // must be safe with no player/context
    }
};

namespace
{
const ::framelift::test::Registrar<DebugOverlayTest> kRegisterDebugOverlayTest{"DebugOverlayTest"};
}

#include "DebugOverlayTests.moc"
