// FIXY-V-097 sentinel TU: DimensionAxis::SyscallSurface enumerator +
// SyscallFamilyLattice.h scaffolding.
//
// V-097 ships:
//   1. DimensionAxis::SyscallSurface (= 23) appended to the enum.
//   2. dimension_axis_name(SyscallSurface) → "SyscallSurface" arm.
//   3. tier_of_axis(SyscallSurface) → TierKind::Semiring.
//   4. DIMENSION_AXIS_COUNT bumped 23 → 24.
//   5. count_dims_in_tier(Semiring) bumped 18 → 19.
//   6. algebra/lattices/SyscallFamilyLattice.h — 9-element coarse
//      SyscallFamily chain (NoSyscall ⊏ VdsoOnly ⊏ ReadOnlyState
//      ⊏ FileMutation ⊏ MemoryMapping ⊏ ThreadSync ⊏ NetworkIo
//      ⊏ ProcessControl ⊏ Privilege) + ChainLatticeOps + At<T>
//      singleton + reflection-driven cardinality/name-coverage
//      self-test + exhaustive lattice-axiom verification.
//
// V-097 ships NO per-syscall machinery: V-098 ships
// fixy/syscall/Family.h + Per.h (coarse + per-syscall grants),
// V-099 ships Ioctl.h (per-vendor + kernel subsystems), V-100
// ships Bridge.h (automatic effect-row lift from syscall grants).
// This sentinel TU witnesses that the VOCABULARY change is
// structurally consistent.
//
// Why a dedicated SyscallSurface axis (not folded onto Effect):
//   Effect tracks Met(X) effect rows {Alloc, IO, Block, Bg, Init,
//     Test} — coarse semantic classes Crucible passes through its
//     execution-context discipline.  Syscall surfaces are FINER:
//     two ops in the same Effect row can have wildly different
//     kernel-attack-surface profiles (a vDSO-only clock_gettime vs
//     a setuid call BOTH bind under no Effect, but the latter is a
//     hot-path admission disaster).  Mimic per-vendor backends
//     decide ioctl-vs-mmap-vs-userspace paths on this axis;
//     Cipher tier-promotion decides fsync vs msync; Canopy peer-RX
//     decides recvmmsg vs io_uring on it.  Folding onto Effect
//     would lose the per-family granularity and force every
//     consumer to re-derive it from the binding's call-site
//     specification.

#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs   = ::crucible::safety;
namespace cal  = ::crucible::algebra::lattices;

namespace {

// ── DimensionAxis::SyscallSurface is the topmost (23rd) axis ────────
static_assert(std::to_underlying(cs::DimensionAxis::SyscallSurface) == 23,
    "FIXY-V-097: DimensionAxis::SyscallSurface must be the new topmost "
    "axis (ordinal 23).  Append-only discipline forbids reusing earlier "
    "ordinals — every Universe extension goes at the next free slot.");

// ── Tier classification — SyscallSurface is Tier-S (Semiring) ───────
static_assert(cs::tier_of_axis(cs::DimensionAxis::SyscallSurface)
              == cs::TierKind::Semiring,
    "FIXY-V-097: SyscallSurface lives on Tier-S (Semiring) — par=join "
    "(strictest-wins), peer to Synchronization (fixy-A3-008), Regime "
    "(fixy-A3-009), and FpMode (V-088).  Misclassification will break "
    "Mimic per-vendor backend's syscall-surface admission gate.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::SyscallSurface>
              == cs::TierKind::Semiring,
    "FIXY-V-097: variable-template form of tier_of_axis must agree.");

// ── Name surface — non-sentinel, non-empty name ─────────────────────
static_assert(cs::dimension_axis_name(cs::DimensionAxis::SyscallSurface)
              == std::string_view{"SyscallSurface"},
    "FIXY-V-097: dimension_axis_name must return \"SyscallSurface\" for "
    "the new axis; a sentinel leak indicates a missing switch arm.");

// ── Catalog cardinality — V-097 grew the dim count 23 → 24 ──────────
//
// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// (`== 24`) lives in safety/DimensionTraits.h colocated with the
// source-of-truth enum; THIS TU only holds the FLOOR pin (`>= 24`)
// which catches the inverse direction — an accidental REMOVAL of a
// DimensionAxis enumerator post-SyscallSurface.
static_assert(cs::DIMENSION_AXIS_COUNT >= 24,
    "FIXY-V-097 floor: DimensionAxis cardinality regressed below 24 — "
    "a post-SyscallSurface enumerator was removed without updating "
    "both DimensionTraits.h's colocated ceiling pin AND this floor "
    "witness.");

// ── Tier preservation — adding SyscallSurface did not perturb other ─
//
// Bug-class catch: an accidental `case DimensionAxis::SyscallSurface:
// return TierKind::Lattice;` would silently re-classify, and a
// cardinality-only check would still pass.  Per-axis re-witness rules
// this out.
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type)
              == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Refinement)
              == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol)
              == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation)
              == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version)
              == cs::TierKind::Versioned);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization)
              == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Regime)
              == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::FpMode)
              == cs::TierKind::Semiring);

// ── SyscallFamilyLattice.h — 9-element family chain ─────────────────
//
// V-097 ships the enum + ChainLatticeOps + At<T> singletons +
// cardinality + name-coverage + lattice-axiom self-tests.  V-098+
// extend with per-family grants and bridges into effect rows.
static_assert(cal::detail::syscall_family_lattice_self_test::family_count
              == 9,
    "FIXY-V-097: SyscallFamily must have exactly 9 enumerators — "
    "NoSyscall, VdsoOnly, ReadOnlyState, FileMutation, MemoryMapping, "
    "ThreadSync, NetworkIo, ProcessControl, Privilege.  Adding a new "
    "family requires bumping family_count + appending an At<T> "
    "specialization at the END of the lattice.");

// ── Underlying type is uint8_t ──────────────────────────────────────
static_assert(std::is_same_v<std::underlying_type_t<cal::SyscallFamily>,
                             std::uint8_t>,
    "FIXY-V-097: SyscallFamily must use uint8_t underlying type — "
    "ChainLatticeOps<E>::leq derives via std::to_underlying, and "
    "bit-width pinning lets V-100's Bridge.h derive effect-row "
    "indices without zero-extending.");

// ── Bottom-element ordinal convention ───────────────────────────────
//
// SyscallFamily uses subset-inclusion order: NoSyscall has the
// smallest syscall surface (empty set), Privilege has the largest
// (capability-bearing operations include all weaker behaviors).
// bottom() = NoSyscall = 0; top() = Privilege = 8.
static_assert(std::to_underlying(cal::SyscallFamily::NoSyscall)       == 0,
    "FIXY-V-097: SyscallFamily::NoSyscall must be ordinal 0 (bottom). "
    "ChainLatticeOps derives bottom() mechanically from the lowest "
    "enumerator; reordering would break every binding that defaults "
    "to NoSyscall through strict_default_for<SyscallSurface>.");
static_assert(std::to_underlying(cal::SyscallFamily::VdsoOnly)        == 1);
static_assert(std::to_underlying(cal::SyscallFamily::ReadOnlyState)   == 2);
static_assert(std::to_underlying(cal::SyscallFamily::FileMutation)    == 3);
static_assert(std::to_underlying(cal::SyscallFamily::MemoryMapping)   == 4);
static_assert(std::to_underlying(cal::SyscallFamily::ThreadSync)      == 5);
static_assert(std::to_underlying(cal::SyscallFamily::NetworkIo)       == 6);
static_assert(std::to_underlying(cal::SyscallFamily::ProcessControl)  == 7);
static_assert(std::to_underlying(cal::SyscallFamily::Privilege)       == 8,
    "FIXY-V-097: SyscallFamily::Privilege must be ordinal 8 (top). "
    "ChainLatticeOps derives top() from the highest enumerator; "
    "Privilege subsumes every weaker family (capability-bearing "
    "syscalls also do file mutations, sync, IO, etc.).");

// ── Lattice axioms — bottom, top, ordering ──────────────────────────
//
// SyscallFamilyLattice is a finite chain (a fortiori a distributive
// lattice).  Pin the three identity axioms; the exhaustive
// lattice-axiom self-test inside SyscallFamilyLattice.h's runtime
// smoke test verifies meet/join/leq for every pair.
static_assert(cal::SyscallFamilyLattice::bottom()
              == cal::SyscallFamily::NoSyscall);
static_assert(cal::SyscallFamilyLattice::top()
              == cal::SyscallFamily::Privilege);
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::NoSyscall,
                                              cal::SyscallFamily::Privilege),
    "FIXY-V-097: bottom must be leq top — the chain ordering is "
    "structurally violated if this fails.");

// ── Per-element At<T> singleton pattern witness ─────────────────────
//
// The At<T> alias pattern lets per-binding sites pin a SyscallFamily
// at the type level (e.g. `Graded<Absolute, SyscallFamilyLattice::At<
// SyscallFamily::VdsoOnly>, T>` for a binding that admits vDSO calls
// only).  V-097 pins the contract that every At<T>::element_type is
// an EMPTY struct (zero data members) — Graded<..., At<T>, P> then
// EBO-collapses to sizeof(P) downstream.  Witness for bottom, top,
// and a mid-chain element.
static_assert(std::is_empty_v<
    cal::SyscallFamilyLattice::At<cal::SyscallFamily::NoSyscall>::element_type>,
    "FIXY-V-097: At<NoSyscall>::element_type must be an empty struct "
    "so Graded<Absolute, At<NoSyscall>, P> EBO-collapses to sizeof(P) "
    "at every binding site — zero-byte syscall-surface annotation.");
static_assert(std::is_empty_v<
    cal::SyscallFamilyLattice::At<cal::SyscallFamily::FileMutation>::element_type>);
static_assert(std::is_empty_v<
    cal::SyscallFamilyLattice::At<cal::SyscallFamily::Privilege>::element_type>);

// Singleton `tier` constant pins the enum value at the type level —
// this is what V-098+ grants will key on for compile-time admission
// decisions.
static_assert(cal::SyscallFamilyLattice::At<cal::SyscallFamily::VdsoOnly>::tier
              == cal::SyscallFamily::VdsoOnly,
    "FIXY-V-097: At<T>::tier must equal T at the type level so "
    "downstream wrappers can read the family without runtime data.");

// ── Chain monotonicity spot-checks ──────────────────────────────────
//
// Verify the canonical "vDSO < file mutation < network IO < privilege"
// ordering matches subset-inclusion intuition.  These pins protect
// against accidental enumerator reordering during merges.
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::VdsoOnly,
                                              cal::SyscallFamily::FileMutation));
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::FileMutation,
                                              cal::SyscallFamily::NetworkIo));
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::NetworkIo,
                                              cal::SyscallFamily::Privilege));

// Inverse direction: NetworkIo is NOT leq FileMutation (chain is
// strictly ordered, not a partial order with incomparable peers).
static_assert(!cal::SyscallFamilyLattice::leq(cal::SyscallFamily::NetworkIo,
                                               cal::SyscallFamily::FileMutation));

// ── Join semantics — par=join (strictest-wins) ──────────────────────
//
// Two composed sites take the LUB of their syscall surfaces: a binding
// that does vDSO + a binding that does file mutation composes to
// FileMutation (the wider surface dominates).  This pins the Tier-S
// par=join discipline for V-100's effect-row bridge.
static_assert(cal::SyscallFamilyLattice::join(cal::SyscallFamily::VdsoOnly,
                                               cal::SyscallFamily::FileMutation)
              == cal::SyscallFamily::FileMutation,
    "FIXY-V-097: par=join (strictest-wins) — composing weaker + "
    "stronger family at a site yields the stronger.  V-100's "
    "effect-row bridge depends on this so a binding's declared "
    "SyscallSurface is the LUB of its constituents.");

// ── Meet semantics — and=meet (most-permissive-floor) ───────────────
//
// At an admission gate (e.g., hot-path Hardening.h check), the
// admitted-surface meet asks "what's the strongest both sides will
// admit" — and=meet returns the floor.
static_assert(cal::SyscallFamilyLattice::meet(cal::SyscallFamily::Privilege,
                                               cal::SyscallFamily::VdsoOnly)
              == cal::SyscallFamily::VdsoOnly,
    "FIXY-V-097: and=meet (most-permissive-floor) — meeting a tight "
    "admission policy with a loose binding yields the tight floor.");

// ── Runtime smoke test — exhaustive lattice-axiom verification ──────
//
// V-097's runtime smoke fires the exhaustive O(n²) lattice-axiom
// check (reflexivity, antisymmetry, transitivity, join/meet idempo-
// tence, absorption) across every pair of SyscallFamily values.  The
// static_assert chain above pins the inductive base; runtime closes
// the loop for non-constexpr operands.
//
// (Header's `inline void syscall_family_lattice_runtime_smoke_test()`
// is invoked from main(); a regression there fails the TU at link
// time on assert violation.)

}  // namespace

int main() {
    cal::detail::syscall_family_lattice_self_test
        ::syscall_family_lattice_runtime_smoke_test();
    return 0;
}
