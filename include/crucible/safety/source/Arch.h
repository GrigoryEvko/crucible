// safety/source/Arch.h — CPU-host architecture pin tag + cross-arch
// composition gate.
//
// FIXY-V-261 (Agent 11 §3.7 dependency).  Ships the `source::ArchPinned
// <Arch>` provenance tag — the type-level witness that a value (or the
// code that produced it) is bound to one CPU instruction-set trunk —
// plus the cross-arch COMPOSITION GATE (`ArchComposable`) and the V-022
// retag_policy admittances for the one sound relabel direction.
//
// ── Why an arch pin exists ──────────────────────────────────────────
//
// Memory fences and SIMD intrinsics are architecture-specific: `mfence`
// /`lfence`/`sfence` and the SSE..AVX-512 vector ISA on x86-64; `DMB
// ISH` and NEON/SVE on aarch64.  A value carrying the result of a fenced
// or hand-vectorized computation is naturally pinned to the trunk that
// issued those instructions.  V-255 (safety/BarrierGuarded.h) anticipated
// exactly this slot: `Tagged<BarrierGuarded<AcqRel, T>, source::Arch
// Pinned<X86>>`.  V-256 (safety/SimdWidthPinned.h) carries the same
// trunk distinction inside the SimdIsaLattice (FIXY-V-250) at finer
// granularity.  This header is the COARSE arch axis — just the trunk,
// not the per-trunk capability rank.
//
// ── Why a dedicated ArchTag enum (not IsaFamily) ───────────────────
//
// `fixy::Vendor.h`'s `IsaFamily` (FIXY-V-258) spans CPU + GPU families
// (X86, Arm, CudaPtx, Amdgcn, Tpu, Trn).  ArchPinned is specifically
// about the CPU-HOST binary's instruction trunk — which fence dialect
// and which scalar/vector ISA the emitted code targets — so it needs
// only the two CPU trunks plus a Portable ⊤ for arch-agnostic values.
// A focused three-value enum keeps the cross-arch gate trivial and
// keeps one enum-class for grep-discoverability (the TransportPosture /
// version::V<N> precedent in Tagged.h).
//
// ── The composition gate (the load-bearing deliverable) ────────────
//
// `arch_compatible(a, b)` is the trunk-compatibility relation, mirroring
// the SimdIsaLattice's cross-trunk semantics: two pins compose iff they
// are equal OR either is Portable (⊤, runs on every trunk).  The ONLY
// rejection is two CONCRETE, DIFFERENT trunks (X86 × Arm) — an x86+ARM
// binding can never run: the emitted binary would `#UD` on whichever
// ISA it lands.  This is the source-tag-level companion to V-260's
// V002 catalog rule (`marks_vendor_cross_arch`), which catches the same
// hazard at the Fn-binding grant-pack level.
//
// `ArchComposable<SourceA, SourceB>` lifts the relation to types: a
// non-arch-pinned source maps to Portable (no arch constraint), so it
// composes freely; only ArchPinned<X86> against ArchPinned<Arm> is
// rejected.
//
// ── retag direction discipline ─────────────────────────────────────
//
// Following the SimdIsaLattice direction convention (stronger capability
// = higher; Portable = ⊤ "runs everywhere"):
//
//   Portable → X86 / Arm:  WEAKENING — relabelling a universal value's
//                          claim down to one concrete trunk claims LESS
//                          than the value provides (it does run on x86).
//                          Sound; ADMITTED by the V-261 catalog below
//                          (the BarrierGuarded `weaken<Lower>()` analog).
//   X86 → Portable:        WIDENING — claiming x86-pinned code runs on
//                          every trunk is a lie (it `#UD`s on ARM).
//                          REJECTED by the V-022 fail-closed primary.
//   X86 → Arm (and inv.):  cross-trunk relabel — equally unsound.
//                          REJECTED by the fail-closed primary.
//   X → X:                 identity — admitted by V-022's identity spec.
//
// ── Axiom coverage ────────────────────────────────────────────────
//
//   InitSafe   — ArchPinned<Arch> is an empty struct (one static
//                constexpr member, no storage); Tagged<T, ArchPinned>
//                inherits T's init discipline + the empty-element grade.
//   TypeSafe   — ArchTag is a strong scoped enum; ArchPinned<X86> and
//                ArchPinned<Arm> are distinct types — passing one where
//                the other is expected is a compile error.
//   NullSafe   — tags carry no pointer state.
//   MemSafe    — empty POD tags; zero size, zero lifetime concerns;
//                EBO-collapse in Tagged is preserved (sizeof unchanged).
//   BorrowSafe — `RetagAllowed<>` rejects cross-trunk + widening retag
//                at compile time (V-024 wiring).
//   ThreadSafe — tag identity is compile-time only; no runtime state.
//   LeakSafe   — empty structs, no destructors.
//   DetSafe    — tag identity is byte-deterministic across builds and
//                platforms; the retag transition produces no runtime
//                artifact — purely phantom.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  ArchPinned<Arch> emits no symbols; arch_compatible / arch_pin_v
// / arch_composable_v fold at compile time; the retag_policy
// specializations are constexpr `static constexpr bool allowed`; the
// in-header self-test is static_assert-only.

#pragma once

#include <crucible/safety/Tagged.h>  // safety::source::*, retag_policy, RetagAllowed

#include <type_traits>

namespace crucible::safety::source {

// ── source::ArchTag ─────────────────────────────────────────────────
//
// The CPU-host instruction-set trunk a value is pinned to.  Two concrete
// trunks (X86, Arm) plus Portable (⊤): arch-agnostic code that runs on
// either trunk.  One enum class for grep-discoverability.
enum class ArchTag : unsigned char {
    X86      = 0,  // x86-64 host: mfence/lfence/sfence, SSE..AVX-512
    Arm      = 1,  // aarch64 host: DMB ISH, NEON/SVE
    Portable = 2,  // arch-agnostic ⊤: runs on either trunk
};

// arch_compatible — the trunk-compatibility relation.  Two pins compose
// iff equal OR either is Portable (⊤).  The sole incompatibility is two
// distinct concrete trunks (X86 vs Arm): an x86+ARM binding `#UD`s on
// whichever ISA it lands.  Mirrors SimdIsaLattice cross-trunk leq.
[[nodiscard]] constexpr bool arch_compatible(ArchTag a, ArchTag b) noexcept {
    return a == b || a == ArchTag::Portable || b == ArchTag::Portable;
}

// source::ArchPinned<Arch> — the provenance tag.  Empty (one static
// constexpr member, no storage) so it EBO-collapses in Tagged<T, Arch
// Pinned<Arch>> exactly like every other source:: tag.
template <ArchTag Arch>
struct ArchPinned {
    static constexpr ArchTag arch = Arch;
};

// Canonical aliases — the ergonomic spelling used at composition sites.
using X86Pinned      = ArchPinned<ArchTag::X86>;
using ArmPinned      = ArchPinned<ArchTag::Arm>;
using PortablePinned = ArchPinned<ArchTag::Portable>;

}  // namespace crucible::safety::source

namespace crucible::safety {

// ── arch_pin_v<Source> — the trunk a source pins to ────────────────
//
// ArchPinned<A> → A.  EVERY OTHER source maps to Portable (⊤): a value
// with no arch pin imposes no trunk constraint, so it composes freely
// with any pin.  This default is what makes the composition gate
// vacuously true for non-arch-pinned operands — the only way the gate
// fails is two concrete, different trunks meeting.
template <typename Source>
inline constexpr source::ArchTag arch_pin_v = source::ArchTag::Portable;
template <source::ArchTag Arch>
inline constexpr source::ArchTag arch_pin_v<source::ArchPinned<Arch>> = Arch;

// is_arch_pinned_v<Source> — does Source carry an ArchPinned tag at all?
// (True even for ArchPinned<Portable> — that IS an explicit arch pin,
// distinct from "no pin".)
template <typename Source>
inline constexpr bool is_arch_pinned_v = false;
template <source::ArchTag Arch>
inline constexpr bool is_arch_pinned_v<source::ArchPinned<Arch>> = true;

// ── arch_composable_v / ArchComposable — THE CROSS-ARCH GATE ───────
//
// SourceA and SourceB compose iff their trunk pins are compatible.  The
// only rejection is two concrete, different trunks (X86 × Arm).  Portable
// composes with anything; non-arch-pinned sources (Portable by default)
// impose no constraint.
template <typename SourceA, typename SourceB>
inline constexpr bool arch_composable_v =
    source::arch_compatible(arch_pin_v<SourceA>, arch_pin_v<SourceB>);

template <typename SourceA, typename SourceB>
concept ArchComposable = arch_composable_v<SourceA, SourceB>;

// ── retag_policy admittances (FIXY-V-261) ──────────────────────────
//
// Only the sound WEAKENING direction (Portable → concrete trunk) is
// admitted.  Widening (concrete → Portable) and cross-trunk relabel
// (X86 ↔ Arm) stay REJECTED by the V-022 fail-closed primary template.
template <> struct retag_policy<source::PortablePinned, source::X86Pinned> {
    // Sound: a value claiming "runs on every trunk" may be relabelled to
    // claim only "runs on x86" — that claims LESS than the value provides.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<source::PortablePinned, source::ArmPinned> {
    // Symmetric: Portable → Arm is the same sound weakening.
    static constexpr bool allowed = true;
};

}  // namespace crucible::safety

// ── FIXY-V-261 self-test — pin the gate at sentinel-TU compile ─────
//
// Witnesses the load-bearing properties:
//   1. The three arch pins are distinct types (TypeSafe);
//   2. arch_compatible truth table (same / cross / portable);
//   3. arch_pin_v extraction (incl. non-pinned → Portable default);
//   4. ArchComposable accepts compatible, rejects cross-trunk;
//   5. retag_policy admits only Portable → concrete; rejects the rest.
// If any flips, the build breaks HERE, not at downstream composition
// sites.
namespace crucible::safety::source::detail::v261_self_test {

namespace ss = ::crucible::safety;

// ── (1) Tag distinctness ───────────────────────────────────────────
static_assert(!std::is_same_v<X86Pinned, ArmPinned>,
    "FIXY-V-261: X86Pinned and ArmPinned must be distinct types — "
    "an x86 binary does not run on ARM and vice versa.");
static_assert(!std::is_same_v<X86Pinned, PortablePinned>,
    "FIXY-V-261: X86Pinned and PortablePinned must be distinct types.");
static_assert(!std::is_same_v<ArmPinned, PortablePinned>,
    "FIXY-V-261: ArmPinned and PortablePinned must be distinct types.");

// ArchPinned<Arch>::arch member round-trips the template parameter.
static_assert(X86Pinned::arch      == ArchTag::X86);
static_assert(ArmPinned::arch      == ArchTag::Arm);
static_assert(PortablePinned::arch == ArchTag::Portable);

// ── (2) arch_compatible truth table ────────────────────────────────
static_assert(arch_compatible(ArchTag::X86, ArchTag::X86),
    "FIXY-V-261: same trunk must compose.");
static_assert(arch_compatible(ArchTag::Arm, ArchTag::Arm),
    "FIXY-V-261: same trunk must compose.");
static_assert(!arch_compatible(ArchTag::X86, ArchTag::Arm),
    "FIXY-V-261: x86 × ARM must NOT compose — the binary would #UD.");
static_assert(!arch_compatible(ArchTag::Arm, ArchTag::X86),
    "FIXY-V-261: cross-trunk is symmetric — ARM × x86 must NOT compose.");
static_assert(arch_compatible(ArchTag::X86, ArchTag::Portable),
    "FIXY-V-261: Portable ⊤ composes with x86.");
static_assert(arch_compatible(ArchTag::Portable, ArchTag::Arm),
    "FIXY-V-261: Portable ⊤ composes with ARM.");
static_assert(arch_compatible(ArchTag::Portable, ArchTag::Portable),
    "FIXY-V-261: Portable composes with itself.");

// ── (3) arch_pin_v extraction ──────────────────────────────────────
static_assert(ss::arch_pin_v<X86Pinned>      == ArchTag::X86);
static_assert(ss::arch_pin_v<ArmPinned>      == ArchTag::Arm);
static_assert(ss::arch_pin_v<PortablePinned> == ArchTag::Portable);
// Non-arch-pinned sources default to Portable (no constraint).
static_assert(ss::arch_pin_v<External>       == ArchTag::Portable,
    "FIXY-V-261: a non-arch-pinned source must default to Portable so "
    "it imposes no trunk constraint on composition.");
static_assert(ss::is_arch_pinned_v<X86Pinned>,
    "FIXY-V-261: X86Pinned IS an arch pin.");
static_assert(!ss::is_arch_pinned_v<External>,
    "FIXY-V-261: a generic provenance tag is NOT an arch pin.");

// ── (4) ArchComposable concept ─────────────────────────────────────
static_assert(ss::ArchComposable<X86Pinned, X86Pinned>,
    "FIXY-V-261: same-trunk composition must be admitted.");
static_assert(ss::ArchComposable<X86Pinned, PortablePinned>,
    "FIXY-V-261: concrete × Portable must be admitted.");
static_assert(ss::ArchComposable<X86Pinned, External>,
    "FIXY-V-261: a concrete pin × a non-arch source must be admitted "
    "(the non-arch source imposes no trunk constraint).");
static_assert(!ss::ArchComposable<X86Pinned, ArmPinned>,
    "FIXY-V-261: x86 × ARM composition MUST be rejected by the gate.");
static_assert(!ss::ArchComposable<ArmPinned, X86Pinned>,
    "FIXY-V-261: the gate is symmetric — ARM × x86 must be rejected.");

// ── (5) retag_policy direction discipline ──────────────────────────
// Sound weakening admitted.
static_assert(ss::retag_policy<PortablePinned, X86Pinned>::allowed,
    "FIXY-V-261: Portable → x86 is a sound weakening; must be admitted.");
static_assert(ss::retag_policy<PortablePinned, ArmPinned>::allowed,
    "FIXY-V-261: Portable → ARM is a sound weakening; must be admitted.");
static_assert(ss::RetagAllowed<PortablePinned, X86Pinned>,
    "FIXY-V-261: the V-024 RetagAllowed concept must see the Portable → "
    "x86 admittance.");
// Identity admitted by V-022's identity specialization.
static_assert(ss::retag_policy<X86Pinned, X86Pinned>::allowed,
    "FIXY-V-261: identity retag (X → X) must be admitted by V-022.");
// Widening rejected (false claim that x86 code runs everywhere).
static_assert(!ss::retag_policy<X86Pinned, PortablePinned>::allowed,
    "FIXY-V-261: x86 → Portable is a false widening (x86 code #UDs on "
    "ARM); must stay rejected by the fail-closed primary.");
// Cross-trunk relabel rejected.
static_assert(!ss::retag_policy<X86Pinned, ArmPinned>::allowed,
    "FIXY-V-261: x86 → ARM cross-trunk relabel must stay rejected.");
static_assert(!ss::retag_policy<ArmPinned, X86Pinned>::allowed,
    "FIXY-V-261: ARM → x86 cross-trunk relabel must stay rejected.");
static_assert(!ss::RetagAllowed<X86Pinned, ArmPinned>,
    "FIXY-V-261: the V-024 RetagAllowed concept must reject cross-trunk "
    "retag.");

}  // namespace crucible::safety::source::detail::v261_self_test
