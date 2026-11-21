// The context holds 33 roles, one more than the bound of a reading
// (role_bound).  A reading keeps the roles in a fixed array of 32, so the
// context is outside the model of the check.  The door refuses it, and its
// text names the count of 33 roles, the bound and the role Numbered<33>.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage, as the types of the other contexts of
// the door do.
namespace neg_sess_context_net_past_bound_types {

namespace s = ::fixy::session;

template <std::size_t Place>
struct Numbered {};

struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

// The roles Numbered<1> to Numbered<Count>, each with the local type End.
template <std::size_t Count, typename... States>
struct numbered_context {
    using type =
        typename numbered_context<Count - 1, s::RoleState<Numbered<Count>, s::OutQueue<>, s::End>, States...>::type;
};
template <typename... States>
struct numbered_context<0, States...> {
    using type = s::TypingContext<States...>;
};

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<numbered_context<33>::type, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_past_bound_types

using namespace neg_sess_context_net_past_bound_types;

int main() { return require_deadlock_free(); }
