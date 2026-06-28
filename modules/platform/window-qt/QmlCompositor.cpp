#include "QmlCompositor.h"

#include <framelift/Log.h>

#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>

#include <algorithm>

QmlCompositor::QmlCompositor(QQuickItem* root) : root_(root), engine_(std::make_unique<QQmlEngine>())
{
}

QmlCompositor::~QmlCompositor()
{
    Clear();
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
        if (!root_ || !view.viewModel || view.sourceUrl.isEmpty())
        {
            Log::Warn(
                "QML '{}': skipped (root={}, viewModel={}, source='{}')", view.moduleId.toStdString(),
                root_ ? "yes" : "no", view.viewModel ? "yes" : "no", view.sourceUrl.toStdString()
            );
            continue;
        }

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

        item->setParentItem(root_);
        item->setParent(root_);
        item->setPosition(QPointF(0, 0));
        item->setSize(root_->size());
        // The custom VideoItem is the first child of the window content item and
        // occupies z=0. Keep every plugin surface strictly above it while retaining
        // the plugin render-order relationship.
        item->setZ(1.0 + static_cast<qreal>(view.order));
        QObject::connect(
            root_, &QQuickItem::widthChanged, item,
            [root = root_, item]
            {
                item->setWidth(root->width());
            }
        );
        QObject::connect(
            root_, &QQuickItem::heightChanged, item,
            [root = root_, item]
            {
                item->setHeight(root->height());
            }
        );

        if (component.isError())
        {
            Log::Error("QML '{}': {}", view.moduleId.toStdString(), component.errorString().toStdString());
            delete item;
            continue;
        }

        views_.push_back({std::move(context), item});
        Log::Debug(
            "QML '{}': loaded '{}' at z={} ({}x{})", view.moduleId.toStdString(), view.sourceUrl.toStdString(),
            item->z(), item->width(), item->height()
        );
    }
}
