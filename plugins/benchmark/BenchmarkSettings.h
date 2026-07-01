#pragma once

#include <QtCore/QObject>

class Benchmark;

class BenchmarkSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)
    Q_PROPERTY(bool limitDuration READ LimitDuration WRITE SetLimitDuration NOTIFY changed)
    Q_PROPERTY(float benchmarkDuration READ BenchmarkDuration WRITE SetBenchmarkDuration NOTIFY changed)

public:
    explicit BenchmarkSettings(Benchmark& benchmark);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] bool Dirty() const;
    [[nodiscard]] bool LimitDuration() const;
    [[nodiscard]] float BenchmarkDuration() const;

    void SetLimitDuration(bool value);
    void SetBenchmarkDuration(float value);

    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();
    // Re-seed the editable draft from the live plugin state and clear the dirty
    // flag. Call from QML when the page becomes visible so reopening the settings
    // window never shows a stale or abandoned draft (this is a transactional edit
    // model: edits live in the draft until save() commits them).
    Q_INVOKABLE void load();

Q_SIGNALS:
    void changed();

private:
    void SeedFromBenchmark();
    void MarkDirty();

    Benchmark& benchmark_;
    bool dirty_ = false;
    bool limitDuration_ = false;
    float benchmarkDuration_ = 30.0f;
};
