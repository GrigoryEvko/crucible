// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-086 HS14 fixture #2 of 2 for BackgroundThread::Region
// ReadyCallback::Fn:
// Type-identity drift detector — `RegionReadyCallback::Fn` MUST be
// the noexcept-qualified function-pointer type, NOT the bare
// non-noexcept variant.
//
// Why this matters: a future refactor that drops the noexcept
// specifier from the typedef (e.g., misguided "make the API
// flexible by accepting any callable") would silently break the
// load-bearing discipline.  This fixture pins the type-identity
// at the SURFACE: `Fn` is `void(*)(void*, RegionNode*) noexcept`,
// nothing else.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// TYPE-IDENTITY half — the typedef MUST NOT be is-same with the
// non-noexcept variant.  Sibling fixture
// `neg_background_thread_callback_throwing_fnptr_rejected.cpp`
// exercises the ASSIGNMENT-CONVERSION half (a non-noexcept fn-ptr
// cannot convert).
//
// Expected diagnostic: "static assertion failed|noexcept|
// RegionReadyCallback|Fn".

#include <crucible/BackgroundThread.h>

#include <type_traits>

namespace c = crucible;

// The non-noexcept variant of the function-pointer type.  Post-V-086,
// `RegionReadyCallback::Fn` MUST NOT be is-same with this type.
//
// We assert the NEGATIVE direction below — a correct V-086 surface
// reddens the assertion at compile time.
using non_noexcept_fn = void (*)(void*, c::RegionNode*);

static_assert(std::is_same_v<c::BackgroundThread::RegionReadyCallback::Fn,
                              non_noexcept_fn>,
    "FIXY-V-086 HS14 fixture #2: RegionReadyCallback::Fn MUST be the "
    "noexcept-qualified function-pointer type; if this static_assert "
    "passes, the noexcept tightening has regressed and a throwing "
    "callback would silently wire into the BG-thread region-ready "
    "path.");

int main() { return 0; }
