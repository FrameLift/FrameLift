#include "QmlCompositor.h"

#include "QtTestRunner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QObject>
#include <QtCore/QTemporaryFile>
#include <QtCore/QUrl>
#include <QtQuick/QQuickItem>
#include <QtTest/QtTest>

namespace
{
class AnchoredViewFile final
{
public:
    bool Open()
    {
        if (!file_.open())
        {
            return false;
        }

        static constexpr char kSource[] = R"(
import QtQuick

Item {
    required property var viewModel
    anchors.fill: parent
}
)";
        if (file_.write(kSource) != static_cast<qint64>(sizeof(kSource) - 1))
        {
            return false;
        }
        return file_.flush();
    }

    [[nodiscard]] QString Url() const
    {
        return QUrl::fromLocalFile(file_.fileName()).toString();
    }

private:
    QTemporaryFile file_;
};

struct LoadedItems
{
    QQuickItem* viewport = nullptr;
    QQuickItem* pluginRoot = nullptr;
};

LoadedItems FindLoadedItems(QQuickItem& windowRoot)
{
    const QList<QQuickItem*> viewportCandidates = windowRoot.childItems();
    if (viewportCandidates.size() != 1)
    {
        return {};
    }

    QQuickItem* viewport = viewportCandidates.constFirst();
    const QList<QQuickItem*> pluginCandidates = viewport->childItems();
    if (pluginCandidates.size() != 1)
    {
        return {viewport, nullptr};
    }
    return {viewport, pluginCandidates.constFirst()};
}
} // namespace

class QmlCompositorTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void AnchoredRootUsesInsetSetBeforeLoad()
    {
        QQuickItem windowRoot;
        windowRoot.setSize(QSizeF(800, 600));
        QObject viewModel;
        AnchoredViewFile qml;
        QVERIFY(qml.Open());

        QmlCompositor compositor(&windowRoot);
        compositor.SetTopInset(40.0);
        compositor.Load({{"test.plugin", qml.Url(), &viewModel, 10}});
        QCoreApplication::processEvents();

        const LoadedItems loaded = FindLoadedItems(windowRoot);
        QVERIFY(loaded.viewport);
        QVERIFY(loaded.pluginRoot);
        QCOMPARE(loaded.viewport->position(), QPointF(0, 40));
        QCOMPARE(loaded.viewport->size(), QSizeF(800, 560));
        QCOMPARE(loaded.pluginRoot->position(), QPointF(0, 0));
        QCOMPARE(loaded.pluginRoot->size(), QSizeF(800, 560));
        QCOMPARE(loaded.pluginRoot->mapToScene(QPointF(0, 0)), QPointF(0, 40));
    }

    void AnchoredRootTracksInsetAndWindowChanges()
    {
        QQuickItem windowRoot;
        windowRoot.setSize(QSizeF(640, 480));
        QObject viewModel;
        AnchoredViewFile qml;
        QVERIFY(qml.Open());

        QmlCompositor compositor(&windowRoot);
        compositor.Load({{"test.plugin", qml.Url(), &viewModel, 10}});

        compositor.SetTopInset(36.0);
        windowRoot.setSize(QSizeF(1024, 768));
        QCoreApplication::processEvents();

        LoadedItems loaded = FindLoadedItems(windowRoot);
        QVERIFY(loaded.viewport);
        QVERIFY(loaded.pluginRoot);
        QCOMPARE(loaded.viewport->position(), QPointF(0, 36));
        QCOMPARE(loaded.viewport->size(), QSizeF(1024, 732));
        QCOMPARE(loaded.pluginRoot->size(), loaded.viewport->size());
        QCOMPARE(loaded.pluginRoot->mapToScene(QPointF(0, 0)), QPointF(0, 36));

        // Fullscreen removes the fallback title bar and restores the complete surface.
        compositor.SetTopInset(0.0);
        QCoreApplication::processEvents();
        loaded = FindLoadedItems(windowRoot);
        QCOMPARE(loaded.viewport->position(), QPointF(0, 0));
        QCOMPARE(loaded.viewport->size(), QSizeF(1024, 768));
        QCOMPARE(loaded.pluginRoot->size(), loaded.viewport->size());

        // Leaving fullscreen reapplies the windowed inset.
        compositor.SetTopInset(36.0);
        QCoreApplication::processEvents();
        loaded = FindLoadedItems(windowRoot);
        QCOMPARE(loaded.viewport->position(), QPointF(0, 36));
        QCOMPARE(loaded.viewport->size(), QSizeF(1024, 732));
        QCOMPARE(loaded.pluginRoot->mapToScene(QPointF(0, 0)), QPointF(0, 36));
    }

    void InsetIsClampedToValidViewportGeometry()
    {
        QQuickItem windowRoot;
        windowRoot.setSize(QSizeF(640, 480));
        QmlCompositor compositor(&windowRoot);

        compositor.SetTopInset(-20.0);
        LoadedItems loaded = FindLoadedItems(windowRoot);
        QVERIFY(loaded.viewport);
        QCOMPARE(loaded.viewport->position(), QPointF(0, 0));
        QCOMPARE(loaded.viewport->size(), QSizeF(640, 480));

        compositor.SetTopInset(600.0);
        loaded = FindLoadedItems(windowRoot);
        QCOMPARE(loaded.viewport->position(), QPointF(0, 480));
        QCOMPARE(loaded.viewport->size(), QSizeF(640, 0));
    }
};

namespace
{
const ::framelift::test::Registrar<QmlCompositorTests> kRegisterQmlCompositorTests{"QmlCompositorTests"};
}

#include "QmlCompositorTests.moc"
