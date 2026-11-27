// Negative-compile test.  This file must fail to compile.
//
// Violation: start_as_array of a Tagged of a keyed tag over bytes.
//
// The lifetime door of the base starts an array only of a type whose life
// can start over bytes.  The annotation of Tagged refuses that for each tag,
// so no array of a keyed claim starts over raw bytes.

#include <fixy/Tagged.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>

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

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture starts a keyed claim over raw bytes, and the compiler refuses it")
int main() {
    using Forged = ::fixy::Tagged<int, Checked>;
    alignas(Forged) unsigned char bytes[sizeof(Forged)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Forged>(bytes, 1);
    return static_cast<int>(forged.size());
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
