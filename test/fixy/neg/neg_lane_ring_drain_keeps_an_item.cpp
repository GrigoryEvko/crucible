// The drain of a ring of lanes gives each item to its drop as a temporary of
// the door.  A drop that takes the item by reference and captures an Option of
// a View by reference could keep a borrow of that temporary there, and the
// caller could read it after the drain.  The drain refuses such a drop
// (LentArmStoresNoBorrow of foundation/core/Choice.h).

#include <fixy/concurrent/LaneRing.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <cstdint>

namespace {

struct LanesOwner final {};
using Ring = ::fixy::concurrent::LaneRing<std::uint64_t, 4, ::fixy::concurrent::LaneCount{2}, LanesOwner>;

[[maybe_unused]] void drain_each(Ring& ring, Ring::ConsumerProof const& proof) noexcept {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    ring.drain(proof, [&kept](std::uint64_t const& item) noexcept {
        static_cast<void>(item);
        static_cast<void>(kept.is_some());
    });
}

}  // namespace

int main() { return 0; }
