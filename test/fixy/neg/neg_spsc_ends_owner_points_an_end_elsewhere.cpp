// The owner is a friend of its ends, and it points the producer end of one
// split at the owner of a second split, which would then have two producer
// ends.  The Ref of an end is const, so no code points it elsewhere.

#include <fixy/concurrent/SpscEnds.h>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");

    static void point_elsewhere(::fixy::concurrent::ProducerEnd<Owner>& first,
                                ::fixy::concurrent::ProducerEnd<Owner> const& second) noexcept {
        first.owner_ = second.owner_;
    }
};

}  // namespace

int main() { return 0; }
