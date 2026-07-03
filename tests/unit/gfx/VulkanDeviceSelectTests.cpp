#include "VulkanDeviceSelect.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

using VulkanDeviceSelect::Candidate;
using VulkanDeviceSelect::kMinApiVersion;
using VulkanDeviceSelect::SelectDevice;

namespace
{
Candidate Eligible(bool discrete = false, bool videoCapable = false)
{
    Candidate c;
    c.apiVersion = kMinApiVersion;
    c.hasGraphicsPresentQueue = true;
    c.hasSwapchainExtension = true;
    c.discrete = discrete;
    c.videoCapable = videoCapable;
    return c;
}
} // namespace

class VulkanDeviceSelectTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void NoCandidatesSelectsNothing()
    {
        QCOMPARE(SelectDevice({}), -1);
    }

    void IneligibleDevicesAreRejected()
    {
        Candidate old = Eligible();
        old.apiVersion = (1u << 22) | (2u << 12); // 1.2 — below the floor
        Candidate noPresent = Eligible();
        noPresent.hasGraphicsPresentQueue = false;
        Candidate noSwapchain = Eligible();
        noSwapchain.hasSwapchainExtension = false;
        QCOMPARE(SelectDevice({old, noPresent, noSwapchain}), -1);
    }

    void VideoCapableBeatsDiscrete()
    {
        // An integrated GPU with a video-decode queue outranks a discrete one without.
        QCOMPARE(SelectDevice({Eligible(true, false), Eligible(false, true)}), 1);
    }

    void DiscreteBreaksTiesWithinSameVideoClass()
    {
        QCOMPARE(SelectDevice({Eligible(false, false), Eligible(true, false)}), 1);
        QCOMPARE(SelectDevice({Eligible(false, true), Eligible(true, true)}), 1);
    }

    void EnumerationOrderBreaksRemainingTies()
    {
        // Drivers list the primary adapter first; equal candidates keep that order.
        QCOMPARE(SelectDevice({Eligible(true, true), Eligible(true, true)}), 0);
    }

    void EligibleBeatsEarlierIneligible()
    {
        Candidate ineligible = Eligible(true, true);
        ineligible.hasSwapchainExtension = false;
        QCOMPARE(SelectDevice({ineligible, Eligible()}), 1);
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanDeviceSelectTests> kRegisterVulkanDeviceSelectTests{"VulkanDeviceSelectTests"};
}

#include "VulkanDeviceSelectTests.moc"
