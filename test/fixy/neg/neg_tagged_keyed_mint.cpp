// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_tagged of a keyed tag.
//
// A keyed tag names a check that ran, and only the keyed retag of a door of
// the check reaches it.  The factory admits only a mintable tag.

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
    auto forged = ::fixy::mint_tagged<Checked>(7);
    return forged.value();
}
