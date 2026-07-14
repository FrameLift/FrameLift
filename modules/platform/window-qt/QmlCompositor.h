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
// to an inset viewport over the video scene-graph item. A broken component is
// isolated to its plugin.
class QmlCompositor final
{
public:
    explicit QmlCompositor(QQuickItem* root);
    ~QmlCompositor();

    void Clear();
    void Load(std::vector<QmlViewSpec> views);

    // Reserve a top strip (logical px) that plugin surfaces must not cover — the
    // fallback title bar. Applies to already-loaded views and to later Load()s;
    // 0 restores full-window surfaces (fullscreen hides the bar).
    void SetTopInset(qreal inset);

private:
    struct LoadedView
    {
        std::unique_ptr<QQmlContext> context;
        QQuickItem* item = nullptr;
    };

    QQuickItem* root_ = nullptr;
    QQuickItem* viewport_ = nullptr;
    std::unique_ptr<QQmlEngine> engine_;
    std::vector<LoadedView> views_;
    qreal topInset_ = 0.0;

    void SyncViewportGeometry();
};
