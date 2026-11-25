// An array is no element of an owner, so the door of the log refuses it.  A
// fixed run of values is a FixedArray.

#include <fixy/Mutation.h>

int main() {
    auto log = fixy::mint_append_only<int[2]>();
    return static_cast<int>(log.size());
}
