// An assignment from a fresh mint would empty a set slot, and set would
// then succeed a second time.  WriteOnceNonNull deletes both assignments,
// so the move assignment below names a deleted function.

#include <fixy/Mutation.h>

int main() {
    static int first = 1;
    static int second = 2;
    auto slot = fixy::mint_write_once_non_null<int*>();
    slot.set(&first);
    slot = fixy::mint_write_once_non_null<int*>();
    slot.set(&second);
    return *slot;
}
