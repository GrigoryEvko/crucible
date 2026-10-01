// The compile-time checks of fixy/session/Protocol.h.

#include <fixy/session/Protocol.h>

namespace fixy::session {

namespace detail {

// True when every registration of the registry is coherent.  It reads no
// shape while the seal is broken, so a registration outside the seal
// gives the one diagnostic of the seal check in the header.  The registry
// is a parameter, so the compiler cannot fold the read before the test of
// the seal.
[[nodiscard]] consteval bool registry_is_coherent(std::meta::info registry) {
    namespace tr = ::foundation::algebra::transition;
    if (tr::read_seal(registry, ^^tr::combinator).fault != tr::seal_fault::none) return true;
    return tr::check_registry(registry).reason == tr::incoherence::none;
}

}  // namespace detail

static_assert(detail::registry_is_coherent(detail::protocol_registry),
              "fixy::session::diagnostic [Protocol_Incoherent_Registration]: a registration in "
              "fixy::session::combinators is incoherent.");

}  // namespace fixy::session
