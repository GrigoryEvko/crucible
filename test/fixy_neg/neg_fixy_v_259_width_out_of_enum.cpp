// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-259 fixture 1/2 — out-of-enum width (garbage value).
//
// `width<W>` is gated by `is_known_width_v<W>`.  A WidthBits value that
// is not one of the six enumerated register-width classes (reachable
// only via an explicit static_cast) is ill-formed at the grant
// template-id.  777 is not a SIMD register width on any architecture.
//
// Mismatch class: garbage width value.  Distinct from
// neg_fixy_v_259_width_unlisted_64.cpp (a plausible-looking but
// unenumerated width).
//
// Expected diagnostic: "constraints not satisfied" / "is_known_width" /
// "width".

#include <crucible/fixy/Simd.h>

int main() {
    namespace fs = ::crucible::fixy::simd;
    namespace gs = ::crucible::fixy::grant::simd;
    // Should FAIL: 777 is not an enumerated WidthBits class.
    [[maybe_unused]] gs::width<static_cast<fs::WidthBits>(777)> bad{};
    return 0;
}
