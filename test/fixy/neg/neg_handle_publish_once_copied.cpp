// A PublishOnce is a channel identity: observers hold its address and
// read the slot through it.  A copy would give two slots where the
// publisher believes there is one, and an observer holding the address
// of the copy would never see the publish.
//
// The slot is published correctly first, so nothing about the
// publication is at fault here — only the copy.

#include <fixy/handle/PublishOnce.h>

namespace h = fixy::handle;

namespace {
struct Payload {
    int value = 0;
};
}  // namespace

int main() {
    Payload value{1};
    h::PublishOnce<Payload> slot{};
    slot.publish(&value);

    auto second = slot;
    return second.observe() == &value ? 0 : 1;
}
