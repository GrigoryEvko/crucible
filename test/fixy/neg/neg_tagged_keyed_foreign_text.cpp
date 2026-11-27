// Negative-compile test.  This file must fail to compile.
//
// Violation: the foreign text door with a keyed tag.
//
// The door of a foreign text admits only a boundary tag, which is mintable.
// A keyed tag is not mintable, so no foreign text carries a keyed claim.

#include <fixy/Core.h>
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

::fixy::ForeignText foreign_name() noexcept;

int main() {
    auto forged = ::fixy::mint_tagged<Checked>(foreign_name());
    return static_cast<int>(forged.value().size());
}
