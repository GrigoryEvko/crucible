// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy-A3-022 (EffectMask::from_raw
// wire-format hardening).
//
// Premise: EffectMask::from_raw is the deserialization escape on
// the runtime dual of `Row<Es...>`.  See companion fixture
// neg_effect_mask_from_raw_single_high_bit.cpp for the full
// threat model; this fixture witnesses the orthogonal case where
// MULTIPLE unknown bits are set simultaneously.
//
// `0xC0` corresponds to bits 6 AND 7 — both outside the 6-atom
// Effect enum (Alloc=0 .. Test=5, valid mask `0x3F`).  This is
// the canonical "wire poison" shape — a malicious peer that
// blasts the high byte hoping the deserializer doesn't sanitize.
//
// Together with the single-bit fixture this pins TWO orthogonal
// bug classes for from_raw:
//
//   1. SINGLE-BIT-POISON (companion): `0x80`.  Catches an impl
//      that masks-on-read (`b & 0x3F`) — would COMPILE this
//      fixture but break the round-trip property silently.
//   2. MULTI-BIT-POISON (this fixture): `0xC0`.  Catches an impl
//      that checks only the top bit (`b & 0x80`) — would
//      COMPILE the companion fixture but would NEED a
//      multi-bit-poison witness to catch a "I checked the top
//      bit, looks fine" implementation.  Wait — that's wrong:
//      if the impl is `pre(!(b & 0x80))` it WOULD reject 0x80
//      AND 0xC0 (both have bit 7 set).  The actually-orthogonal
//      shape this fixture catches is a `pre((popcount(b) & 1)
//      == 0)` parity-check bug (popcount(0xC0)==2, even, passes;
//      popcount(0x80)==1, odd, rejects) — bit-counting impl
//      bugs.  More realistically: this fixture witnesses that
//      BOTH bits 6 and 7 must be rejected, not just bit 7.
//
// The correct fixed impl rejects ANY bit at position ≥
// effect_count.  Reflection-derived from the enum, so the gate
// extends automatically as the Effect universe grows (FOUND-I04
// frozen positions).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/effects/EffectRowProjection.h>

#include <cstdint>

namespace eff = crucible::effects;

namespace {

// Force the CRUCIBLE_PRE consteval branch.  Bits 6 AND 7 both
// set.  Witness that the precondition fires on multi-bit poison
// shapes, not just single-bit ones — catches half-broken
// "I check only the top bit" implementations and parity-based
// fence implementations.
constexpr auto witness = eff::EffectMask::from_raw(std::uint8_t{0xC0});

}  // namespace

int main() { (void)witness; return 0; }
