#pragma once

#include <QtCore/QObject>

class History;

class HistorySettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)
    Q_PROPERTY(int maxEntries READ MaxEntries WRITE SetMaxEntries NOTIFY changed)

public:
    explicit HistorySettings(History& history);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] bool Dirty() const;
    [[nodiscard]] int MaxEntries() const;

    void SetMaxEntries(int value);

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
    void SeedFromHistory();

    History& history_;
    bool dirty_ = false;
    int maxEntries_ = 200;
};
