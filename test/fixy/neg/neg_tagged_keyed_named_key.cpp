// Negative-compile test.  This file must fail to compile.
//
// Violation: a door names a key and passes it to the keyed retag.
//
// A key cannot copy or move, so the keyed retag takes only a prvalue key.  A
// key with a name cannot reach the retag, so no door keeps a key or lends
// one to other code.

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

struct Door {
    static ::fixy::Tagged<int, Checked> retag(::fixy::Tagged<int, tags::source::External>&& raw) {
        Key key{};
        return ::fixy::retag<Checked>(std::move(raw), std::move(key));
    }
};

int main() { return Door::retag(::fixy::mint_tagged<tags::source::External>(7)).value(); }
