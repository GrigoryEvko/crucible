// The context holds 9 roles that each send to each of the 9: 81 channels,
// past the bound of 64 channels of a reading.  The channels stand in the
// order of the sender, so the 65th channel goes from the role Numbered<8>
// to the role Numbered<2>, and the door refuses the context there.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_context_net_past_channel_bound_types {

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

using ManyChannels = numbered_context<send_to_each<9, s::End>::type, 9>::type;

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<ManyChannels, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_past_channel_bound_types

using namespace neg_sess_context_net_past_channel_bound_types;

int main() { return require_deadlock_free(); }
