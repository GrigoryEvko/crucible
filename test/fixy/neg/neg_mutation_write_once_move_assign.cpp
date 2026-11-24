// An assignment from a fresh mint would empty a set slot, and set would
// then succeed a second time.  WriteOnce deletes both assignments, so the
// move assignment below names a deleted function.

#include <fixy/Mutation.h>

int main() {
    auto slot = fixy::mint_write_once<int>();
    static_cast<void>(slot.set(1));
    slot = fixy::mint_write_once<int>();
    return slot.has_value() ? 0 : 1;
}
