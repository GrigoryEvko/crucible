// The borrow of a word takes the key of its authority, which only a member of
// the authority builds.  A call with no key finds no door.

#include <foundation/core/Ref.h>

struct Channel {};

int main() {
    ::foundation::core::BoxWord<int, Channel> const word{};
    return *word.borrow();
}
