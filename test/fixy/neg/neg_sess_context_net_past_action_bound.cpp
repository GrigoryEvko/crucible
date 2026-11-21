// The context holds 29 roles that each send to 9 roles: 261 actions, past
// the bound of 256 actions of a reading (action_bound).  The 257th action
// is the fifth step of the role Numbered<29>.  The door refuses the context,
// and its text names the count of 261 actions, the bound and that role.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_context_net_past_action_bound_types {

namespace s = ::fixy::session;

template <std::size_t Place>
struct Numbered {};

struct X {};

struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

// The local type that sends one message to each of the roles Numbered<1>
// to Numbered<Count>, in that order, and then Next.
template <std::size_t Count, typename Next>
struct send_to_each {
    using type = typename send_to_each<Count - 1, s::Send<s::PeerMsg<Numbered<Count>, X, int>, Next>>::type;
};
template <typename Next>
struct send_to_each<0, Next> {
    using type = Next;
};

// The roles Numbered<1> to Numbered<Count>, each with the local type Local.
template <typename Local, std::size_t Count, typename... States>
struct numbered_context {
    using type = typename numbered_context<Local, Count - 1, s::RoleState<Numbered<Count>, s::OutQueue<>, Local>,
                                           States...>::type;
};
template <typename Local, typename... States>
struct numbered_context<Local, 0, States...> {
    using type = s::TypingContext<States...>;
};

using ManyActions = numbered_context<send_to_each<9, s::End>::type, 29>::type;

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<ManyActions, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_past_action_bound_types

using namespace neg_sess_context_net_past_action_bound_types;

int main() { return require_deadlock_free(); }
