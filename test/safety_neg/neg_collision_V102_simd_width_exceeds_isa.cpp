// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V102 (FIXY-V-260) — CONCEPT-ISOLATION path.
//
//     marks_simd_width_exceeds_isa<F>::value == true ⇒ V102_OK<F> false
//
// Plain English: a simd::width<W> grant whose W exceeds the bound vendor
// ISA family native width is unsound — the marquee "width<512> on an
// AVX2 binding" the V-259 sentinel reserved for this rule (AVX2 tops out
// at 256-bit; a 512-bit width emits instructions the target #UDs on).
// Reasons about cross-grant VALUE compatibility, so it ships as a
// default-SAFE marker the V-258/V-259 grant-pack analysis specializes.
//
// Mismatch class: marker-driven, asserted standalone on a non-Fn probe.
// Distinct from V101 (replay × pinned vector ISA, type-readable) — V102
// is the width-exceeds-ISA-family grant-pack case.
//
// Expected diagnostic substring: V102.

#include <crucible/safety/CollisionCatalog.h>

#include <type_traits>

namespace csc = crucible::safety::fn::collision;

namespace neg_collision_v102 {
struct Probe {};
}  // namespace neg_collision_v102

namespace crucible::safety::fn::collision {
    template <> struct marks_simd_width_exceeds_isa<::neg_collision_v102::Probe>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

static_assert(csc::V102_OK<::neg_collision_v102::Probe>,
              "V102: simd::width<W> exceeds the declared ISA family native width");

int main() { return 0; }
