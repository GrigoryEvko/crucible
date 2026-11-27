// Negative-compile test.  This file must fail to compile.
//
// Violation: the member retag into a keyed tag, with no key.
//
// RetagAllowed admits no pair into a keyed tag.  Only the keyed retag enters
// it, with the key of the check.

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
    auto forged = ::fixy::mint_tagged<tags::source::External>(7).retag<Checked>();
    return forged.value();
}
