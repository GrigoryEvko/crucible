// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The gate of extract in foundation/effects/Computation.h reads the
// annotation authority, and it reads an annotation only on a complete
// class.  This unit only declares Permission, so the gate cannot read the
// token that the payload names as a template argument.  An answer here
// could differ from the answer of a unit that defines Permission, so the
// gate stops the build.

#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Fwd.h>

namespace fe = ::foundation::effects;

namespace {
struct RegionTag {};
struct RegionBrand {};

// A carrier that names its argument and holds no value of it.
template <class T>
struct Phantom {};

using DeclaredToken = ::foundation::permissions::Permission<RegionTag, RegionBrand>;
}  // namespace

using PureOverDeclaredToken = fe::Computation<fe::Row<>, Phantom<DeclaredToken>>;

int main() {
    PureOverDeclaredToken pure = PureOverDeclaredToken::mint_computation(Phantom<DeclaredToken>{});

    // THE LOAD-BEARING LINE: must FAIL to compile.
    auto escaped = pure.extract();
    static_cast<void>(escaped);
    return 0;
}
