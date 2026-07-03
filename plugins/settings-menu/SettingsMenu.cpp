#include "SettingsMenu.h"

#include "KeybindList.h"
#include <framelift/Hotkeys.h>

#include <QtCore/QFile>
#include <QtCore/QMetaObject>
#include <QtCore/QSet>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtCore/Qt>
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct CoreBindEntry
{
    const char* label;
    const char* name;
    const char* key;
};

constexpr CoreBindEntry kCoreKeybinds[] = {
    {"Toggle pause", "togglePause", "keybinds.togglePause"},
    {"Toggle fullscreen", "toggleFullscreen", "keybinds.toggleFullscreen"},
    {"Quit", "quit", "keybinds.quit"},
    {"Volume up", "volumeUp", "keybinds.volumeUp"},
    {"Volume down", "volumeDown", "keybinds.volumeDown"},
    {"Toggle mute", "toggleMute", "keybinds.toggleMute"},
    {"Seek forward", "seekForward", "keybinds.seekForward"},
    {"Seek back", "seekBack", "keybinds.seekBack"},
    {"Seek forward (long)", "seekForwardLong", "keybinds.seekForwardLong"},
    {"Seek back (long)", "seekBackLong", "keybinds.seekBackLong"},
    {"Toggle normalize", "toggleNormalize", "keybinds.toggleNormalize"},
    {"Toggle subtitles", "toggleSubtitles", "keybinds.toggleSubtitles"},
    {"Open file dialog", "openFileDialog", "keybinds.openFileDialog"},
};

constexpr const char* kPluginsPageQml = "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/SettingsPluginsPage.qml";
constexpr const char* kAdvancedPageQml = "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/AdvancedSettings.qml";

// Sort weight for the three nav groups so they render contiguously: core config
// first, plugin-contributed pages next, the settings-menu's own management pages last.
int GroupRank(const QString& group)
{
    if (group == QStringLiteral("core"))
    {
        return 0;
    }
    if (group == QStringLiteral("system"))
    {
        return 2;
    }
    return 1; // "plugin"
}

const char* SettingsPageQmlForSection(const std::string& id)
{
    if (id == "audio")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/AudioSettings.qml";
    }
    if (id == "cache")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/CacheSettings.qml";
    }
    if (id == "files")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/FilesSettings.qml";
    }
    if (id == "keybinds")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/KeybindSettings.qml";
    }
    if (id == "playback")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/PlaybackSettings.qml";
    }
    if (id == "subtitles")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/SubtitleSettings.qml";
    }
    if (id == "theme")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/ThemeSettings.qml";
    }
    if (id == "ui")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/UISettings.qml";
    }
    return nullptr;
}

QString PageTitleFromId(const std::string& id)
{
    // Display names for sections whose pretty title isn't just a capitalised id.
    if (id == "ui")
    {
        return QStringLiteral("UI");
    }
    QString title = QString::fromStdString(id);
    if (title.isEmpty())
    {
        return title;
    }
    title[0] = title[0].toUpper();
    return title;
}
} // namespace

void EditModel::Clear()
{
    bools_.clear();
    ints_.clear();
    floats_.clear();
    strings_.clear();
}

bool& EditModel::Bool(const std::string& key)
{
    return bools_[key];
}

int& EditModel::Int(const std::string& key)
{
    return ints_[key];
}

float& EditModel::Float(const std::string& key)
{
    return floats_[key];
}

std::string& EditModel::Str(const std::string& key)
{
    return strings_[key];
}

SettingsSectionPageModel::SettingsSectionPageModel(SettingsMenu& owner, QString id, QString title)
    : owner_(owner), id_(std::move(id)), title_(std::move(title))
{
    // Re-seeding the draft (open, save, reset) happens on the owner; relay it as a
    // page-level change so hand-authored field bindings re-read their values.
    QObject::connect(&owner_, &SettingsMenu::qmlChanged, this, &SettingsSectionPageModel::changed);
}

QString SettingsSectionPageModel::Title() const
{
    return title_;
}

QVariantList SettingsSectionPageModel::Fields()
{
    return owner_.fieldsForPage(id_);
}

bool SettingsSectionPageModel::Dirty() const
{
    return owner_.Dirty();
}

QVariant SettingsSectionPageModel::fieldValue(const QString& key) const
{
    return owner_.FieldValue(key);
}

void SettingsSectionPageModel::setFieldValue(const QString& key, const QVariant& value)
{
    owner_.setFieldValue(key, value);
    Q_EMIT changed();
}

void SettingsSectionPageModel::save()
{
    owner_.saveActivePage();
    Q_EMIT changed();
}

void SettingsSectionPageModel::reset()
{
    owner_.resetActivePage();
    Q_EMIT changed();
}

SettingsPluginsPageModel::SettingsPluginsPageModel(SettingsMenu& owner) : owner_(owner)
{
}

QString SettingsPluginsPageModel::Title() const
{
    return QStringLiteral("Plugins");
}

QVariantList SettingsPluginsPageModel::Plugins() const
{
    return owner_.pluginsModel();
}

bool SettingsPluginsPageModel::Dirty() const
{
    return false;
}

void SettingsPluginsPageModel::setPluginEnabled(const QString& pluginId, bool enabled)
{
    owner_.setPluginEnabled(pluginId, enabled);
    Q_EMIT changed();
}

void SettingsPluginsPageModel::save()
{
}

void SettingsPluginsPageModel::reset()
{
}

SettingsAdvancedPageModel::SettingsAdvancedPageModel(SettingsMenu& owner) : owner_(owner)
{
}

QString SettingsAdvancedPageModel::Title() const
{
    return QStringLiteral("Advanced");
}

QString SettingsAdvancedPageModel::FilePath() const
{
    return QString::fromStdString(owner_.SettingsFilePath());
}

QString SettingsAdvancedPageModel::Text() const
{
    return text_;
}

bool SettingsAdvancedPageModel::Dirty() const
{
    return dirty_;
}

void SettingsAdvancedPageModel::load()
{
    text_.clear();
    const QString path = QString::fromStdString(owner_.SettingsFilePath());
    if (!path.isEmpty())
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            text_ = QString::fromUtf8(file.readAll());
        }
    }
    dirty_ = false;
    Q_EMIT changed();
}

void SettingsAdvancedPageModel::setText(const QString& text)
{
    if (text == text_)
    {
        return;
    }
    text_ = text;
    dirty_ = true;
    Q_EMIT changed();
}

void SettingsAdvancedPageModel::save()
{
    const QString path = QString::fromStdString(owner_.SettingsFilePath());
    if (path.isEmpty())
    {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        return;
    }
    file.write(text_.toUtf8());
    file.close();
    // Mandatory: without a reload the in-memory settings stay stale and the next Save
    // from any other page would overwrite these hand edits.
    owner_.ReloadSettingsFromDisk();
    dirty_ = false;
    Q_EMIT changed();
}

void SettingsAdvancedPageModel::reset()
{
    load();
}

KeybindsPageModel::KeybindsPageModel(SettingsMenu& owner) : owner_(owner)
{
    // Relay owner-level changes (open/save/reset/capture) so the page re-reads entries,
    // capture state and conflict text.
    QObject::connect(&owner_, &SettingsMenu::qmlChanged, this, &KeybindsPageModel::changed);
}

QString KeybindsPageModel::Title() const
{
    return QStringLiteral("Keybinds");
}

QVariantList KeybindsPageModel::CoreEntries() const
{
    return owner_.CoreKeybindEntries();
}

QVariantList KeybindsPageModel::PluginGroups() const
{
    return owner_.PluginKeybindGroups();
}

bool KeybindsPageModel::Dirty() const
{
    return owner_.Dirty();
}

bool KeybindsPageModel::Capturing() const
{
    return owner_.Capturing();
}

QString KeybindsPageModel::CapturingAction() const
{
    return owner_.CapturingActionName();
}

int KeybindsPageModel::CapturingSlot() const
{
    return owner_.CapturingSlot();
}

QString KeybindsPageModel::Conflict() const
{
    return owner_.KeybindConflict();
}

void KeybindsPageModel::beginCapture(const QString& action, int slot)
{
    owner_.BeginCapture(action, slot);
}

void KeybindsPageModel::captureKey(int qtKey, int qtMods)
{
    owner_.CaptureKey(qtKey, qtMods);
}

void KeybindsPageModel::cancelCapture()
{
    owner_.CancelCapture();
}

void KeybindsPageModel::clearSlot(const QString& action, int slot)
{
    owner_.ClearKeybindSlot(action, slot);
}

void KeybindsPageModel::save()
{
    owner_.saveActivePage();
}

void KeybindsPageModel::reset()
{
    owner_.resetActivePage();
}

const char* SettingsMenu::ModuleName() const
{
    return "SettingsMenu";
}

std::vector<framelift::Keybind> SettingsMenu::Keybinds()
{
    return {
        {"Open settings", "openSettings", &openSettingsKey_, "Ctrl+Comma", [this]
         {
             Open();
         }}
    };
}

void SettingsMenu::OnInstall(IModuleContext& ctx)
{
    SeedFromContext();
    RegisterBuiltInPages();

    // Invalidate the QmlPages cache on every change to the pages projection.
    QObject::connect(
        this, &SettingsMenu::qmlChanged, this,
        [this]
        {
            pagesCacheDirty_ = true;
        }
    );

    framelift::Subscribe<OpenSettingsPageEvent>(
        ctx,
        [this](const OpenSettingsPageEvent& e)
        {
            OpenPage(e.pageId);
        }
    );

    if (auto* menu = ctx.GetService<ContextMenu>())
    {
        framelift::AddSection(
            *menu,
            [this](ContextMenu& m)
            {
                m.AddSeparator();
                // Audio/Subtitle settings are reachable from the context menu's
                // Audio and Subtitle submenus (owned by the ContextMenu plugin,
                // which publishes OpenSettingsPageEvent). Only the top-level
                // Settings entry is contributed here.
                framelift::AddItem(
                    m, "Settings", "openSettings",
                    [this]
                    {
                        OpenPage(nullptr);
                    }
                );
            }
        );
    }
}

ISettingsStore* SettingsMenu::SettingsStore() const
{
    return ctx_ ? ctx_->GetService<ISettingsStore>() : nullptr;
}

ISettingsRegistry* SettingsMenu::SettingsReg() const
{
    return ctx_ ? ctx_->GetService<ISettingsRegistry>() : nullptr;
}

ISettingsPageRegistry* SettingsMenu::SettingsPageReg() const
{
    return ctx_ ? ctx_->GetService<ISettingsPageRegistry>() : nullptr;
}

IPluginCatalog* SettingsMenu::PluginCatalog() const
{
    return ctx_ ? ctx_->GetService<IPluginCatalog>() : nullptr;
}

void SettingsMenu::SeedHostValue(const FieldMeta& f)
{
    auto* store = SettingsStore();
    if (!store)
    {
        return;
    }
    switch (f.type)
    {
    case 0:
        model_.Bool(f.key) = store->GetSettingBool(f.key.c_str());
        break;
    case 1:
        model_.Int(f.key) = store->GetSettingInt(f.key.c_str());
        break;
    case 2:
        model_.Float(f.key) = store->GetSettingFloat(f.key.c_str());
        break;
    case 3: {
        const int n = store->GetSettingString(f.key.c_str(), nullptr, 0);
        std::string value(static_cast<std::size_t>(n), '\0');
        if (n > 0)
        {
            store->GetSettingString(f.key.c_str(), value.data(), n + 1);
        }
        model_.Str(f.key) = std::move(value);
        break;
    }
    default:
        break;
    }
}

void SettingsMenu::SeedModuleValue(const FieldMeta& f)
{
    const char* value = f.getValue ? f.getValue(f.ud) : "";
    switch (f.type)
    {
    case 0:
        model_.Bool(f.key) = value && (std::string(value) == "1" || std::string(value) == "true");
        break;
    case 1:
        model_.Int(f.key) = value ? std::stoi(value) : 0;
        break;
    case 2:
        model_.Float(f.key) = value ? std::stof(value) : 0.f;
        break;
    case 3:
        model_.Str(f.key) = value ? value : "";
        break;
    default:
        break;
    }
}

void SettingsMenu::SeedFromContext()
{
    fields_.clear();
    model_.Clear();
    auto* registry = SettingsReg();
    if (!registry)
    {
        return;
    }

    registry->EnumerateSettings(
        [](const FrameLiftSettingDesc* d, void* ud)
        {
            auto* self = static_cast<SettingsMenu*>(ud);
            FieldMeta field{
                d->key ? d->key : "", d->type, d->desc ? d->desc : "", d->defaultValue ? d->defaultValue : ""
            };
            self->SeedHostValue(field);
            self->fields_.push_back(std::move(field));
        },
        this
    );

    registry->EnumerateModuleSettings(
        [](const FrameLiftModuleSettingDesc* d, void* ud)
        {
            auto* self = static_cast<SettingsMenu*>(ud);
            FieldMeta field{d->key ? d->key : "",
                            d->type,
                            d->desc ? d->desc : "",
                            d->defaultValue ? d->defaultValue : "",
                            true,
                            d->getValue,
                            d->setValue,
                            d->ud};
            self->SeedModuleValue(field);
            self->fields_.push_back(std::move(field));
        },
        this
    );

    SeedPluginKeybinds();
    dirty_ = false;
}

void SettingsMenu::SeedPluginKeybinds()
{
    pluginKeybinds_.clear();
    auto* registry = SettingsReg();
    if (!registry)
    {
        return;
    }
    registry->EnumerateKeybindEntries(
        [](const char* label, const char* actionName, const char* (*getStr)(void*), void (*setStr)(void*, const char*),
           void* ud, const char* group, const char* defaultBind, void* visitUd)
        {
            auto* self = static_cast<SettingsMenu*>(visitUd);
            PluginKeybindDraft d;
            d.label = label ? label : "";
            d.action = actionName ? actionName : "";
            d.group = group && *group ? group : "Plugin";
            d.defaultBind = defaultBind ? defaultBind : "";
            d.getStr = getStr;
            d.setStr = setStr;
            d.ud = ud;
            d.draft = getStr ? getStr(ud) : "";
            self->pluginKeybinds_.push_back(std::move(d));
        },
        this
    );
}

void SettingsMenu::RefreshFields()
{
    if (!dirty_)
    {
        SeedFromContext();
    }
}

bool SettingsMenu::IsOpen() const noexcept
{
    return open_;
}

bool SettingsMenu::Dirty() const noexcept
{
    return dirty_;
}

const char* SettingsMenu::PageGroup(const std::string& id) const
{
    if (id == "plugins" || id == "advanced")
    {
        return "system";
    }
    if (corePageIds_.contains(id))
    {
        return "core";
    }
    return "plugin";
}

QVariantList SettingsMenu::QmlPages()
{
    if (!pagesCacheDirty_)
    {
        return pagesCache_;
    }
    QVariantList result;
    auto* registry = SettingsPageReg();
    if (!registry)
    {
        return result; // leave the cache dirty; rebuild once a registry is available
    }

    registry->EnumerateSettingsPages(
        [](const FrameLiftSettingsPageDesc* page, void* ud)
        {
            auto* pages = static_cast<QVariantList*>(ud);
            QVariantMap row;
            row.insert(QStringLiteral("id"), QString::fromUtf8(page->id ? page->id : ""));
            row.insert(QStringLiteral("title"), QString::fromUtf8(page->title ? page->title : ""));
            row.insert(QStringLiteral("qmlUrl"), QString::fromUtf8(page->qmlUrl ? page->qmlUrl : ""));
            row.insert(QStringLiteral("viewModel"), QVariant::fromValue(page->viewModel));
            row.insert(QStringLiteral("order"), page->order);
            pages->push_back(row);
        },
        &result
    );
    // Tag each page with its nav group and sort groups contiguously (core → plugin →
    // system), keeping registration order within a group. This makes the grouping
    // independent of the order values plugins happen to pick.
    for (QVariant& entry : result)
    {
        QVariantMap row = entry.toMap();
        row.insert(
            QStringLiteral("group"),
            QString::fromUtf8(PageGroup(row.value(QStringLiteral("id")).toString().toStdString()))
        );
        entry = row;
    }
    std::stable_sort(
        result.begin(), result.end(),
        [](const QVariant& a, const QVariant& b)
        {
            const QVariantMap ma = a.toMap();
            const QVariantMap mb = b.toMap();
            const int ra = GroupRank(ma.value(QStringLiteral("group")).toString());
            const int rb = GroupRank(mb.value(QStringLiteral("group")).toString());
            if (ra != rb)
            {
                return ra < rb;
            }
            return ma.value(QStringLiteral("order")).toInt() < mb.value(QStringLiteral("order")).toInt();
        }
    );
    if (qmlActivePage_.empty() && !result.empty())
    {
        qmlActivePage_ = result.front().toMap().value(QStringLiteral("id")).toString().toStdString();
    }
    pagesCache_ = std::move(result);
    pagesCacheDirty_ = false;
    return pagesCache_;
}

QVariantMap SettingsMenu::ActivePageRecord() const
{
    QVariantMap first;
    auto* registry = SettingsPageReg();
    if (!registry)
    {
        return {};
    }

    struct Ctx
    {
        const std::string* active;
        QVariantMap first;
        QVariantMap match;
    };

    Ctx ctx{&qmlActivePage_, {}, {}};
    registry->EnumerateSettingsPages(
        [](const FrameLiftSettingsPageDesc* page, void* ud)
        {
            auto& ctx = *static_cast<Ctx*>(ud);
            QVariantMap row;
            row.insert(QStringLiteral("id"), QString::fromUtf8(page->id ? page->id : ""));
            row.insert(QStringLiteral("title"), QString::fromUtf8(page->title ? page->title : ""));
            row.insert(QStringLiteral("qmlUrl"), QString::fromUtf8(page->qmlUrl ? page->qmlUrl : ""));
            row.insert(QStringLiteral("viewModel"), QVariant::fromValue(page->viewModel));
            row.insert(QStringLiteral("order"), page->order);
            if (ctx.first.isEmpty())
            {
                ctx.first = row;
            }
            if (!ctx.active->empty() && *ctx.active == row.value(QStringLiteral("id")).toString().toStdString())
            {
                ctx.match = row;
            }
        },
        &ctx
    );
    return ctx.match.isEmpty() ? ctx.first : ctx.match;
}

QString SettingsMenu::ActivePage() const
{
    return QString::fromStdString(qmlActivePage_);
}

void SettingsMenu::SetActivePage(const QString& page)
{
    const std::string next = page.toStdString();
    if (next != qmlActivePage_)
    {
        qmlActivePage_ = next;
        ReseedActivePage();
        Q_EMIT qmlChanged();
    }
}

void SettingsMenu::ReseedActivePage()
{
    QObject* vm = ActivePageViewModel();
    // Skip pages whose model has no "load" slot (pure-QML pages, or the built-in
    // host pages that read live store values directly) — invokeMethod would warn
    // on a missing method, so check the meta-object first.
    if (vm && vm->metaObject()->indexOfMethod("load()") >= 0)
    {
        QMetaObject::invokeMethod(vm, "load", Qt::DirectConnection);
    }
}

QString SettingsMenu::ActivePageUrl() const
{
    return ActivePageRecord().value(QStringLiteral("qmlUrl")).toString();
}

QObject* SettingsMenu::ActivePageViewModel() const
{
    // Non-owning pointer borrowed from the page registry. Safe to hand to QML
    // because RegisterSettingsPage requires the view model to outlive the settings
    // UI (see ISettingsPageRegistry) and pages are never unregistered at runtime.
    // Returns nullptr for an unknown/empty active page (pure-QML pages), which the
    // QML binding handles gracefully.
    return ActivePageRecord().value(QStringLiteral("viewModel")).value<QObject*>();
}

QVariantList SettingsMenu::fieldsForPage(const QString& pageId)
{
    RefreshFields();
    QVariantList result;
    const std::string prefix = pageId.toStdString() + ".";
    for (const FieldMeta& field : fields_)
    {
        if (!field.key.starts_with(prefix))
        {
            continue;
        }
        QVariantMap row;
        row.insert(QStringLiteral("key"), QString::fromStdString(field.key));
        row.insert(QStringLiteral("label"), QString::fromStdString(field.key.substr(prefix.size())));
        row.insert(QStringLiteral("description"), QString::fromStdString(field.desc));
        row.insert(QStringLiteral("type"), field.type);
        switch (field.type)
        {
        case 0:
            row.insert(QStringLiteral("value"), model_.Bool(field.key));
            break;
        case 1:
            row.insert(QStringLiteral("value"), model_.Int(field.key));
            break;
        case 2:
            row.insert(QStringLiteral("value"), model_.Float(field.key));
            break;
        case 3:
            row.insert(QStringLiteral("value"), QString::fromStdString(model_.Str(field.key)));
            break;
        default:
            break;
        }
        result.push_back(row);
    }
    return result;
}

QVariantList SettingsMenu::pluginsModel() const
{
    QVariantList result;
    auto* catalog = PluginCatalog();
    if (!catalog)
    {
        return result;
    }

    catalog->EnumeratePlugins(
        [](const char* pluginId, const char* displayName, const int* version, const char* publisher,
           const char* description, bool enabled, bool loaded, bool loadFailed, void* ud)
        {
            auto* out = static_cast<QVariantList*>(ud);
            QVariantMap row;
            row.insert(QStringLiteral("id"), QString::fromUtf8(pluginId ? pluginId : ""));
            row.insert(QStringLiteral("name"), QString::fromUtf8(displayName ? displayName : ""));
            row.insert(QStringLiteral("publisher"), QString::fromUtf8(publisher ? publisher : ""));
            row.insert(QStringLiteral("description"), QString::fromUtf8(description ? description : ""));
            row.insert(QStringLiteral("enabled"), enabled);
            row.insert(QStringLiteral("loaded"), loaded);
            row.insert(QStringLiteral("loadFailed"), loadFailed);
            if (version)
            {
                row.insert(
                    QStringLiteral("version"),
                    QStringLiteral("%1.%2.%3").arg(version[0]).arg(version[1]).arg(version[2])
                );
            }
            out->push_back(row);
        },
        &result
    );
    return result;
}

void SettingsMenu::RegisterBuiltInPages()
{
    auto* registry = SettingsPageReg();
    if (!registry)
    {
        return;
    }

    pageModels_.clear();
    corePageIds_.clear();
    QSet<QString> seen;
    int order = 10;
    for (const FieldMeta& field : fields_)
    {
        if (field.moduleOwned)
        {
            continue;
        }
        const auto dot = field.key.find('.');
        const std::string section = field.key.substr(0, dot);
        const QString id = QString::fromStdString(section);
        if (seen.contains(id))
        {
            continue;
        }
        seen.insert(id);
        const char* qmlUrl = SettingsPageQmlForSection(section);
        if (!qmlUrl)
        {
            continue;
        }
        QObject* modelPtr = nullptr;
        if (section == "keybinds")
        {
            // The Keybinds page has a bespoke capture UI + per-plugin grouping, so it
            // uses KeybindsPageModel rather than the generic field renderer.
            auto model = std::make_unique<KeybindsPageModel>(*this);
            modelPtr = model.get();
            pageModels_.push_back(std::move(model));
        }
        else
        {
            auto model = std::make_unique<SettingsSectionPageModel>(*this, id, PageTitleFromId(section));
            modelPtr = model.get();
            pageModels_.push_back(std::move(model));
        }
        registry->RegisterSettingsPage(
            section.c_str(), PageTitleFromId(section).toUtf8().constData(), qmlUrl, modelPtr, order
        );
        corePageIds_.insert(section);
        order += 10;
    }

    if (PluginCatalog())
    {
        auto model = std::make_unique<SettingsPluginsPageModel>(*this);
        QObject* modelPtr = model.get();
        pageModels_.push_back(std::move(model));
        registry->RegisterSettingsPage("plugins", "Plugins", kPluginsPageQml, modelPtr, 900);
    }

    // Advanced raw settings.ini editor — pinned to the bottom "System" group.
    {
        auto model = std::make_unique<SettingsAdvancedPageModel>(*this);
        QObject* modelPtr = model.get();
        pageModels_.push_back(std::move(model));
        registry->RegisterSettingsPage("advanced", "Advanced", kAdvancedPageQml, modelPtr, 910);
    }
}

void SettingsMenu::setFieldValue(const QString& key, const QVariant& value)
{
    const std::string fieldKey = key.toStdString();
    const auto it = std::ranges::find_if(
        fields_,
        [&](const FieldMeta& field)
        {
            return field.key == fieldKey;
        }
    );
    if (it == fields_.end())
    {
        return;
    }
    switch (it->type)
    {
    case 0:
        model_.Bool(fieldKey) = value.toBool();
        break;
    case 1:
        model_.Int(fieldKey) = value.toInt();
        break;
    case 2:
        model_.Float(fieldKey) = value.toFloat();
        break;
    case 3:
        model_.Str(fieldKey) = value.toString().toStdString();
        break;
    default:
        return;
    }
    dirty_ = true;
    Q_EMIT qmlChanged();
}

void SettingsMenu::setPluginEnabled(const QString& pluginId, bool enabled)
{
    auto* catalog = PluginCatalog();
    if (!catalog)
    {
        return;
    }
    catalog->SetPluginEnabled(pluginId.toUtf8().constData(), enabled);
    Q_EMIT qmlChanged();
}

void SettingsMenu::saveActivePage()
{
    Save();
}

void SettingsMenu::resetActivePage()
{
    Reset();
}

void SettingsMenu::resetActivePageOnly()
{
    const std::string page = qmlActivePage_;
    const std::string prefix = page + ".";
    const bool hasFields = std::ranges::any_of(
        fields_,
        [&](const FieldMeta& f)
        {
            return f.key.starts_with(prefix);
        }
    );
    if (!hasFields)
    {
        // Non field-backed page: defer to its own reset (Advanced reloads from disk,
        // Plugins is a no-op).
        if (QObject* vm = ActivePageViewModel())
        {
            QMetaObject::invokeMethod(vm, "reset");
        }
        return;
    }
    for (const auto& f : fields_)
    {
        if (f.key.starts_with(prefix))
        {
            ResetValue(f);
        }
    }
    // The Keybinds page's plugin entries live outside fields_; reset their drafts too.
    if (page == "keybinds")
    {
        for (auto& d : pluginKeybinds_)
        {
            d.draft = d.defaultBind;
        }
    }
    dirty_ = true;
    Q_EMIT qmlChanged();
}

void SettingsMenu::Save()
{
    auto* store = SettingsStore();
    if (!store)
    {
        return;
    }
    for (const auto& f : fields_)
    {
        if (f.moduleOwned)
        {
            if (!f.setValue)
            {
                continue;
            }
            switch (f.type)
            {
            case 0:
                f.setValue(f.ud, model_.Bool(f.key) ? "1" : "0");
                break;
            case 1: {
                const std::string value = std::to_string(model_.Int(f.key));
                f.setValue(f.ud, value.c_str());
                break;
            }
            case 2: {
                const std::string value = std::to_string(model_.Float(f.key));
                f.setValue(f.ud, value.c_str());
                break;
            }
            case 3:
                f.setValue(f.ud, model_.Str(f.key).c_str());
                break;
            default:
                break;
            }
            continue;
        }

        switch (f.type)
        {
        case 0:
            store->CommitSettingBool(f.key.c_str(), model_.Bool(f.key));
            break;
        case 1:
            store->CommitSettingInt(f.key.c_str(), model_.Int(f.key));
            break;
        case 2:
            store->CommitSettingFloat(f.key.c_str(), model_.Float(f.key));
            break;
        case 3:
            store->CommitSettingString(f.key.c_str(), model_.Str(f.key).c_str());
            break;
        default:
            break;
        }
    }
    store->SaveSettings();

    // Apply keybind drafts to the live Hotkeys (deferred from capture so all settings
    // changes take effect on Save). Core values were just committed above; plugin
    // values are written back through their setter (the plugin persists on shutdown).
    if (auto* hk = ctx_ ? ctx_->GetService<Hotkeys>() : nullptr)
    {
        for (const auto& e : kCoreKeybinds)
        {
            hk->RebindList(e.name, model_.Str(e.key).c_str());
        }
        for (auto& d : pluginKeybinds_)
        {
            if (d.setStr)
            {
                d.setStr(d.ud, d.draft.c_str());
            }
            hk->RebindList(d.action.c_str(), d.draft.c_str());
        }
    }

    dirty_ = false;
    Q_EMIT qmlChanged();
}

void SettingsMenu::ResetValue(const FieldMeta& f)
{
    switch (f.type)
    {
    case 0:
        model_.Bool(f.key) = (f.defaultValue == "1" || f.defaultValue == "true");
        break;
    case 1:
        model_.Int(f.key) = std::stoi(f.defaultValue);
        break;
    case 2:
        model_.Float(f.key) = std::stof(f.defaultValue);
        break;
    case 3:
        model_.Str(f.key) = f.defaultValue;
        break;
    default:
        break;
    }
}

void SettingsMenu::Reset()
{
    for (const auto& f : fields_)
    {
        ResetValue(f);
    }
    for (auto& d : pluginKeybinds_)
    {
        d.draft = d.defaultBind;
    }
    dirty_ = true;
    Q_EMIT qmlChanged();
}

void SettingsMenu::saveQml()
{
    Save();
}

void SettingsMenu::resetAllQml()
{
    Reset();
}

void SettingsMenu::closeQml()
{
    Close();
}

bool SettingsMenu::SettingBool(const std::string& key)
{
    return model_.Bool(key);
}

int SettingsMenu::SettingInt(const std::string& key)
{
    return model_.Int(key);
}

float SettingsMenu::SettingFloat(const std::string& key)
{
    return model_.Float(key);
}

std::string SettingsMenu::SettingString(const std::string& key)
{
    return model_.Str(key);
}

std::string SettingsMenu::SettingsFilePath() const
{
    auto* store = SettingsStore();
    if (!store)
    {
        return {};
    }
    const int n = store->GetSettingsFilePath(nullptr, 0);
    if (n <= 0)
    {
        return {};
    }
    std::string path(static_cast<std::size_t>(n), '\0');
    store->GetSettingsFilePath(path.data(), n + 1);
    return path;
}

void SettingsMenu::ReloadSettingsFromDisk()
{
    if (auto* store = SettingsStore())
    {
        store->ReloadSettings();
    }
    // Re-seed drafts from the freshly reloaded live settings so no other page keeps a
    // stale value that a subsequent Save would write back over the hand edits.
    SeedFromContext();
    Q_EMIT qmlChanged();
}

QVariant SettingsMenu::FieldValue(const QString& key)
{
    const std::string fieldKey = key.toStdString();
    const auto it = std::ranges::find_if(
        fields_,
        [&](const FieldMeta& field)
        {
            return field.key == fieldKey;
        }
    );
    if (it == fields_.end())
    {
        return {};
    }
    switch (it->type)
    {
    case 0:
        return model_.Bool(fieldKey);
    case 1:
        return model_.Int(fieldKey);
    case 2:
        return model_.Float(fieldKey);
    case 3:
        return QString::fromStdString(model_.Str(fieldKey));
    default:
        return {};
    }
}

void SettingsMenu::Open() noexcept
{
    SeedFromContext();
    ReseedActivePage();
    open_ = true;
    if (ctx_)
    {
        ctx_->Publish<SettingsVisibilityEvent>({true});
    }
    Q_EMIT qmlChanged();
}

void SettingsMenu::OpenPage(const char* pageId) noexcept
{
    if (pageId && pageId[0])
    {
        qmlActivePage_ = pageId;
    }
    Open();
}

void SettingsMenu::Close() noexcept
{
    open_ = false;
    keybindConflict_.clear();
    if (isCapturing_)
    {
        SetCapturing(false);
    }
    if (ctx_)
    {
        ctx_->Publish<SettingsVisibilityEvent>({false});
    }
    Q_EMIT qmlChanged();
}

void SettingsMenu::SetCapturing(const bool v)
{
    isCapturing_ = v;
    if (!v)
    {
        capturingDraft_ = nullptr;
        capturingName_.clear();
    }
}

std::string SettingsMenu::FindKeyOwnerLabel(const std::string& canonicalKey, const char* exceptAction)
{
    if (canonicalKey.empty())
    {
        return {};
    }
    const std::string except = exceptAction ? exceptAction : "";
    // Conflict detection runs against the editable drafts (core in model_, plugin in
    // pluginKeybinds_), so a key the user just reassigned is seen immediately.
    for (const auto& e : kCoreKeybinds)
    {
        if (e.name != except && keybinds::Contains(model_.Str(e.key), canonicalKey))
        {
            return e.label;
        }
    }
    for (const auto& d : pluginKeybinds_)
    {
        if (d.action != except && keybinds::Contains(d.draft, canonicalKey))
        {
            return d.label;
        }
    }
    return {};
}

std::string* SettingsMenu::DraftForAction(const std::string& action)
{
    for (const auto& e : kCoreKeybinds)
    {
        if (action == e.name)
        {
            return &model_.Str(e.key);
        }
    }
    for (auto& d : pluginKeybinds_)
    {
        if (action == d.action)
        {
            return &d.draft;
        }
    }
    return nullptr;
}

void SettingsMenu::BeginCapture(const QString& action, const int slot)
{
    const std::string name = action.toStdString();
    std::string* draft = DraftForAction(name);
    if (!draft)
    {
        return;
    }
    capturingDraft_ = draft;
    capturingName_ = name;
    capturingSlot_ = slot;
    keybindConflict_.clear();
    SetCapturing(true);
    Q_EMIT qmlChanged();
}

void SettingsMenu::CancelCapture()
{
    if (isCapturing_)
    {
        SetCapturing(false);
        Q_EMIT qmlChanged();
    }
}

void SettingsMenu::CaptureKey(const int qtKey, const int qtMods)
{
    if (!isCapturing_)
    {
        return;
    }
    Mod mods = Mod::None;
    if (qtMods & static_cast<int>(Qt::ControlModifier))
    {
        mods = mods | Mod::Ctrl;
    }
    if (qtMods & static_cast<int>(Qt::ShiftModifier))
    {
        mods = mods | Mod::Shift;
    }
    if (qtMods & static_cast<int>(Qt::AltModifier))
    {
        mods = mods | Mod::Alt;
    }
    // Key constants mirror Qt::Key (AppEvent.h), so event.key is already a framelift Key.
    char buf[64] = {};
    KeyBindToString(static_cast<Key>(qtKey), mods, buf, sizeof(buf));
    ApplyCanonicalKey(buf);
}

void SettingsMenu::ApplyCanonicalKey(const std::string& keyStr)
{
    if (!isCapturing_ || !capturingDraft_)
    {
        return;
    }
    // KeyBindToString yields "?" for an unknown / modifier-only key — ignore it and
    // stay in capture so the user can press a real key.
    if (keyStr.empty() || keyStr.find('?') != std::string::npos)
    {
        return;
    }

    const std::string owner = FindKeyOwnerLabel(keyStr, capturingName_.c_str());
    if (!owner.empty())
    {
        keybindConflict_ = "\"" + keyStr + "\" is already bound to " + owner;
        SetCapturing(false);
        Q_EMIT qmlChanged();
        return;
    }
    if (!keybinds::Contains(*capturingDraft_, keyStr))
    {
        *capturingDraft_ = keybinds::SetSlot(*capturingDraft_, capturingSlot_, keyStr);
        dirty_ = true;
    }
    keybindConflict_.clear();
    SetCapturing(false);
    Q_EMIT qmlChanged();
}

void SettingsMenu::ClearKeybindSlot(const QString& action, const int slot)
{
    std::string* draft = DraftForAction(action.toStdString());
    if (!draft)
    {
        return;
    }
    *draft = keybinds::SetSlot(*draft, slot, "");
    dirty_ = true;
    keybindConflict_.clear();
    Q_EMIT qmlChanged();
}

namespace
{
QVariantMap KeybindEntryRow(const QString& label, const QString& action, bool isCore, const std::string& list)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("action"), action);
    row.insert(QStringLiteral("isCore"), isCore);
    row.insert(QStringLiteral("primary"), QString::fromStdString(keybinds::Slot(list, 0)));
    row.insert(QStringLiteral("alternate"), QString::fromStdString(keybinds::Slot(list, 1)));
    return row;
}
} // namespace

QVariantList SettingsMenu::CoreKeybindEntries()
{
    RefreshFields();
    QVariantList result;
    for (const auto& e : kCoreKeybinds)
    {
        result.push_back(
            KeybindEntryRow(QString::fromUtf8(e.label), QString::fromUtf8(e.name), true, model_.Str(e.key))
        );
    }
    return result;
}

QVariantList SettingsMenu::PluginKeybindGroups()
{
    RefreshFields();
    QVariantList groups;
    std::vector<QString> order; // preserve registration order of groups
    for (const auto& d : pluginKeybinds_)
    {
        const QString title = QString::fromStdString(d.group);
        if (std::find(order.begin(), order.end(), title) == order.end())
        {
            order.push_back(title);
            QVariantMap group;
            group.insert(QStringLiteral("title"), title);
            group.insert(QStringLiteral("entries"), QVariantList{});
            groups.push_back(group);
        }
        const int idx = static_cast<int>(std::find(order.begin(), order.end(), title) - order.begin());
        QVariantMap group = groups[idx].toMap();
        QVariantList list = group.value(QStringLiteral("entries")).toList();
        list.push_back(
            KeybindEntryRow(QString::fromStdString(d.label), QString::fromStdString(d.action), false, d.draft)
        );
        group.insert(QStringLiteral("entries"), list);
        groups[idx] = group;
    }
    return groups;
}

bool SettingsMenu::Capturing() const noexcept
{
    return isCapturing_;
}

QString SettingsMenu::CapturingActionName() const
{
    return QString::fromStdString(capturingName_);
}

int SettingsMenu::CapturingSlot() const noexcept
{
    return capturingSlot_;
}

QString SettingsMenu::KeybindConflict() const
{
    return QString::fromStdString(keybindConflict_);
}

bool SettingsMenu::HandleKeyDownEvent(const AppEvent&)
{
    // Key capture is driven from the settings ApplicationWindow's QML (a separate Qt
    // window whose key events never reach this host AppEvent hook). This handler only
    // swallows main-window keybinds while the settings UI is open.
    return open_;
}
