#pragma once
#include <algorithm>
#include <framelift/FocusManager.h>
#include <vector>

class IModule;

// Concrete host-side FocusManager. App owns one instance and registers it as a
// service; modules use it through the abstract FocusManager interface.
class FocusManagerImpl final : public FocusManager
{
public:
    void Acquire(IModule* f) noexcept override
    {
        std::erase(stack_, f);
        stack_.push_back(f);
    }

    void Release(IModule* f) noexcept override
    {
        std::erase(stack_, f);
    }

    [[nodiscard]] IModule* Focused() const noexcept override
    {
        return stack_.empty() ? nullptr : stack_.back();
    }

private:
    std::vector<IModule*> stack_;
};
