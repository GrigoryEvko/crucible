// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-246 HS14 fixture (Global 2/2) — singleton expects a tag TYPE.
//
// `grant::global::singleton<GlobalTag>`'s parameter is a TYPE (the unique
// phantom tag naming the singleton).  Substituting a VALUE (here `42`)
// where a type is expected is rejected at template-id formation — a
// singleton cannot be identified by an integer.
//
// Mismatch class: value-where-TYPE-expected (distinct from the
// thread_local_ missing-tag arity path).
//
// Expected diagnostic: a GCC "expected a type" / "type/value mismatch"
// / "invalid" template-argument error.

#include <crucible/fixy/grant/Global.h>

namespace glb = crucible::fixy::grant::global;

using Bad = glb::singleton<42>;  // 42 is a value; GlobalTag is a type

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
