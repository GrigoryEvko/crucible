// The compile-time checks of fixy/session/NetworkModel.h.

#include <fixy/session/NetworkModel.h>

namespace fixy::session {

namespace detail::network::witness {

struct FifoCarrier {
    static constexpr Network session_network = Network::PerPairFifo;
};
struct MailboxCarrier {
    static constexpr Network session_network = Network::Mailbox;
};
struct BagCarrier {
    static constexpr Network session_network = Network::Bag;
};
struct LocalCarrier {
    static constexpr Network session_network = Network::Local;
};
struct SilentCarrier {};
struct IntegerCarrier {
    static constexpr int session_network = 0;
};

struct InheritedCarrier : MailboxCarrier {};

static_assert(has_session_network(^^FifoCarrier) && has_session_network(^^MailboxCarrier)
              && has_session_network(^^BagCarrier) && has_session_network(^^LocalCarrier)
              && has_session_network(^^FifoCarrier&) && has_session_network(^^InheritedCarrier));
static_assert(!has_session_network(^^int) && !has_session_network(^^SilentCarrier)
              && !has_session_network(^^IntegerCarrier));
static_assert(states_a_network_member(^^IntegerCarrier) && !states_a_network_member(^^SilentCarrier));
static_assert(session_network(^^InheritedCarrier) == Network::Mailbox);
static_assert(::fixy::session::LocalCarrier<LocalCarrier> && !::fixy::session::LocalCarrier<FifoCarrier>
              && !::fixy::session::LocalCarrier<SilentCarrier> && !::fixy::session::LocalCarrier<IntegerCarrier>);

}  // namespace detail::network::witness

}  // namespace fixy::session
