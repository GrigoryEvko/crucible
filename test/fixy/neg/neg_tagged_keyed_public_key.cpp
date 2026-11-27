// Negative-compile test.  This file must fail to compile.
//
// Violation: a tag that names a key with a public constructor.
//
// Each code can write such a key, so the tag would record no check.  A tag
// that names a retag_key must be a KeyedTag in full, so the Tagged of such a
// tag does not compile.

#include <fixy/Tagged.h>

namespace tags = ::fixy::tags;

struct PublicKey {
    PublicKey() = default;
    PublicKey(const PublicKey&) = delete;
    PublicKey(PublicKey&&) = delete;
};
struct Checked {
    using retag_key = PublicKey;
    using earned_from = tags::source::External;
};

int main() { return static_cast<int>(sizeof(::fixy::Tagged<int, Checked>)); }
