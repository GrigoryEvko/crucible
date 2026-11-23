// A null pointer is the empty state of a PublishSlot, so a publish of the
// null pointer would read as no publish.  publish takes a Ref, which is
// never null, so the null pointer does not reach it.

#include <fixy/handle/PublishOnce.h>

namespace h = fixy::handle;

namespace {
struct Payload {
    int value = 0;
};
}  // namespace

int main() {
    h::PublishSlot<Payload> slot{};
    slot.publish(nullptr);
    return slot.has_pending() ? 1 : 0;
}
