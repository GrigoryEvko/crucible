// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004a-AUDIT (#1288): SenseHub::load() takes a `effects::Init`
// capability tag by value as its sole parameter.  The cap-typing
// gate is structural — `effects::Bg`, `effects::Test`, and any
// other context type are DISTINCT 1-byte structs with NO implicit
// conversion to `effects::Init` (each has its own [[no_unique_address]]
// cap::* member layout).  This file proves that gate fires: a hot-path
// or background-thread frame holding only a `Bg` token cannot
// accidentally reach the startup-only loader.
//
// The structural promise — "hot-path frames hold no `effects::Init`
// token and therefore cannot construct the argument; the absence of
// the cap is the type-system gate that prevents accidental hot-path
// SenseHub::load() calls" — is asserted in the SenseHub.h docblock at
// lines 234-240.  This test is the regression-witness for that claim.
//
// Violation: passes `effects::Bg{}` where `effects::Init{}` is required.
// The compiler should fail with "no matching function" / "could not
// convert" / "expected" pointing at the parameter type mismatch.
//
// Expected diagnostic: "could not convert|no matching function|cannot
// convert|expected.*Init" — any of these proves the gate fires
// regardless of toolchain wording.

#include <crucible/perf/SenseHub.h>
#include <crucible/effects/Capabilities.h>

#include <optional>

int main() {
    // Bg is a Bg-frame context (Alloc + IO + Block); load() requires
    // an Init-frame context (Alloc + IO).  These are DISTINCT 1-byte
    // structs — no implicit conversion exists or should exist.
    crucible::effects::Bg bg_cap{};

    // <-- this line must NOT compile
    std::optional<crucible::perf::SenseHub> hub =
        crucible::perf::SenseHub::load(bg_cap);

    (void)hub;
    return 0;
}
