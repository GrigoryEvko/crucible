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
    namespace tr = ::fixy::session::transition;
    if (tr::read_seal(registry, ^^tr::combinator).fault != tr::seal_fault::none) return true;
    return tr::check_registry(registry).reason == tr::incoherence::none;
}

}  // namespace detail

static_assert(detail::registry_is_coherent(detail::protocol_registry),
              "fixy::session::diagnostic [Protocol_Incoherent_Registration]: a registration in "
              "fixy::session::combinators is incoherent.");

namespace detail::unfold_self_test {

// The unfold of a Loop puts the Loop in place of each Continue that it binds,
// through each step and each branch of a choice, also behind the Sender note
// of an Offer.  An inner Loop binds its own Continue, so it stays.  A node
// that is no binder is its own unfold.
struct Role {};
using Body = Send<int, Select<Recv<long, Continue>, End>>;
using Outer = Loop<Body>;
using Inner = Loop<Recv<short, Continue>>;
using Nested = Loop<Send<int, Offer<Sender<Role>, Inner, Continue>>>;

[[nodiscard]] consteval std::meta::info unfold_of(std::meta::info type) {
    return ::fixy::session::transition::unfold_of(protocol_registry, type);
}

static_assert(unfold_of(^^Outer) == ^^Send<int, Select<Recv<long, Outer>, End>>);
static_assert(unfold_of(^^Nested) == ^^Send<int, Offer<Sender<Role>, Inner, Nested>>);
static_assert(unfold_of(^^Body) == std::meta::dealias(^^Body));
static_assert(unfold_of(^^End) == (^^End));

}  // namespace detail::unfold_self_test

}  // namespace fixy::session
