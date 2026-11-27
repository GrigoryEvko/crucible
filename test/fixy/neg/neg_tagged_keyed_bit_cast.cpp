// Negative-compile test.  This file must fail to compile.
//
// Violation: std::bit_cast into a Tagged of a keyed tag.
//
// A keyed tag is earned, so the two assignments of its Tagged are
// user-provided and the class is not trivially copyable.  bit_cast refuses
// it, and no byte route builds the claim.

#include <fixy/Tagged.h>
#include <foundation/Quarantine.h>

#include <bit>

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

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture builds a keyed claim with std::bit_cast, and the compiler refuses it")
int main() {
    auto forged = std::bit_cast<::fixy::Tagged<int, Checked>>(7);
    return forged.value();
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
