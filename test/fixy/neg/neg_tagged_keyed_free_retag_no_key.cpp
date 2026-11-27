// Negative-compile test.  This file must fail to compile.
//
// Violation: the free retag of one argument into a keyed tag.
//
// The free retag of one argument admits the same pairs as the member form,
// and no pair enters a keyed tag.

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
    auto forged = ::fixy::retag<Checked>(std::move(raw));
    return forged.value();
}
