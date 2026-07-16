#include "QtTestRunner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QUrl>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
std::unique_ptr<QQuickItem> CreateDrawer(QQmlEngine& engine, QQuickItem& parent, const bool rightSide)
{
    const QString path = QStringLiteral(FRAMELIFT_TEST_CONTROL_QML_DIR "/FLDrawer.qml");
    QQmlComponent component(&engine, QUrl::fromLocalFile(path));
    if (component.isError())
    {
        qWarning().noquote() << component.errorString();
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    auto* drawer = qobject_cast<QQuickItem*>(object.get());
    if (!drawer)
    {
        qWarning().noquote() << component.errorString();
        return nullptr;
    }

    drawer->setParentItem(&parent);
    drawer->setProperty("rightSide", rightSide);
    drawer->setProperty("drawerWidthRatio", 0.32);
    drawer->setProperty("minimumDrawerWidth", 320.0);
    drawer->setProperty("maximumDrawerWidth", 440.0);
    return std::unique_ptr<QQuickItem>(static_cast<QQuickItem*>(object.release()));
}

qreal VisibleWidth(const QQuickItem& drawer, const QQuickItem& parent, const bool rightSide)
{
    return rightSide ? std::max<qreal>(0.0, parent.width() - drawer.x())
                     : std::max<qreal>(0.0, drawer.width() + drawer.x());
}
} // namespace

class FLDrawerTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void ClosedDrawerRemainsHiddenWhenParentGrows_data()
    {
        QTest::addColumn<bool>("rightSide");
        QTest::newRow("left") << false;
        QTest::newRow("right") << true;
    }

    void ClosedDrawerRemainsHiddenWhenParentGrows()
    {
        QFETCH(bool, rightSide);

        QQmlEngine engine;
        QQuickItem parent;
        parent.setSize(QSizeF(1000, 600));
        std::unique_ptr<QQuickItem> drawer = CreateDrawer(engine, parent, rightSide);
        QVERIFY(drawer);
        QCoreApplication::processEvents();

        QCOMPARE(drawer->property("open").toBool(), false);
        QCOMPARE(VisibleWidth(*drawer, parent, rightSide), 0.0);

        // 1000 * 0.32 uses the 320 px minimum; 1400 * 0.32 reaches the
        // 440 px maximum. The closed endpoint must follow both changes directly.
        parent.setWidth(1400);
        for (int elapsedMs = 0; elapsedMs <= 220; elapsedMs += 10)
        {
            QCoreApplication::processEvents();
            QCOMPARE(VisibleWidth(*drawer, parent, rightSide), 0.0);
            QTest::qWait(10);
        }

        QCOMPARE(drawer->width(), 442.0); // 440 px content plus 1 px bleed per side
        const qreal expectedX = rightSide ? 1401.0 : -442.0;
        QCOMPARE(drawer->x(), expectedX);
    }

    void OpenAndCloseStillAnimate_data()
    {
        QTest::addColumn<bool>("rightSide");
        QTest::newRow("left") << false;
        QTest::newRow("right") << true;
    }

    void OpenAndCloseStillAnimate()
    {
        QFETCH(bool, rightSide);

        QQmlEngine engine;
        QQuickItem parent;
        parent.setSize(QSizeF(1000, 600));
        std::unique_ptr<QQuickItem> drawer = CreateDrawer(engine, parent, rightSide);
        QVERIFY(drawer);
        QCoreApplication::processEvents();

        const qreal closedX = rightSide ? parent.width() + 1.0 : -drawer->width();
        const qreal openX = rightSide ? parent.width() + 1.0 - drawer->width() : -1.0;
        const qreal minimumX = std::min(closedX, openX);
        const qreal maximumX = std::max(closedX, openX);
        QCOMPARE(drawer->x(), closedX);

        drawer->setProperty("open", true);
        QTest::qWait(60);
        QVERIFY(drawer->x() > minimumX);
        QVERIFY(drawer->x() < maximumX);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(drawer->x() - openX) < 0.5, 300);

        drawer->setProperty("open", false);
        QTest::qWait(60);
        QVERIFY(drawer->x() > minimumX);
        QVERIFY(drawer->x() < maximumX);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(drawer->x() - closedX) < 0.5, 300);
    }
};

namespace
{
const ::framelift::test::Registrar<FLDrawerTests> kRegisterFLDrawerTests{"FLDrawerTests"};
}

#include "FLDrawerTests.moc"
