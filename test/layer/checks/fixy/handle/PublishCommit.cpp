// The compile-time checks of fixy/handle/PublishCommit.h.

#include <fixy/handle/PublishCommit.h>

namespace fixy::handle {

namespace detail::publish_commit_detail {

struct ProbeTag {};
struct ProbeAuth {};

static_assert(std::is_trivially_destructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_move_constructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_copy_constructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(alignof(PublishCommitCell<ProbeTag, ProbeAuth>) == 64);

}  // namespace detail::publish_commit_detail

}  // namespace fixy::handle
