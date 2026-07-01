#pragma once

#include <QtCore/QObject>

class Playlist;

class PlaylistSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ Title CONSTANT)
    Q_PROPERTY(bool dirty READ Dirty NOTIFY changed)
    Q_PROPERTY(bool scanSubdirs READ ScanSubdirs WRITE SetScanSubdirs NOTIFY changed)
    Q_PROPERTY(int scanMaxDepth READ ScanMaxDepth WRITE SetScanMaxDepth NOTIFY changed)
    Q_PROPERTY(bool mixedPlaylist READ MixedPlaylist WRITE SetMixedPlaylist NOTIFY changed)
    Q_PROPERTY(bool imageSlideshow READ ImageSlideshow WRITE SetImageSlideshow NOTIFY changed)
    Q_PROPERTY(float slideshowDuration READ SlideshowDuration WRITE SetSlideshowDuration NOTIFY changed)
    Q_PROPERTY(bool autoReload READ AutoReload WRITE SetAutoReload NOTIFY changed)
    Q_PROPERTY(bool sortByName READ SortByName WRITE SetSortByName NOTIFY changed)

public:
    explicit PlaylistSettings(Playlist& playlist);

    [[nodiscard]] QString Title() const;
    [[nodiscard]] bool Dirty() const;
    [[nodiscard]] bool ScanSubdirs() const;
    [[nodiscard]] int ScanMaxDepth() const;
    [[nodiscard]] bool MixedPlaylist() const;
    [[nodiscard]] bool ImageSlideshow() const;
    [[nodiscard]] float SlideshowDuration() const;
    [[nodiscard]] bool AutoReload() const;
    [[nodiscard]] bool SortByName() const;

    void SetScanSubdirs(bool value);
    void SetScanMaxDepth(int value);
    void SetMixedPlaylist(bool value);
    void SetImageSlideshow(bool value);
    void SetSlideshowDuration(float value);
    void SetAutoReload(bool value);
    void SetSortByName(bool value);

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
    void SeedFromPlaylist();
    void MarkDirty();

    Playlist& playlist_;
    bool dirty_ = false;
    bool scanSubdirs_ = true;
    int scanMaxDepth_ = 5;
    bool mixedPlaylist_ = false;
    bool imageSlideshow_ = false;
    float slideshowDuration_ = 5.0f;
    bool autoReload_ = true;
    bool sortByName_ = false;
};
