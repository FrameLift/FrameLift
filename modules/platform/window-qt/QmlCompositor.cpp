#include "QmlCompositor.h"

#include <framelift/Log.h>

#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>

#include <algorithm>
#include <string>
#include <utility>

namespace
{
class PerfScope
{
public:
    explicit PerfScope(std::string name) : name_(std::move(name))
    {
        FRAMELIFT_PERF_START(name_.c_str());
    }

    ~PerfScope()
    {
        FRAMELIFT_PERF_END(name_.c_str());
    }

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    std::string name_;
};
} // namespace

QmlCompositor::QmlCompositor(QQuickItem* root) : root_(root), engine_(std::make_unique<QQmlEngine>())
{
    if (!root_)
    {
        return;
    }

    // Plugin roots commonly use `anchors.fill: parent`. Parent them to a dedicated
    // viewport so those anchors resolve against the usable area below the fallback
    // title bar instead of overriding the compositor's inset against the window root.
    viewport_ = new QQuickItem(root_);
    viewport_->setParent(root_);
    viewport_->setClip(true);
    viewport_->setZ(1.0); // above the video item; the title bar remains at z=10000
    SyncViewportGeometry();

    QObject::connect(
        root_, &QQuickItem::widthChanged, viewport_,
        [this]
        {
            SyncViewportGeometry();
        }
    );
    QObject::connect(
        root_, &QQuickItem::heightChanged, viewport_,
        [this]
        {
            SyncViewportGeometry();
        }
    );
}

QmlCompositor::~QmlCompositor()
{
    Clear();
    delete viewport_;
    viewport_ = nullptr;
}

void QmlCompositor::Clear()
{
    for (auto& view : views_)
    {
        delete view.item;
        view.item = nullptr;
    }
    views_.clear();
}

void QmlCompositor::Load(std::vector<QmlViewSpec> views)
{
    Clear();

    std::stable_sort(
        views.begin(), views.end(),
        [](const QmlViewSpec& a, const QmlViewSpec& b)
        {
            return a.order < b.order;
        }
    );

    for (const QmlViewSpec& view : views)
    {
        if (!viewport_ || !view.viewModel || view.sourceUrl.isEmpty())
        {
            Log::Warn(
                "QML '{}': skipped (root={}, viewModel={}, source='{}')", view.moduleId.toStdString(),
                root_ ? "yes" : "no", view.viewModel ? "yes" : "no", view.sourceUrl.toStdString()
            );
            continue;
        }

        PerfScope perf("qml-load:" + view.moduleId.toStdString());
        auto context = std::make_unique<QQmlContext>(engine_->rootContext());

        QQmlComponent component(engine_.get(), QUrl(view.sourceUrl));
        const QVariantMap initialProperties{{QStringLiteral("viewModel"), QVariant::fromValue(view.viewModel)}};
        QObject* object = component.createWithInitialProperties(initialProperties, context.get());
        if (!object)
        {
            Log::Error("QML '{}': {}", view.moduleId.toStdString(), component.errorString().toStdString());
            continue;
        }

        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item)
        {
            Log::Error("QML '{}': root object is not a QQuickItem", view.moduleId.toStdString());
            delete object;
            continue;
        }

        item->setParentItem(viewport_);
        item->setParent(viewport_);
        item->setPosition(QPointF(0, 0));
        item->setSize(viewport_->size());
        // Keep the plugin render-order relationship within the shared viewport.
        item->setZ(1.0 + static_cast<qreal>(view.order));
        QObject::connect(
            viewport_, &QQuickItem::widthChanged, item,
            [viewport = viewport_, item]
            {
                item->setWidth(viewport->width());
            }
        );
        QObject::connect(
            viewport_, &QQuickItem::heightChanged, item,
            [viewport = viewport_, item]
            {
                item->setHeight(viewport->height());
            }
        );

        views_.push_back({std::move(context), item});
        Log::Debug(
            "QML '{}': loaded '{}' at z={} ({}x{})", view.moduleId.toStdString(), view.sourceUrl.toStdString(),
            item->z(), item->width(), item->height()
        );
    }
}

void QmlCompositor::SetTopInset(qreal inset)
{
    const qreal clampedInset = std::max<qreal>(0.0, inset);
    if (clampedInset == topInset_)
    {
        return;
    }
    topInset_ = clampedInset;
    SyncViewportGeometry();
}

void QmlCompositor::SyncViewportGeometry()
{
    if (!root_ || !viewport_)
    {
        return;
    }

    const qreal rootWidth = std::max<qreal>(0.0, root_->width());
    const qreal rootHeight = std::max<qreal>(0.0, root_->height());
    const qreal effectiveInset = std::min(topInset_, rootHeight);
    viewport_->setPosition(QPointF(0, effectiveInset));
    viewport_->setSize(QSizeF(rootWidth, rootHeight - effectiveInset));
}
