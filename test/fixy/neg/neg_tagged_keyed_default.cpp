// Negative-compile test.  This file must fail to compile.
//
// Violation: a default Tagged of a keyed tag.
//
// The empty-slot door is shut for an earned tag: a default value would claim
// that a check passed on a value that no check saw.  A keyed tag is earned.

#include <fixy/Tagged.h>

namespace tags = ::fixy::tags;

class Key;
struct Checked {
    using retag_key = Key;
    using earned_from = tags::source::External;
};
class Key {
    Key() = default;
    friend struct Door;

public:
    Key(const Key&) = delete;
    Key(Key&&) = delete;
};

int main() {
    ::fixy::Tagged<int, Checked> forged{};
    return forged.value();
}
