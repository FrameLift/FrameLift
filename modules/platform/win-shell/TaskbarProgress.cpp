#include "TaskbarProgress.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shobjidl.h>

namespace
{
TBPFLAG ToFlag(ProgressState s) noexcept
{
    switch (s)
    {
    case ProgressState::NoProgress:
        return TBPF_NOPROGRESS;
    case ProgressState::Indeterminate:
        return TBPF_INDETERMINATE;
    case ProgressState::Normal:
        return TBPF_NORMAL;
    case ProgressState::Paused:
        return TBPF_PAUSED;
    case ProgressState::Error:
        return TBPF_ERROR;
    }
    return TBPF_NOPROGRESS;
}
} // namespace

TaskbarProgress::TaskbarProgress(void* hwnd) noexcept : hwnd_(hwnd)
{
    // The taskbar API runs on the UI thread; init a single-threaded apartment.
    // Both S_OK and S_FALSE (already initialized on this thread) must be balanced
    // by CoUninitialize; RPC_E_CHANGED_MODE fails, so we then own no init.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    comInitialized_ = SUCCEEDED(hr);

    ITaskbarList3* tbl = nullptr;
    if (SUCCEEDED(CoCreateInstance(
            CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, reinterpret_cast<void**>(&tbl)
        )))
    {
        if (SUCCEEDED(tbl->HrInit()))
        {
            taskbar_ = tbl;
        }
        else
        {
            tbl->Release();
        }
    }
}

TaskbarProgress::~TaskbarProgress()
{
    if (taskbar_)
    {
        Clear();
        static_cast<ITaskbarList3*>(taskbar_)->Release();
        taskbar_ = nullptr;
    }
    if (comInitialized_)
    {
        CoUninitialize();
    }
}

void TaskbarProgress::SetValue(int permille) noexcept
{
    if (!taskbar_ || !hwnd_)
    {
        return;
    }
    const ULONGLONG value = permille < 0 ? 0 : (permille > 1000 ? 1000 : static_cast<ULONGLONG>(permille));
    static_cast<ITaskbarList3*>(taskbar_)->SetProgressValue(static_cast<HWND>(hwnd_), value, 1000ull);
}

void TaskbarProgress::SetState(ProgressState state) noexcept
{
    if (!taskbar_ || !hwnd_)
    {
        return;
    }
    static_cast<ITaskbarList3*>(taskbar_)->SetProgressState(static_cast<HWND>(hwnd_), ToFlag(state));
}

void TaskbarProgress::Clear() noexcept
{
    SetState(ProgressState::NoProgress);
}
