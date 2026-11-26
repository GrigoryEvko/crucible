// The compile-time checks of fixy/handle/PublishCommit.h.

#include <fixy/handle/PublishCommit.h>

#include <foundation/core/CaptureStore.h>

namespace fixy::handle {

namespace detail::publish_commit_detail {

struct ProbeTag {};
struct ProbeAuth {};

static_assert(std::is_trivially_destructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_move_constructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(!std::is_copy_constructible_v<PublishCommitCell<ProbeTag, ProbeAuth>>);
static_assert(alignof(PublishCommitCell<ProbeTag, ProbeAuth>) == 64);

// The cell states synchronized_by_proof{}, and the check of the mark holds on
// it: no door keeps a borrow that a caller gives it.  The check of
// synchronized{} runs first, so the token of the writer is a token of one
// thread that only its stage builds.
class PublishingStage;
class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] Publisher final {
    Publisher() noexcept {}
    friend class PublishingStage;

public:
    Publisher(Publisher const&) = delete("a publisher token names the one writer of the cell");
    Publisher& operator=(Publisher const&) = delete("a publisher token names the one writer of the cell");
    ~Publisher() = default;
};
static_assert(::foundation::core::detail::synchronized_by_proof_check_v<PublishCommitCell<ProbeTag, Publisher>>);

}  // namespace detail::publish_commit_detail

}  // namespace fixy::handle
