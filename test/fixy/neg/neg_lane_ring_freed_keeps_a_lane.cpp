// try_receive_freeing of a ring of lanes calls freed with the index of a lane,
// a local of the door.  A callable that takes the index by reference and
// captures an Option of a View by reference could keep a borrow of that local
// there, and the caller could read it after the receive.  The receive refuses
// such a callable (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <fixy/concurrent/LaneRing.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <cstdint>

namespace {

struct LanesOwner final {};
using Ring = ::fixy::concurrent::LaneRing<std::uint64_t, 4, ::fixy::concurrent::LaneCount{2}, LanesOwner>;

[[maybe_unused]] bool receive_one(Ring& ring, Ring::ConsumerProof const& proof) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    return ring
        .try_receive_freeing(proof,
                             [&kept](Ring::lane_index const& lane) noexcept {
                                 static_cast<void>(lane);
                                 static_cast<void>(kept.is_some());
                             })
        .is_ok();
}

}  // namespace

int main() { return 0; }
