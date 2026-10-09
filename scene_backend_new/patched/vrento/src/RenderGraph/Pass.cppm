export module vrento.rgraph:pass;
import rstd;

using namespace rstd::prelude;

export namespace vrento::rg
{

struct PassHandle {
    usize index { usize::MAX };

    bool valid() const noexcept { return index != usize::MAX; }

    friend auto operator<=>(const PassHandle&, const PassHandle&) = default;
};

struct Pass {
    Pass()          = default;
    virtual ~Pass() = default;

    Pass(const Pass&)            = delete;
    Pass& operator=(const Pass&) = delete;
};

struct PassObject {
    using Trait                  = PassObject;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = PassObject;
        auto AsPass() -> Pass& { return rstd::trait_call<0>(this); }
        auto AsConstPass() const -> const Pass& { return rstd::trait_call<1>(this); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::AsPass, &T::AsConstPass>;
};

struct VirtualPass : Pass {
    struct Desc {};

    explicit VirtualPass(Desc&&) noexcept {}
    ~VirtualPass() noexcept override = default;
};

} // namespace vrento::rg

export namespace rstd
{

template<typename T>
    requires requires(T& value) { static_cast<vrento::rg::Pass&>(value); }
struct Impl<vrento::rg::PassObject, T> : ImplBase<T> {
    auto AsPass() -> vrento::rg::Pass& { return this->self(); }
    auto AsConstPass() const -> const vrento::rg::Pass& { return this->self(); }
};

template<>
struct Impl<hash::Hash, vrento::rg::PassHandle> : ImplBase<vrento::rg::PassHandle> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().index, state);
    }
};

} // namespace rstd
