// A log gives each element as a const object, and it owns its storage.  So the
// door of the log takes an element type that is not const.

#include <fixy/Mutation.h>

int main() {
    auto log = fixy::mint_append_only<int const>();
    return static_cast<int>(log.size());
}
