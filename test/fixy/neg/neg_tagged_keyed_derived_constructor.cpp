// Negative-compile test.  This file must fail to compile.
//
// Violation: a derived class takes the constructors of a Tagged of a keyed
// tag, and builds one from a value.
//
// An inherited constructor keeps its access, so the private constructor of
// Tagged stays private in the derived class.

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

struct Forge : ::fixy::Tagged<int, Checked> {
    using ::fixy::Tagged<int, Checked>::Tagged;
};

int main() {
    Forge forged{7};
    return forged.value();
}
