#pragma once

// ── FrameLift plugin ABI rules ─────────────────────────────────────────────────────
//
// These rules apply to every type that crosses the host↔plugin DLL boundary.
// Violating them causes undefined behavior, crashes, or silent data corruption
// when host and plugin are built with different compilers or runtimes.
//
// 1. INTERFACES ONLY at the boundary.
//    Every exchanged object is a pure abstract class: single inheritance, only
//    virtual methods, no data members, no inline non-virtual logic that touches
//    object layout.  Declare methods noexcept; exceptions must not propagate
//    across a DLL boundary.
//
// 2. POD-ONLY signatures.
//    No std::string, std::vector, std::function, std::variant, std::optional,
//    std::unique_ptr, or by-value non-trivial structs in virtual method parameters
//    or return types.  Use instead:
//      - const char*     for string inputs (null-terminated, caller retains ownership)
//      - int Get(char*, int)  pattern for string outputs (returns byte count excl. NUL)
//      - enumeration callback  for collection outputs: void(*)(const T*, void*), void* ud
//      - function pointer + void* userdata  instead of std::function
//      - out-pointer parameter  for returned non-trivial values
//      - C POD structs  for exchanged data (fixed-size char arrays, numeric fields)
//
// 3. EXPLICIT IDs, not typeid.
//    Every interface declares:
//        static constexpr const char* InterfaceId = "framelift.IMyInterface";
//    Every POD event declares:
//        static constexpr const char* EventId = "framelift.MyEvent";
//    Service lookup and pub/sub are keyed on these string constants.
//    Never use typeid(T).name() across a DLL boundary.
//
// 4. STRING LIFETIME documented per direction.
//      host → plugin:  const char* params are valid only for the duration of the call.
//      plugin → host:  same.
//      Subscribers:    event const char* fields are valid only during the callback.
//    Plugins that need to outlive the call must copy into their own storage.
//
// 5. ABI VERSION (a single integer — sdk/include/framelift/ModuleABI.h).
//    The loader accepts a plugin iff plugin.abiVersion == host.abiVersion, checked
//    before any vtable is touched. Host and plugins are built from one source tree
//    in lockstep, so an exact match is the whole rule — a mismatch means a stale
//    binary that must be rebuilt, not negotiated.
//      Bump FRAMELIFT_ABI_VERSION only on a Tier-1 break: a framelift_* export
//      signature, the FrameLiftPackageInfo/FrameLiftModuleInfo layout, a host-CALLED
//      interface (IModule, IRenderable), or the bootstrap surface of IModuleContext.
//      New host capabilities are NOT a break — they ship as new, independently
//      discovered service interfaces (GetService<T>() returns nullptr when absent),
//      so adding one never touches the version.
//
// ── Helper macros ─────────────────────────────────────────────────────────────

// Declare a non-copyable, non-movable interface base.
// Use at the top of every boundary interface class body.
#define FRAMELIFT_INTERFACE(ClassName)                                                                                      \
    ClassName(const ClassName&) = delete;                                                                              \
    ClassName& operator=(const ClassName&) = delete;                                                                   \
    ClassName(ClassName&&) = delete;                                                                                   \
    ClassName& operator=(ClassName&&) = delete
