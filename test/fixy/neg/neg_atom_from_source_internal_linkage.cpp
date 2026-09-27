// A provenance source with internal linkage as the argument of
// from_source.
//
// A class in an unnamed namespace is a different class in each
// translation unit, and each of them prints one name.  Two different
// sources would then share one key in the row hash, so IsAtom refuses the
// atom at tier 2, and the message names the identity read.

#include <fixy/Fn.h>

namespace {
struct private_source final {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::from_source<private_source>> refused{};
    return 0;
}
