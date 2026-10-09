export module vvk:handle;

import rstd;

using namespace rstd::prelude;

export namespace vvk
{

class NoCopy {
protected:
    NoCopy()  = default;
    ~NoCopy() = default;

public:
    NoCopy(const NoCopy&)            = delete;
    NoCopy& operator=(const NoCopy&) = delete;
};

template<typename Type, typename OwnerType, typename Dispatch>
class Handle : NoCopy {
public:
    using handle_type = Type;
    explicit Handle(Type handle_, OwnerType owner_, const Dispatch& dld_) noexcept
        : handle { handle_ }, owner { owner_ }, dld { &dld_ } {}

    Handle() = default;
    Handle(nullptr_t) {}

    Handle(Handle&& rhs) noexcept
        : handle { rstd::exchange(rhs.handle, nullptr) }, owner { rhs.owner }, dld { rhs.dld } {}
    Handle& operator=(Handle&& rhs) noexcept {
        Release();
        handle = rstd::exchange(rhs.handle, nullptr);
        owner  = rhs.owner;
        dld    = rhs.dld;
        return *this;
    }

    ~Handle() noexcept { Release(); }

    void reset() noexcept {
        Release();
        handle = nullptr;
    }

    const Type* address() const noexcept { return rstd::addressof(handle); }
    const Type& operator*() const noexcept { return handle; }
    explicit    operator bool() const noexcept { return handle != nullptr; }

protected:
    Type            handle = nullptr;
    OwnerType       owner  = nullptr;
    const Dispatch* dld    = nullptr;

private:
    void Release() noexcept {
        if (handle) {
            Destroy(owner, handle, *dld);
        }
    }
};

struct NoOwner {};
struct NoOwnerLife {};
struct BorrowedHandle {};
inline constexpr BorrowedHandle borrowed_handle {};

template<typename Type, typename Dispatch>
class Handle<Type, NoOwner, Dispatch> : NoCopy {
public:
    using handle_type = Type;
    explicit Handle(Type handle_, const Dispatch& dld_) noexcept
        : handle { handle_ }, dld { &dld_ } {}
    explicit Handle(Type handle_, const Dispatch& dld_, BorrowedHandle) noexcept
        : handle { handle_ }, dld { &dld_ }, owned { false } {}

    Handle() = default;
    Handle(nullptr_t) {}

    Handle(Handle&& rhs) noexcept
        : handle { rstd::exchange(rhs.handle, nullptr) }, dld { rhs.dld }, owned { rhs.owned } {}
    Handle& operator=(Handle&& rhs) noexcept {
        Release();
        handle = rstd::exchange(rhs.handle, nullptr);
        dld    = rhs.dld;
        owned  = rhs.owned;
        return *this;
    }

    ~Handle() noexcept { Release(); }

    void reset() noexcept {
        Release();
        handle = nullptr;
    }

    const Type* address() const noexcept { return rstd::addressof(handle); }
    const Type& operator*() const noexcept { return handle; }
    explicit    operator bool() const noexcept { return handle != nullptr; }

protected:
    Type            handle = nullptr;
    const Dispatch* dld    = nullptr;
    bool            owned  = true;

private:
    void Release() noexcept {
        if (handle && owned) {
            Destroy(handle, *dld);
        }
    }
};

template<typename Type, typename Dispatch>
class Handle<Type, NoOwnerLife, Dispatch> {
public:
    using handle_type = Type;
    explicit Handle(Type handle_, const Dispatch& dld_) noexcept
        : handle { handle_ }, dld { &dld_ } {}

    Handle()  = default;
    ~Handle() = default;

    Handle(nullptr_t) {}

    void reset() noexcept { handle = nullptr; }

    const Type* address() const noexcept { return rstd::addressof(handle); }
    const Type& operator*() const noexcept { return handle; }
    explicit    operator bool() const noexcept { return handle != nullptr; }

protected:
    Type            handle = nullptr;
    const Dispatch* dld    = nullptr;
};

} // namespace vvk
