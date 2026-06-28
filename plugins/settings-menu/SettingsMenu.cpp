#include "SettingsMenu.h"

#include "KeybindList.h"
#include <framelift/Hotkeys.h>

#include <QtCore/QMetaObject>
#include <QtCore/QSet>
#include <QtCore/QVariantMap>
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
    if (id == "subtitle")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/SubtitleSettings.qml";
    }
    if (id == "theme")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/ThemeSettings.qml";
    }
    if (id == "ui")
    {
        return "qrc:/qt/qml/FrameLift/Plugins/SettingsMenu/UiSettings.qml";
    }
    return nullptr;
}

QString PageTitleFromId(const std::string& id)
{
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
                framelift::AddItem(
                    m, "Settings", "openSettings",
                    [this]
                    {
                        OpenPage(nullptr);
                    }
                );
                framelift::AddItem(
                    m, "Audio Settings",
                    [this]
                    {
                        OpenPage("audio");
                    }
                );
                framelift::AddItem(
                    m, "Subtitle Settings",
                    [this]
                    {
                        OpenPage("subtitle");
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
    dirty_ = false;
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
    std::stable_sort(
        result.begin(), result.end(),
        [](const QVariant& a, const QVariant& b)
        {
            return a.toMap().value(QStringLiteral("order")).toInt() < b.toMap().value(QStringLiteral("order")).toInt();
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
        auto model = std::make_unique<SettingsSectionPageModel>(*this, id, PageTitleFromId(section));
        QObject* modelPtr = model.get();
        pageModels_.push_back(std::move(model));
        registry->RegisterSettingsPage(
            section.c_str(), PageTitleFromId(section).toUtf8().constData(), qmlUrl, modelPtr, order
        );
        order += 10;
    }

    if (PluginCatalog())
    {
        auto model = std::make_unique<SettingsPluginsPageModel>(*this);
        QObject* modelPtr = model.get();
        pageModels_.push_back(std::move(model));
        registry->RegisterSettingsPage("plugins", "Plugins", kPluginsPageQml, modelPtr, 900);
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
        capturingBind_ = nullptr;
        capturingGetStr_ = nullptr;
        capturingSetStr_ = nullptr;
        capturingUd_ = nullptr;
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
    for (const auto& e : kCoreKeybinds)
    {
        if (e.name != except && keybinds::Contains(model_.Str(e.key), canonicalKey))
        {
            return e.label;
        }
    }

    struct SearchCtx
    {
        const std::string* key;
        const std::string* except;
        std::string found;
    };

    SearchCtx sc{&canonicalKey, &except, {}};
    if (auto* reg = SettingsReg())
    {
        reg->EnumerateKeybindEntries(
            [](const char* label, const char* actionName, const char* (*getStr)(void*), void (*)(void*, const char*),
               void* ud, void* pv)
            {
                auto& s = *static_cast<SearchCtx*>(pv);
                if (!s.found.empty() || (actionName && *s.except == actionName))
                {
                    return;
                }
                const std::string list = getStr ? getStr(ud) : "";
                if (keybinds::Contains(list, *s.key))
                {
                    s.found = label ? label : "";
                }
            },
            &sc
        );
    }
    return sc.found;
}

bool SettingsMenu::HandleKeyDownEvent(const AppEvent& e)
{
    if (!isCapturing_)
    {
        return open_;
    }
    const AppEvent::KeyPayload& kp = e.AsKey();
    if (kp.key == Keys::Escape)
    {
        SetCapturing(false);
        return true;
    }
    char bindBuf[64] = {};
    KeyBindToString(kp.key, kp.mods, bindBuf, sizeof(bindBuf));
    const std::string keyStr = bindBuf;

    const std::string owner = FindKeyOwnerLabel(keyStr, capturingName_.c_str());
    if (!owner.empty())
    {
        keybindConflict_ = "\"" + keyStr + "\" is already bound to " + owner;
        SetCapturing(false);
        return true;
    }

    const std::string cur = capturingBind_     ? *capturingBind_
                            : capturingGetStr_ ? capturingGetStr_(capturingUd_)
                                               : std::string{};
    if (keybinds::Contains(cur, keyStr))
    {
        keybindConflict_.clear();
        SetCapturing(false);
        return true;
    }

    const std::string next = keybinds::SetSlot(cur, capturingSlot_, keyStr);
    if (capturingBind_)
    {
        *capturingBind_ = next;
    }
    else if (capturingSetStr_)
    {
        capturingSetStr_(capturingUd_, next.c_str());
    }
    if (!capturingName_.empty())
    {
        if (auto* hk = ctx_ ? ctx_->GetService<Hotkeys>() : nullptr)
        {
            hk->RebindList(capturingName_.c_str(), next.c_str());
        }
    }
    keybindConflict_.clear();
    dirty_ = true;
    SetCapturing(false);
    return true;
}
