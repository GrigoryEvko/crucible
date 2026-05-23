// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-245 HS14 fixture 2/3 — virtual_call expects a base TYPE.
//
// `grant::dispatch::virtual_call<BaseClass>`'s template parameter is a
// TYPE (the polymorphic base whose vtable is crossed).  Substituting a
// VALUE (here `42`) where a type is expected is rejected at template-id
// formation — the grant cannot be parameterized by an integer.
//
// Why this matters: virtual_call is the FFI-compat escape hatch for
// crossing a vtable boundary; its parameter names WHICH hierarchy is
// crossed.  A value there is a category error the type system must catch
// at the spelling site, not silently coerce.
//
// Mismatch class for HS14 audit: value-where-TYPE-expected — distinct
// from the recurses missing-bound arity path (fixture 1) and the
// IsGrantTag cv-purity path (fixture 3).
//
// Expected diagnostic: a GCC "expected a type" / "type/value mismatch"
// / "invalid" template-argument error.

#include <crucible/fixy/grant/Dispatch.h>

namespace disp = crucible::fixy::grant::dispatch;

// 42 is a value; virtual_call's template parameter is a class type.
using Bad = disp::virtual_call<42>;

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
