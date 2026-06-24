#include "ToastNotifier.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <propidl.h>  // PROPVARIANT
#include <propsys.h>  // IPropertyStore
#include <shlobj.h>   // SHGetKnownFolderPath, FOLDERID_Programs
#include <shobjidl.h> // IShellLinkW, SetCurrentProcessExplicitAppUserModelID
#include <wrl/client.h>

#include <string>

// Toast support is reached through the Windows.UI.Notifications COM ABI (not the
// C++/WinRT projection). Guard on header availability so a toolchain that lacks
// these ABI headers still builds — NotifyError() then degrades to a no-op while
// the taskbar feature and registration are unaffected.
#if __has_include(<windows.ui.notifications.h>)
#include <roapi.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <winstring.h>
#define FL_HAVE_TOASTS 1
#endif

using Microsoft::WRL::ComPtr;

namespace
{
constexpr const wchar_t* kAumid = L"FrameLift.Player";
constexpr const wchar_t* kDisplayName = L"FrameLift";

// PKEY_AppUserModel_ID = {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, pid 5.
// Defined inline to avoid linking propsys just for one property key.
const PROPERTYKEY kPkeyAumid = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5
};

std::wstring Widen(const char* utf8)
{
    if (!utf8 || !*utf8)
    {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0)
    {
        return {};
    }
    std::wstring w(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), len);
    return w;
}

std::wstring StartMenuShortcutPath()
{
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs)))
    {
        return {};
    }
    std::wstring path = std::wstring(programs) + L"\\FrameLift.lnk";
    CoTaskMemFree(programs);
    return path;
}

#ifdef FL_HAVE_TOASTS
// RAII HSTRING from a wide string.
struct HStr
{
    HSTRING h = nullptr;
    explicit HStr(const wchar_t* s) noexcept
    {
        WindowsCreateString(s, static_cast<UINT32>(s ? wcslen(s) : 0), &h);
    }
    ~HStr()
    {
        if (h)
        {
            WindowsDeleteString(h);
        }
    }
    HStr(const HStr&) = delete;
    HStr& operator=(const HStr&) = delete;
};

// Append a text child to the index-th <text> node of a toast XML template.
void SetToastText(
    ABI::Windows::Data::Xml::Dom::IXmlDocument* xml, ABI::Windows::Data::Xml::Dom::IXmlNodeList* texts, UINT32 index,
    const wchar_t* value
) noexcept
{
    using namespace ABI::Windows::Data::Xml::Dom;
    UINT32 count = 0;
    if (FAILED(texts->get_Length(&count)) || index >= count)
    {
        return;
    }
    ComPtr<IXmlNode> node;
    if (FAILED(texts->Item(index, &node)))
    {
        return;
    }
    HStr val(value);
    ComPtr<IXmlText> textNode;
    if (FAILED(xml->CreateTextNode(val.h, &textNode)))
    {
        return;
    }
    ComPtr<IXmlNode> textAsNode;
    if (FAILED(textNode.As(&textAsNode)))
    {
        return;
    }
    ComPtr<IXmlNode> appended;
    node->AppendChild(textAsNode.Get(), &appended);
}
#endif // FL_HAVE_TOASTS

} // namespace

ToastNotifier::ToastNotifier() noexcept
{
#ifdef FL_HAVE_TOASTS
    // RO_INIT_SINGLETHREADED matches the apartment TaskbarProgress establishes on
    // the UI thread; S_FALSE (already initialized) still needs balancing.
    roInitialized_ = SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED));
#endif
    // Associate this process with the AUMID so any toast we raise routes to our
    // registered shortcut. Harmless even before Register() is called.
    SetCurrentProcessExplicitAppUserModelID(kAumid);
}

ToastNotifier::~ToastNotifier()
{
#ifdef FL_HAVE_TOASTS
    if (roInitialized_)
    {
        RoUninitialize();
    }
#endif
}

bool ToastNotifier::IsRegistered() const noexcept
{
    const std::wstring lnk = StartMenuShortcutPath();
    if (lnk.empty())
    {
        return false;
    }
    return GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ToastNotifier::Register() noexcept
{
    const std::wstring lnk = StartMenuShortcutPath();
    if (lnk.empty())
    {
        return false;
    }

    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0)
    {
        return false;
    }

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
    {
        return false;
    }
    link->SetPath(exe);

    // Stamp the shortcut with the AUMID so Windows attributes our toasts to it.
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(link.As(&props)))
    {
        PROPVARIANT pv = {};
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<wchar_t*>(kAumid); // SetValue copies; literal lifetime is fine
        props->SetValue(kPkeyAumid, pv);
        props->Commit();
    }

    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file)))
    {
        return false;
    }
    if (FAILED(file->Save(lnk.c_str(), TRUE)))
    {
        return false;
    }

    // Per-user AppUserModelId registry entry (DisplayName for the toast header).
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\FrameLift.Player", 0, nullptr, 0, KEY_WRITE,
            nullptr, &key, nullptr
        ) == ERROR_SUCCESS)
    {
        RegSetValueExW(
            key, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(kDisplayName),
            static_cast<DWORD>((wcslen(kDisplayName) + 1) * sizeof(wchar_t))
        );
        RegCloseKey(key);
    }
    return true;
}

void ToastNotifier::Notify(const char* title, const char* body) noexcept
{
#ifdef FL_HAVE_TOASTS
    if (!IsRegistered())
    {
        return;
    }
    using namespace ABI::Windows::UI::Notifications;
    using namespace ABI::Windows::Data::Xml::Dom;

    ComPtr<IToastNotificationManagerStatics> mgr;
    {
        HStr managerId(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager);
        if (FAILED(RoGetActivationFactory(managerId.h, IID_PPV_ARGS(&mgr))))
        {
            return;
        }
    }

    ComPtr<IXmlDocument> xml;
    if (FAILED(mgr->GetTemplateContent(ToastTemplateType_ToastText02, &xml)))
    {
        return;
    }

    ComPtr<IXmlNodeList> texts;
    {
        HStr tag(L"text");
        if (FAILED(xml->GetElementsByTagName(tag.h, &texts)))
        {
            return;
        }
    }
    const std::wstring wtitle = Widen(title);
    const std::wstring wbody = Widen(body);
    SetToastText(xml.Get(), texts.Get(), 0, wtitle.empty() ? L"FrameLift" : wtitle.c_str());
    SetToastText(xml.Get(), texts.Get(), 1, wbody.c_str());

    ComPtr<IToastNotificationFactory> factory;
    {
        HStr toastId(RuntimeClass_Windows_UI_Notifications_ToastNotification);
        if (FAILED(RoGetActivationFactory(toastId.h, IID_PPV_ARGS(&factory))))
        {
            return;
        }
    }

    ComPtr<IToastNotification> toast;
    if (FAILED(factory->CreateToastNotification(xml.Get(), &toast)))
    {
        return;
    }

    ComPtr<IToastNotifier> notifier;
    {
        HStr aumid(kAumid);
        if (FAILED(mgr->CreateToastNotifierWithId(aumid.h, &notifier)))
        {
            return;
        }
    }
    notifier->Show(toast.Get());
#else
    (void)title;
    (void)body;
#endif
}

void ToastNotifier::NotifyError(const char* file) noexcept
{
    Notify("Playback error", (file && *file) ? file : "Failed to play file");
}
