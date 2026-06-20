#include "Overlay.h"

#include <gtest/gtest.h>

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

TEST(OverlayTest, StartsIdle)
{
    const Overlay o;
    EXPECT_TRUE(o.IsIdle());
}

TEST(OverlayTest, IdleActivePropertyTogglesIdleState)
{
    Overlay o;
    o.OnMediaEvent(IdleEvent(false));
    EXPECT_FALSE(o.IsIdle());
    o.OnMediaEvent(IdleEvent(true));
    EXPECT_TRUE(o.IsIdle());
}

TEST(OverlayTest, ShowCommandWhileIdleDoesNotChangeIdleState)
{
    Overlay o;
    ASSERT_TRUE(o.IsIdle());          // no file loaded
    o.ShowCommand("File not found");  // e.g. selecting a missing history item — must not crash
    EXPECT_TRUE(o.IsIdle());
}

TEST(OverlayTest, NonPropertyEventsAreIgnored)
{
    Overlay o;
    MediaEvent e;
    e.type = MediaEventType::EndFile; // not a PropertyChange
    o.OnMediaEvent(e);
    EXPECT_TRUE(o.IsIdle()); // unchanged
}
