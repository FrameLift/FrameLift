#include "PlaylistSettings.h"
#include "Playlist.h"

PlaylistSettings::PlaylistSettings(Playlist& playlist) : playlist_(playlist)
{
    SeedFromPlaylist();
}

QString PlaylistSettings::Title() const
{
    return QStringLiteral("Playlist");
}

bool PlaylistSettings::Dirty() const
{
    return dirty_;
}

bool PlaylistSettings::ScanSubdirs() const
{
    return scanSubdirs_;
}

int PlaylistSettings::ScanMaxDepth() const
{
    return scanMaxDepth_;
}

bool PlaylistSettings::MixedPlaylist() const
{
    return mixedPlaylist_;
}

bool PlaylistSettings::ImageSlideshow() const
{
    return imageSlideshow_;
}

float PlaylistSettings::SlideshowDuration() const
{
    return slideshowDuration_;
}

bool PlaylistSettings::AutoReload() const
{
    return autoReload_;
}

void PlaylistSettings::SetScanSubdirs(bool value)
{
    if (scanSubdirs_ != value)
    {
        scanSubdirs_ = value;
        MarkDirty();
    }
}

void PlaylistSettings::SetScanMaxDepth(int value)
{
    if (scanMaxDepth_ != value)
    {
        scanMaxDepth_ = value;
        MarkDirty();
    }
}

void PlaylistSettings::SetMixedPlaylist(bool value)
{
    if (mixedPlaylist_ != value)
    {
        mixedPlaylist_ = value;
        MarkDirty();
    }
}

void PlaylistSettings::SetImageSlideshow(bool value)
{
    if (imageSlideshow_ != value)
    {
        imageSlideshow_ = value;
        MarkDirty();
    }
}

void PlaylistSettings::SetSlideshowDuration(float value)
{
    if (slideshowDuration_ != value)
    {
        slideshowDuration_ = value;
        MarkDirty();
    }
}

void PlaylistSettings::SetAutoReload(bool value)
{
    if (autoReload_ != value)
    {
        autoReload_ = value;
        MarkDirty();
    }
}

void PlaylistSettings::save()
{
    playlist_.ApplySettings(
        scanSubdirs_, scanMaxDepth_, mixedPlaylist_, imageSlideshow_, slideshowDuration_, autoReload_
    );
    dirty_ = false;
    Q_EMIT changed();
}

void PlaylistSettings::reset()
{
    scanSubdirs_ = true;
    scanMaxDepth_ = 5;
    mixedPlaylist_ = false;
    imageSlideshow_ = false;
    slideshowDuration_ = 5.0f;
    autoReload_ = true;
    dirty_ = true;
    Q_EMIT changed();
}

void PlaylistSettings::load()
{
    SeedFromPlaylist();
    Q_EMIT changed();
}

void PlaylistSettings::SeedFromPlaylist()
{
    scanSubdirs_ = playlist_.scanSubdirs_;
    scanMaxDepth_ = playlist_.scanMaxDepth_;
    mixedPlaylist_ = playlist_.mixedPlaylist_;
    imageSlideshow_ = playlist_.imageSlideshow_;
    slideshowDuration_ = playlist_.slideshowDuration_;
    autoReload_ = playlist_.autoReload_;
    dirty_ = false;
}

void PlaylistSettings::MarkDirty()
{
    dirty_ = true;
    Q_EMIT changed();
}
