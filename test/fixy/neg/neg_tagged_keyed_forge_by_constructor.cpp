// Negative-compile test.  This file must fail to compile.
//
// Violation: a Tagged of a keyed tag from its value, by the constructor.
//
// The constructor of Tagged is private, and the class is no aggregate.  The
// keyed retag is the one friend that builds a keyed claim.

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
    ::fixy::Tagged<int, Checked> forged{7};
    return forged.value();
}
