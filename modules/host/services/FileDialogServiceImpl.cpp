#include "FileDialogServiceImpl.h"
#include "CoreSettings.h" // FilesSettings (video/image extension lists)
#include "Settings.h"     // host aggregate settings
#include <framelift/platform/IAppWindow.h>

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QFileDialog>

#include <memory>
#include <string>

// Qt native open-file picker. The modal QFileDialog runs synchronously on the GUI
// thread, then the chosen path is posted back through the queued custom-event
// round-trip so the caller's callback fires on a later turn — never reentrantly
// inside OpenFile, matching the host/plugin contract.

namespace
{
struct Payload
{
    void (*cb)(const char* path, bool ok, void* ud) = nullptr;
    void* ud = nullptr;
    std::string path;
};

// Build a Qt name-filter clause ("Video files (*.mp4 *.mkv ...)") from a host
// semicolon-separated extension list ("mp4;mkv;..."). Empty input ⇒ empty string.
QString MakeFilter(const char* label, const std::string& extensions)
{
    QStringList globs;
    for (const QString& ext : QString::fromStdString(extensions).split(';', Qt::SkipEmptyParts))
    {
        globs << "*." + ext.trimmed();
    }
    if (globs.isEmpty())
    {
        return {};
    }
    return QString("%1 (%2)").arg(QLatin1String(label), globs.join(' '));
}
} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void FileDialogServiceImpl::Init(IAppWindow* appWindow, IEventPump* events) noexcept
{
    appWindow_ = appWindow;
    events_ = events;
    eventType_ = events->RegisterCustomEventType();
}

void FileDialogServiceImpl::OpenFile(void (*cb)(const char* path, bool ok, void* ud), void* ud) noexcept
{
    if (!appWindow_ || !events_)
    {
        return;
    }

    QStringList filters;
    bool rememberDirectory = false;
    if (settings_)
    {
        const FilesSettings& files = settings_->Get<FilesSettings>();
        rememberDirectory = files.rememberOpenDialogDirectory;
        if (const QString f = MakeFilter("Video files", files.videoExtensions); !f.isEmpty())
        {
            filters << f;
        }
        if (const QString f = MakeFilter("Image files", files.imageExtensions); !f.isEmpty())
        {
            filters << f;
        }
    }
    filters << "All files (*)";

    // Modal native picker — blocks on a nested event loop until the user chooses
    // or cancels. Cancel returns an empty string ⇒ ok=false downstream.
    const QString initialDirectory = rememberDirectory ? state_.FileDialogLastDirectory() : QString();
    const QString chosen = QFileDialog::getOpenFileName(nullptr, "Open File", initialDirectory, filters.join(";;"));
    if (rememberDirectory && !chosen.isEmpty())
    {
        state_.SaveFileDialogLastDirectoryForFile(chosen);
    }

    // Defer delivery through the event loop so the callback never fires reentrantly.
    auto* p = new Payload{cb, ud, chosen.toStdString()};
    events_->PushCustomEvent(eventType_, p);
}

bool FileDialogServiceImpl::HandleEvent(const AppEvent& e) noexcept
{
    if (e.type != AppEventType::Custom)
    {
        return false;
    }
    const AppEvent::CustomPayload& cp = e.AsCustom();
    if (cp.eventType != eventType_)
    {
        return false;
    }

    const std::unique_ptr<Payload> p(static_cast<Payload*>(cp.userData1));
    if (p->cb)
    {
        p->cb(p->path.c_str(), !p->path.empty(), p->ud);
    }
    return true;
}
