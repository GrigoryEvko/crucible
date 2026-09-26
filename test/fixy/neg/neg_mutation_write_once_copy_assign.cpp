// A copy assignment would overwrite a set slot with another value.
// WriteOnce deletes both assignments, so the copy assignment below names
// a deleted function.

#include <fixy/Mutation.h>

int main() {
    auto other = fixy::mint_write_once<int>();
    other.set(2);
    auto slot = fixy::mint_write_once<int>();
    slot.set(1);
    slot = other;
    return slot.get();
}
