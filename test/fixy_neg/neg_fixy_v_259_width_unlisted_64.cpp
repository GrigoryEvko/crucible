// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-259 fixture 2/2 — plausible-but-unlisted width (64-bit).
//
// `width<W>` is gated by `is_known_width_v<W>`.  64 LOOKS like a
// reasonable width (an MMX / half-NEON lane), but it is NOT one of the
// six enumerated register-width classes the grant admits {Scalar, 128,
// 256, 512, 1024, 2048}.  The gate refuses it — a kernel cannot pin a
// width the dispatch surface does not model.
//
// Mismatch class: plausible-but-unenumerated width.  Distinct from
// neg_fixy_v_259_width_out_of_enum.cpp (an obvious garbage value).
//
// Expected diagnostic: "constraints not satisfied" / "is_known_width" /
// "width".

#include <crucible/fixy/Simd.h>

int main() {
    namespace fs = ::crucible::fixy::simd;
    namespace gs = ::crucible::fixy::grant::simd;
    // Should FAIL: 64 is not an enumerated WidthBits class.
    [[maybe_unused]] gs::width<static_cast<fs::WidthBits>(64)> bad{};
    return 0;
}
