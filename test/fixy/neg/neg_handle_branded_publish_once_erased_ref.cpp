// An erased Ref names no owner, and it can reach an object of a frame.  A
// branded slot keeps the object for its whole life, so publish takes no erased
// Ref.

#include <fixy/Core.h>
#include <fixy/handle/PublishOnce.h>

namespace h = fixy::handle;

namespace {
struct Payload {
    int value = 0;
};
struct SlotOwner final {};

void publish_erased(h::BrandedPublishOnce<Payload const, SlotOwner>& slot, Payload const& payload) noexcept {
    slot.publish(::fixy::erase_brand(::fixy::to(payload)));
}
}  // namespace

int main() {
    static_cast<void>(&publish_erased);
    return 0;
}
