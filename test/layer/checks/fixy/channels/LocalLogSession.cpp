// The compile-time checks of fixy/channels/LocalLogSession.h.

#include <fixy/channels/LocalLogSession.h>
#include <fixy/channels/SubstrateSessionBridge.h>

#include <type_traits>

namespace fixy::channels {

namespace detail::local_log_session_self_test {

namespace s = ::fixy::session;

// A log of int: two ends over the Local network, each naming the log.  The
// table knows the ends of a log, and the frame handles of its ends.
struct IntLog {
    using value_type = int;
    struct ProducerEnd {
        using channel_type = IntLog;
        static constexpr s::Network session_network = s::Network::Local;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
    struct ConsumerEnd {
        using channel_type = IntLog;
        static constexpr s::Network session_network = s::Network::Local;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
};

using Producer = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
using Consumer = s::Loop<s::Select<s::Recv<int, s::Continue>, s::End>>;

static_assert(LocalLogChannel<IntLog>);
static_assert(std::is_same_v<s::pole_protocol_t<IntLog::ProducerEnd>, Producer>);
static_assert(std::is_same_v<s::pole_peer_protocol_t<IntLog::ProducerEnd>, Consumer>);
static_assert(std::is_same_v<s::pole_protocol_t<IntLog::ConsumerEnd>, Consumer>);
static_assert(std::is_same_v<s::pole_peer_protocol_t<IntLog::ConsumerEnd>, Producer>);

// The local dual flips each step, keeps each output choice, and is its own
// inverse.  A protocol with an input choice has none.
static_assert(s::detail::local_dual_of(^^Producer) == std::meta::dealias(^^Consumer));
static_assert(s::detail::local_dual_of(^^Consumer) == std::meta::dealias(^^Producer));
static_assert(s::detail::local_dual_of(^^s::dual_of_t<Producer>) == std::meta::info{});
static_assert(s::LocalChoiceSidesAgree<Producer, Consumer, IntLog::ProducerEnd, IntLog::ConsumerEnd>);
static_assert(!s::LocalChoiceSidesAgree<Producer, Producer, IntLog::ProducerEnd, IntLog::ConsumerEnd>,
              "a producer does not face a producer");

// The relation asks for two carriers of the Local network.
struct WireEnd {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
static_assert(!s::LocalChoiceSidesAgree<Producer, Consumer, WireEnd, WireEnd>,
              "a carrier that states no network makes no local choice");
static_assert(s::ChannelSidesAgree<Producer, Consumer, IntLog::ProducerEnd, IntLog::ProducerEnd>);

// The unit includes the bridge too, so the root of the pole tables holds two
// families, and each pole reads the one table that knows it.
static_assert(!s::HasPoleTable<WireEnd>);

}  // namespace detail::local_log_session_self_test

}  // namespace fixy::channels
