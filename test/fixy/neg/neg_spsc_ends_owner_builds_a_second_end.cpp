// The owner is a friend of its ends, and it builds a second producer end from
// the Ref of the first one.  The constructor of an end takes a key that only
// a split builds, so the owner has no key.

#include <fixy/concurrent/SpscEnds.h>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");

    static void second_end(::fixy::concurrent::ProducerEnd<Owner> const& end) noexcept {
        ::fixy::concurrent::ProducerEnd<Owner> const second{::fixy::concurrent::SpscEndKey<Owner>{}, end.owner_};
        static_cast<void>(second);
    }
};

}  // namespace

int main() { return 0; }
