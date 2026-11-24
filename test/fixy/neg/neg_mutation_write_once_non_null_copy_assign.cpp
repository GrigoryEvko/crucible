// A copy assignment would overwrite a set slot with another pointer.
// WriteOnceNonNull deletes both assignments, so the copy assignment below
// names a deleted function.

#include <fixy/Core.h>
#include <fixy/Mutation.h>

int main() {
    int first = 1;
    int second = 2;
    auto other = fixy::mint_write_once_non_null<int*>();
    static_cast<void>(other.set(fixy::to(second)));
    auto slot = fixy::mint_write_once_non_null<int*>();
    static_cast<void>(slot.set(fixy::to(first)));
    slot = other;
    return slot.has_value() ? 0 : 1;
}
