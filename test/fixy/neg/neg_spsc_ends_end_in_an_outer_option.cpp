// An Option of an outer scope would take the end that a body borrows, so the
// end could outlive its split and step a freed owner.  An end lives in its
// split and does not move, so no sum holds one.

#include <fixy/Core.h>
#include <fixy/concurrent/SpscEnds.h>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");
};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::Option<::fixy::concurrent::ProducerEnd<Owner>> kept{};
    return 0;
}
