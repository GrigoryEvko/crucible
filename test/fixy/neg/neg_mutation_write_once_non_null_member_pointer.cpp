// A pointer to a member has a null value but is not an object pointer, and
// the slot dereferences what it holds.  The door refuses it.

#include <fixy/Mutation.h>

namespace {

struct Holder {
    int value = 0;
};

}  // namespace

int main() {
    auto slot = fixy::mint_write_once_non_null<int Holder::*>();
    return slot.has_value() ? 1 : 0;
}
