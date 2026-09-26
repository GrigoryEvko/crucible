// A copy assignment would overwrite a set slot with another pointer.
// WriteOnceNonNull deletes both assignments, so the copy assignment below
// names a deleted function.

#include <fixy/Mutation.h>

int main() {
    static int first = 1;
    static int second = 2;
    auto other = fixy::mint_write_once_non_null<int*>();
    other.set(&second);
    auto slot = fixy::mint_write_once_non_null<int*>();
    slot.set(&first);
    slot = other;
    return *slot;
}
