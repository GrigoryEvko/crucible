// The axioms of the payload order are sealed in fixy/session/Subtype.h.
// An axiom that a later header adds would widen the order in the
// translation units that see it and not in the others.  So the next
// query of the order counts the axioms again, and a count that differs
// from the seal stops the build.

#include <fixy/session/Subtype.h>

namespace {

template <class T, class U>
inline constexpr bool long_weakens_to_int_v = std::is_same_v<T, long> && std::is_same_v<U, int>;

}  // namespace

namespace fixy::session::payload_axioms {
inline constexpr ::foundation::algebra::transition::subsort_axiom long_to_int{.weakens = ^^::long_weakens_to_int_v};
}  // namespace fixy::session::payload_axioms

int main() {
    return ::fixy::session::is_subtype_sync_v<::fixy::session::Send<long, ::fixy::session::End>,
                                              ::fixy::session::Send<int, ::fixy::session::End>>
             ? 0
             : 1;
}
