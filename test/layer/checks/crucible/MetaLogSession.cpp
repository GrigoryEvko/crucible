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

static_assert(sizeof(Log::ProducerHandle) == sizeof(Log*),
              "metalog_session: ProducerHandle must stay pointer-sized; the Permission token collapses through EBO.");
static_assert(sizeof(Log::ConsumerHandle) == sizeof(Log*),
              "metalog_session: ConsumerHandle must stay pointer-sized; the Permission token collapses through EBO.");

// Each handle is a pole of the log, and the log table of
// fixy/channels/LocalLogSession.h names its protocol.  The two protocols
// agree by the relation of a local choice.
using ProducerProto = ::fixy::session::pole_protocol_t<Log::ProducerHandle>;
using ConsumerProto = ::fixy::session::pole_protocol_t<Log::ConsumerHandle>;
static_assert(::fixy::session::ChannelPole<Log::ProducerHandle> && ::fixy::session::ChannelPole<Log::ConsumerHandle>);
static_assert(
    std::is_same_v<ProducerProto,
                   ::fixy::session::Loop<::fixy::session::Select<
                       ::fixy::session::Send<MetaLogRecord, ::fixy::session::Continue>, ::fixy::session::End>>>);
static_assert(std::is_same_v<ConsumerProto, ::fixy::session::pole_peer_protocol_t<Log::ProducerHandle>>);
static_assert(
    ::fixy::session::LocalChoiceSidesAgree<ProducerProto, ConsumerProto, Log::ProducerHandle, Log::ConsumerHandle>);
static_assert(::fixy::session::AdmitsLocalChoice<Log::ProducerHandle, ProducerProto, void>);
static_assert(::fixy::session::AdmitsLocalChoice<Log::ConsumerHandle, ConsumerProto, void>);

}  // namespace detail::metalog_session_self_test

}  // namespace crucible::metalog_session
