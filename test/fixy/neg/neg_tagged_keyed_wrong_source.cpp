// Negative-compile test.  This file must fail to compile.
//
// Violation: the keyed retag from a tag that is not the earned_from tag.
//
// A keyed tag names the one tag that its check takes.  A door that holds the
// key cannot move a value of another tag under it.

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
    static ::fixy::Tagged<int, Checked> retag(::fixy::Tagged<int, tags::source::FromUser>&& raw) {
        return ::fixy::retag<Checked>(std::move(raw), Key{});
    }
};

int main() { return Door::retag(::fixy::mint_tagged<tags::source::FromUser>(7)).value(); }
