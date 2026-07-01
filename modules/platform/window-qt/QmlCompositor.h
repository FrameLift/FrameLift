#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include <memory>
#include <vector>

class QQmlContext;
class QQmlEngine;
class QQuickItem;

struct QmlViewSpec
{
    QString moduleId;
    QString sourceUrl;
    QObject* viewModel = nullptr;
    int order = 0;
};

// Loads plugin-owned QML components into one engine and parents their root items
// over the video scene-graph item. A broken component is isolated to its plugin.
class QmlCompositor final
{
public:
    explicit QmlCompositor(QQuickItem* root);
    ~QmlCompositor();

    void Clear();
    void Load(std::vector<QmlViewSpec> views);

private:
    struct LoadedView
    {
        std::unique_ptr<QQmlContext> context;
        QQuickItem* item = nullptr;
    };

    QQuickItem* root_ = nullptr;
    std::unique_ptr<QQmlEngine> engine_;
    std::vector<LoadedView> views_;
};
