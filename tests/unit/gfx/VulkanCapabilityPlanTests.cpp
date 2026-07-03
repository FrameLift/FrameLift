#include "VulkanCapabilityPlan.h"

#include "QtTestRunner.h"

#include <QtTest/QtTest>

using VulkanCapabilityPlan::Inputs;
using VulkanCapabilityPlan::Negotiate;

namespace
{
Inputs Device14Integrated()
{
    Inputs in;
    in.deviceIs14 = true;
    in.coreHostImageCopy = true;
    in.corePushDescriptor = true;
    in.hostTransferFormatOk = true;
    in.discreteAdapter = false;
    return in;
}

Inputs Device13WithExts()
{
    Inputs in;
    in.deviceIs14 = false;
    in.extHostImageCopy = true;
    in.extPushDescriptor = true;
    in.hostTransferFormatOk = true;
    in.discreteAdapter = false;
    return in;
}
} // namespace

class VulkanCapabilityPlanTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void FullySupported14IntegratedUsesEverythingViaCore()
    {
        const auto plan = Negotiate(Device14Integrated());
        QVERIFY(plan.enableHostCopyFeature);
        QVERIFY(!plan.enableHostCopyExt); // core path: no extension
        QVERIFY(plan.useHostCopy);
        QVERIFY(plan.enablePushDescFeature);
        QVERIFY(!plan.enablePushDescExt);
        QVERIFY(plan.usePushDesc);
    }

    void FullySupported13UsesExtensions()
    {
        const auto plan = Negotiate(Device13WithExts());
        QVERIFY(plan.enableHostCopyFeature);
        QVERIFY(plan.enableHostCopyExt);
        QVERIFY(plan.useHostCopy);
        QVERIFY(!plan.enablePushDescFeature); // no core bit below 1.4
        QVERIFY(plan.enablePushDescExt);
        QVERIFY(plan.usePushDesc);
    }

    void DiscreteAdapterDefaultsHostCopyOffButKeepsFeatureEnabled()
    {
        Inputs in = Device14Integrated();
        in.discreteAdapter = true;
        const auto plan = Negotiate(in);
        QVERIFY(plan.enableHostCopyFeature); // stays available for the env override
        QVERIFY(!plan.useHostCopy);
        QVERIFY(plan.usePushDesc); // unrelated capability is unaffected
    }

    void EnvOverridesBeatTheAdapterDefault()
    {
        Inputs discrete = Device14Integrated();
        discrete.discreteAdapter = true;
        discrete.hostCopyEnv = 1;
        QVERIFY(Negotiate(discrete).useHostCopy);

        Inputs integrated = Device14Integrated();
        integrated.hostCopyEnv = 0;
        QVERIFY(!Negotiate(integrated).useHostCopy);
    }

    void HostCopyNeedsFormatSupport()
    {
        Inputs in = Device14Integrated();
        in.hostTransferFormatOk = false;
        QVERIFY(!Negotiate(in).useHostCopy);
    }

    void CoreBitsAreIgnoredBelow14()
    {
        // A 1.3 device may not use 1.4 core feature bits, only the extensions.
        Inputs in = Device13WithExts();
        in.coreHostImageCopy = true;
        in.corePushDescriptor = true;
        in.extHostImageCopy = false;
        in.extPushDescriptor = false;
        const auto plan = Negotiate(in);
        QVERIFY(!plan.enableHostCopyFeature);
        QVERIFY(!plan.useHostCopy);
        QVERIFY(!plan.enablePushDescFeature);
        QVERIFY(!plan.usePushDesc);
    }

    void NoPushDescEnvForcesThePoolPath()
    {
        Inputs in = Device14Integrated();
        in.noPushDescEnv = true;
        const auto plan = Negotiate(in);
        QVERIFY(plan.enablePushDescFeature); // feature stays on; use is what's disabled
        QVERIFY(!plan.usePushDesc);
    }

    void UnsupportedDeviceGetsNothing()
    {
        const auto plan = Negotiate(Inputs{});
        QVERIFY(!plan.enableHostCopyFeature);
        QVERIFY(!plan.useHostCopy);
        QVERIFY(!plan.usePushDesc);
    }
};

namespace
{
const ::framelift::test::Registrar<VulkanCapabilityPlanTests> kRegisterVulkanCapabilityPlanTests{"VulkanCapabilityPlanTests"};
}

#include "VulkanCapabilityPlanTests.moc"
