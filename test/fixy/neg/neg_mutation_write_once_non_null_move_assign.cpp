// An assignment from a fresh mint would empty a set slot, and set would
// then succeed a second time.  WriteOnceNonNull deletes both assignments,
// so the move assignment below names a deleted function.

#include <fixy/Core.h>
#include <fixy/Mutation.h>

int main() {
    int first = 1;
    auto slot = fixy::mint_write_once_non_null<int*>();
    static_cast<void>(slot.set(fixy::to(first)));
    slot = fixy::mint_write_once_non_null<int*>();
    return slot.has_value() ? 0 : 1;
}
