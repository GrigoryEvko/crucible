// The hash of the Protocol family folds each base of a class, so it refuses
// the state of a base that it refuses in the class itself.  A View holds an
// address.  A fold that skips the bases compiles this file, and it gives one
// hash for two values that differ only in the base.

#include <foundation/core/Protocol.h>

struct[[= ::foundation::core::derive_protocol{}]] HoldsView {
    ::foundation::core::View<int const> elements{};
};

struct[[= ::foundation::core::derive_protocol{}]] DerivedFromView : HoldsView {
    unsigned own = 0;
};

int main() {
    DerivedFromView const value{};
    return static_cast<int>(::foundation::core::hash(value).raw() & 1u);
}
