#include "QtEnvironment.h"

#include "QtTestRunner.h"

#include <QtCore/QByteArray>
#include <QtCore/qtenvironmentvariables.h>
#include <QtTest/QtTest>

namespace
{
constexpr const char* kRenderLoop = "QSG_RENDER_LOOP";
constexpr const char* kDecodeDeviceTypes = "QT_FFMPEG_DECODING_HW_DEVICE_TYPES";
constexpr const char* kEncodeDeviceTypes = "QT_FFMPEG_ENCODING_HW_DEVICE_TYPES";

class EnvironmentGuard
{
public:
    EnvironmentGuard()
        : oldRenderLoop_(qgetenv(kRenderLoop)), oldDecodeValue_(qgetenv(kDecodeDeviceTypes)),
          oldEncodeValue_(qgetenv(kEncodeDeviceTypes))
    {
    }

    ~EnvironmentGuard()
    {
        Restore(kRenderLoop, oldRenderLoop_);
        Restore(kDecodeDeviceTypes, oldDecodeValue_);
        Restore(kEncodeDeviceTypes, oldEncodeValue_);
    }

    EnvironmentGuard(const EnvironmentGuard&) = delete;
    EnvironmentGuard& operator=(const EnvironmentGuard&) = delete;

private:
    static void Restore(const char* name, const QByteArray& value)
    {
        if (value.isNull())
        {
            qunsetenv(name);
        }
        else
        {
            qputenv(name, value);
        }
    }

    QByteArray oldRenderLoop_;
    QByteArray oldDecodeValue_;
    QByteArray oldEncodeValue_;
};
} // namespace

class QtEnvironmentTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void SetsQtPolicyWhenVariablesAreUnset()
    {
        const EnvironmentGuard guard;
        qunsetenv(kRenderLoop);
        qunsetenv(kDecodeDeviceTypes);
        qunsetenv(kEncodeDeviceTypes);

        ConfigureQtEnvironment();

        QCOMPARE(qgetenv(kRenderLoop), QByteArray("basic"));
        QCOMPARE(qgetenv(kDecodeDeviceTypes), QByteArray(","));
        QCOMPARE(qgetenv(kEncodeDeviceTypes), QByteArray(","));
    }

    void OverridesExistingQtPolicy()
    {
        const EnvironmentGuard guard;
        qputenv(kRenderLoop, QByteArray("threaded"));
        qputenv(kDecodeDeviceTypes, QByteArray("vaapi,vdpau"));
        qputenv(kEncodeDeviceTypes, QByteArray("cuda,vdpau"));

        ConfigureQtEnvironment();

        QCOMPARE(qgetenv(kRenderLoop), QByteArray("basic"));
        QCOMPARE(qgetenv(kDecodeDeviceTypes), QByteArray(","));
        QCOMPARE(qgetenv(kEncodeDeviceTypes), QByteArray(","));
    }
};

namespace
{
const ::framelift::test::Registrar<QtEnvironmentTests> kRegisterQtEnvironmentTests{"QtEnvironmentTests"};
}

#include "QtEnvironmentTests.moc"
