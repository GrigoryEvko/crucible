// The compile-time checks of crucible/MetaLogSession.h.

#include <crucible/MetaLogSession.h>

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

static_assert(::fixy::session::AdmitsLocalChoice<Log::ProducerHandle, ProducerProto, void>);
static_assert(::fixy::session::AdmitsLocalChoice<Log::ConsumerHandle, ConsumerProto, void>);

}  // namespace detail::metalog_session_self_test

}  // namespace crucible::metalog_session
