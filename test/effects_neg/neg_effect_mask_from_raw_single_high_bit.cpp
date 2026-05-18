// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy-A3-022 (EffectMask::from_raw
// wire-format hardening).
//
// Premise: EffectMask::from_raw is the public deserialization /
// interop escape on the runtime dual of `Row<Es...>`.  It accepts
// an underlying_type (uint8_t) and lifts it into an EffectMask
// value that downstream consumers (federation cache keying,
// runtime drift detection, observe metrics broadcast) trust as
// well-formed.  If from_raw silently admits bits that correspond
// to NO Effect atom, a malicious or buggy peer can wire-poison
// every downstream consumer: a federation peer that sends
// `from_raw(0x80)` causes a fresh cache-slot collision against
// `from_raw(0x00)` after `& valid_mask` normalization, or worse,
// passes through unchanged and computes a row hash that no other
// peer can ever reproduce.
//
// The fix (fixy-A3-022) plants CRUCIBLE_PRE inside from_raw's
// body to reject unknown bits at construction.  effect_count is
// reflection-derived from the Effect enum (currently 6 atoms:
// Alloc=0 .. Test=5), so the valid-bit mask is
// `(1u << effect_count) - 1` = `0x3F`.  Any bit at position
// ≥ effect_count is poison and must trigger __builtin_trap at
// consteval / contract_failed at runtime.
//
// This fixture: WITNESS BIT 7 ALONE.  `0x80` corresponds to no
// Effect atom (positions 6 and 7 are both outside the enum).
// The simplest possible witness — a single bit beyond the valid
// range.  An impl that masked-on-read (e.g. `return EffectMask{b
// & 0x3F}`) would COMPILE this fixture but silently corrupt the
// round-trip property `from_raw(b).raw() == b`.  The correct
// fixed impl rejects the input outright via CRUCIBLE_PRE.
//
// Companion fixture (neg_effect_mask_from_raw_high_bits.cpp)
// pins the dual-bit case `0xC0` — both unknown bits set.
// Together they witness that the contract fires whether one OR
// both poisoned bits are present, defending against the
// half-broken "I check only the top bit" implementation.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/effects/EffectRowProjection.h>

#include <cstdint>

namespace eff = crucible::effects;

namespace {

// Force the CRUCIBLE_PRE consteval branch by binding the result
// into a constexpr variable.  At runtime under NDEBUG the
// precondition collapses to [[assume]] and the code compiles; at
// consteval (this fixture), the poisoned bit fires
// __builtin_trap which poisons the surrounding constant
// expression.  static_assert downstream then refuses to evaluate
// the binding.
constexpr auto witness = eff::EffectMask::from_raw(std::uint8_t{0x80});

}  // namespace

int main() { (void)witness; return 0; }
