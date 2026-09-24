// A closure takes no label word.
//
// Two distinct closure types can print one name, so their keys would
// share a word, and the peer would enter the wrong branch.  The label
// word comes from a stable id, and every stable id refuses a type that
// names a closure, so the collision never reaches the wire.

#include <foundation/algebra/Transition.h>

namespace closure_label {
using Key = decltype([] {});
}  // namespace closure_label

int main() {
    constexpr std::uint64_t word = ::foundation::algebra::transition::label_word_of(^^closure_label::Key);
    return word == 0 ? 1 : 0;
}
