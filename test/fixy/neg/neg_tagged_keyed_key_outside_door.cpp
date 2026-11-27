// Negative-compile test.  This file must fail to compile.
//
// Violation: the keyed retag with a key that code outside the door writes.
//
// The constructor of the key is private, and only the friends of the key,
// the doors of the check, can write Key{}.

#include <fixy/Tagged.h>

#include <utility>

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
    auto raw = ::fixy::mint_tagged<tags::source::External>(7);
    auto forged = ::fixy::retag<Checked>(std::move(raw), Key{});
    return forged.value();
}
