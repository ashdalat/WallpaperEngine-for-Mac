export module vrento.resource:error;
import rstd;

export namespace vrento::resource
{

using namespace rstd::prelude;

enum class ResourceErrorKind
{
    MissingDefinition,
    MissingContent,
    BackendFailure,
};

struct ResourceError {
    ResourceErrorKind kind { ResourceErrorKind::BackendFailure };
    String            message;
};

} // namespace vrento::resource

export namespace rstd
{

template<>
struct Impl<fmt::Display, vrento::resource::ResourceError>
    : ImplBase<vrento::resource::ResourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("{}", this->self().message));
    }
};

template<>
struct Impl<fmt::Debug, vrento::resource::ResourceError>
    : ImplBase<vrento::resource::ResourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("ResourceError(kind={}, message={})",
                                                        static_cast<int>(this->self().kind),
                                                        this->self().message));
    }
};

template<>
struct Impl<error::Error, vrento::resource::ResourceError>
    : DefaultInImpl<error::Error, vrento::resource::ResourceError> {};

} // namespace rstd

static_assert(rstd::Impled<vrento::resource::ResourceError, rstd::error::Error>);
