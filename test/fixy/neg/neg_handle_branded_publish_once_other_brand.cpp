// A branded slot names the owner of the object that it holds.  A Ref of
// another brand names another owner, so publish takes no such Ref.

#include <fixy/Core.h>
#include <fixy/handle/PublishOnce.h>

namespace h = fixy::handle;

namespace {
struct Payload {
    int value = 0;
};
struct SlotOwner final {};
struct OtherOwner final {};

void publish_other(h::BrandedPublishOnce<Payload const, SlotOwner>& slot,
                   ::fixy::Ref<Payload const, OtherOwner> other) noexcept {
    slot.publish(other);
}
}  // namespace

int main() {
    static_cast<void>(&publish_other);
    return 0;
}
