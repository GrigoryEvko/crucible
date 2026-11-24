// The set of a WriteOnce gives a Ref of the value in the slot.  A temporary
// slot ends at the end of the statement, and the Ref would name its storage
// after that.  The set of an rvalue slot is deleted.

#include <fixy/Mutation.h>

int main() {
    auto const view = fixy::mint_write_once<int>().set(1);
    return *view;
}
