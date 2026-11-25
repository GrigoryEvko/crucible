// A function returns a reference to an element of a local log.  The log ends
// with the frame of the function and frees the element, so a read of the result
// reads freed storage.  The rule view-escape refuses the return.

#include <fixy/Mutation.h>
#include <foundation/effects/Effect.h>

namespace {

int const& element_of_local_log() {
    auto log = fixy::mint_append_only<int>();
    log.append(::foundation::effects::testing::test().alloc, 3);
    return log[0];
}

}  // namespace

int main() { return element_of_local_log(); }
