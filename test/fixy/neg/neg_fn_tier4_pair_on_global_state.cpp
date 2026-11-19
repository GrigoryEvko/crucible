// Tier 4 refuses two atoms on GlobalState.  GlobalState is a Lattice axis
// in the table of fixy/Axis.h, so it carries one grade, and the pack does
// not say whether the state is one object or one object for each thread.

#include <fixy/Fn.h>

namespace fixture_global {
struct counter final {};
}  // namespace fixture_global

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::global::singleton<fixture_global::counter>,
                                ::fixy::atom::global::thread_local_<fixture_global::counter>> refused{};
    return 0;
}
