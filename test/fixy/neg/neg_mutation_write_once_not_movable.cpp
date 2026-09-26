// set moves its argument into the slot, so a carrier that cannot move
// could never be set.  The door refuses it.

#include <fixy/Mutation.h>

namespace {

struct Pinned {
    Pinned() = default;
    Pinned(Pinned const&) = delete;
    Pinned(Pinned&&) = delete;
};

}  // namespace

int main() {
    auto slot = fixy::mint_write_once<Pinned>();
    return slot.has_value() ? 1 : 0;
}
