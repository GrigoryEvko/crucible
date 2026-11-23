// The compile-time checks of crucible/MetaLogSession.h.

#include <crucible/MetaLogSession.h>

#include <type_traits>

namespace crucible::metalog_session {

// The gate cases that need an execution context are in
// test/test_metalog_session.cpp, because a production header has no
// context to name.
namespace detail::metalog_session_self_test {

struct Tag {};
struct Brand {};
using Log = ::crucible::PermissionedMetaLog<Tag, Brand>;

static_assert(MetaLogSessionSurface<Log>);
static_assert(!MetaLogSessionSurface<int>, "an int is not a permissioned log");

// A scope brand that the checks name, and the frame handles on it.  No frame
// door gives a handle on it.
struct CheckScope {};
using Producer = Log::producer_handle<CheckScope>;
using Consumer = Log::consumer_handle<CheckScope>;

static_assert(
    sizeof(Producer) == sizeof(Log*),
    "metalog_session: a producer handle must stay pointer-sized; the Permission token collapses through EBO.");
static_assert(
    sizeof(Consumer) == sizeof(Log*),
    "metalog_session: a consumer handle must stay pointer-sized; the Permission token collapses through EBO.");

// Each end and each frame handle of an end is a pole of the log, and the log
// table of fixy/channels/LocalLogSession.h names its protocol.  The two
// protocols agree by the relation of a local choice.
using ProducerProto = ::fixy::session::pole_protocol_t<Producer>;
using ConsumerProto = ::fixy::session::pole_protocol_t<Consumer>;
static_assert(::fixy::session::ChannelPole<Producer> && ::fixy::session::ChannelPole<Consumer>);
static_assert(::fixy::session::ChannelPole<Log::ProducerEnd> && ::fixy::session::ChannelPole<Log::ConsumerEnd>);
static_assert(
    std::is_same_v<ProducerProto,
                   ::fixy::session::Loop<::fixy::session::Select<
                       ::fixy::session::Send<MetaLogRecord, ::fixy::session::Continue>, ::fixy::session::End>>>);
static_assert(std::is_same_v<ConsumerProto, ::fixy::session::pole_peer_protocol_t<Producer>>);
static_assert(std::is_same_v<ProducerProto, ::fixy::session::pole_protocol_t<Log::ProducerEnd>>);
static_assert(::fixy::session::LocalChoiceSidesAgree<ProducerProto, ConsumerProto, Producer, Consumer>);
static_assert(::fixy::session::AdmitsLocalChoice<Producer, ProducerProto, void>);
static_assert(::fixy::session::AdmitsLocalChoice<Consumer, ConsumerProto, void>);

}  // namespace detail::metalog_session_self_test

}  // namespace crucible::metalog_session
