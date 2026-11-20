// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A Ref of an object with a named brand does not convert to the erased
// brand.  Each erased Ref of one target is one type, so an implicit erasure
// would let a borrow of one object stand in for a borrow of another, at a
// site that names no drop.  erase_brand(ref) is the one door, and
// decltype(erase_brand(branded)) spells the erased Ref here.

#include <foundation/core/Ref.h>

namespace {
struct NamedBrand {};
struct Carrier {
    using brand_type = NamedBrand;
    int value = 1;
};
}  // namespace

int main() {
    Carrier carrier{};
    auto const branded = ::foundation::core::to(carrier);
    using Erased = decltype(::foundation::core::erase_brand(branded));
    Erased const erased = branded;
    return erased->value;
}
