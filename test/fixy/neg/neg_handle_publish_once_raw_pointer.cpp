// A null pointer is the never-published state of a PublishOnce.  A raw
// pointer can be null, so publish takes no raw pointer.  It takes a Ref,
// which is never null, and the publish needs no test of the pointer.

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
    return slot.is_published() ? 0 : 1;
}
