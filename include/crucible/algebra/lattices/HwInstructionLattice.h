#pragma once

// ── crucible::algebra::lattices::HwInstructionLattice ───────────────
//
// Total-order CHAIN over hardware-instruction CAPABILITY tiers — the
// grading axis underlying the safety/Hw.h `Hw<Tier, P>` wrapper
// (FIXY-V-254) and the strict-default progression that pins fixy
// stances to the narrowest instruction class they actually need.
//
//   NoneAllowed ⊑ Scalar ⊑ Vectorizable ⊑ NonDeterministicTsc ⊑ PrivilegedMsr
//
// Each tier admits EVERYTHING below it plus its own instruction class —
// capability is cumulative, so the order is a genuine chain (a fortiori
// a distributive lattice, unlike the SimdIsa partial order of V-250).
//
// ── Why a dedicated HwInstruction axis (Tier 0 — BLOCKER for Mimic) ──
//
// The Met(X) effect row records MEMORY effects {Alloc, IO, Block, Bg,
// Init, Test}.  It has NO notion of WHICH hardware-instruction class a
// kernel issues.  Whether a function emits only scalar arithmetic
// (portable to any ISA, deterministic, ring-3), SIMD intrinsics (needs
// an ISA-availability proof), `rdtsc` (non-deterministic — breaks
// BITEXACT), or `rdmsr`/`wrmsr`/`IN`/`OUT` (ring-0, privileged) is
// invisible to the effect row AND to the V-239 ControlFlow axis (which
// classifies non-local CONTROL TRANSFER, not instruction class).  Four
// real gates cannot be expressed without this axis:
//
//   1. **Mimic backend instruction legalization** (the Tier-0 blocker):
//      Mimic must know, per kernel, whether SIMD / rdtsc / MSR
//      instructions are emitted before it can pick a legal ISA target
//      AND privilege level.  A kernel pinned ⊑ Scalar is portable to
//      every backend with no ISA proof; a `Vectorizable` kernel composes
//      with the V-256 SimdWidthPinned ISA proof; an MSR kernel can only
//      land on a ring-0 execution context.  Folding onto the effect row
//      collapses these into one bit and blocks Mimic codegen.
//
//   2. **DetSafe coupling at NonDeterministicTsc**: `rdtsc`/`rdtscp`
//      read a counter that differs per run and per core — a NON-
//      deterministic entropy read.  A function at this tier cannot be
//      BITEXACT; it must carry `grant::dropdetsafe<EntropyRead>` (V-243
//      lineage).  Only a dedicated axis lets the DetSafe downgrade
//      attach to exactly the kernels that read the TSC.
//
//   3. **Permission coupling at PrivilegedMsr**: `rdmsr`/`wrmsr`/`IN`/
//      `OUT` are ring-0 instructions.  A function at this tier requires
//      `Permission<warden::tag::Root>` (the H003 collision rule shipped
//      by V-260).  warden::Hardening admits this tier only inside an
//      Init-context.
//
//   4. **Strict-default stance progression**: `stance::PureLinear` pins
//      `NoneAllowed` (the strict default — a pure linear computation
//      issues no hardware-specific instruction at all).  Production
//      kernels admit `Vectorizable`.  The bench harness admits
//      `NonDeterministicTsc` (paired with a CpuPinProof so the TSC read
//      is at least core-stable).  warden::Hardening admits
//      `PrivilegedMsr` only inside Init-ctx.  The chain order IS this
//      progression.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// HwInstruction is `TierKind::Semiring` (the AXIS tier in
// DimensionTraits.h, shipped by V-253 — NOT a Semiring concept on the
// lattice itself; the chain order has no independent ⊕/⊗).  The
// composition reading is "instruction-capability union": two call sites
// composing in parallel OR in sequence admit the JOIN (the wider /
// higher-privilege instruction class) of their declared tiers — a
// region that contains both a scalar site and an rdtsc site is itself
// `NonDeterministicTsc`.  This parallels the V-239 ControlFlow par=join
// reading at the granularity of HARDWARE INSTRUCTION CLASS.
//
// ── Chain order — subset-inclusion of admitted instruction classes ──
//
// Ordinal 0 = NoneAllowed (smallest set — no hardware instructions; the
// hot-path-safest, most-portable claim).  Ordinal 4 = PrivilegedMsr
// (largest set — every instruction below PLUS ring-0 MSR/port I/O).
// A function declaring `HwInstruction = X` ASSERTS its actual
// instruction-class set ⊆ X's admitted set.  Hot-path / Mimic-portable
// admission `hw_instruction ⊑ Scalar` therefore requires the function
// to claim at most Scalar.
//
//   NoneAllowed         = 0 — bottom; no hw instructions; strict default
//                             for stance::PureLinear; portable everywhere.
//   Scalar              = 1 — scalar arithmetic + control flow only, NO
//                             SIMD.  Portable to every ISA with no width
//                             proof; deterministic; ring-3.
//   Vectorizable        = 2 — SIMD intrinsics admitted.  Composes with the
//                             V-256 SimdWidthPinned wrapper, which carries
//                             the ISA-availability proof (an AVX2 binary
//                             must not run on an SSE2 host).
//   NonDeterministicTsc = 3 — rdtsc / rdtscp permitted.  A non-
//                             deterministic counter read; couples to a
//                             DetSafe downgrade (grant::dropdetsafe<
//                             EntropyRead>).  Strictly above Vectorizable
//                             because SIMD is deterministic but a TSC read
//                             is not — it adds a NEW hazard (non-
//                             reproducibility) on top of everything below.
//   PrivilegedMsr       = 4 — top; rdmsr / wrmsr / IN / OUT ring-0
//                             instructions.  Requires Permission<warden::
//                             tag::Root> (H003).  Strictly above
//                             NonDeterministicTsc because ring-0 access is
//                             a privilege escalation — the most disruptive
//                             instruction class, admitted only by
//                             warden::Hardening inside an Init-context.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — `HwInstruction` is a strong scoped enum (`enum class
//                : uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site
//                (see test/safety_neg/neg_hw_instruction_*).
//   InitSafe — every enumerator has an explicit ordinal; reflection-
//                driven coverage fires automatically if a switch arm is
//                forgotten.
//   DetSafe  — lattice operations are `constexpr` (not `consteval`) so a
//                runtime Graded carrier (V-254) can enforce its
//                `pre (L::leq(...))` precondition under enforce.
//   LeakSafe — zero-state enum; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  The enum compiles to one uint8_t per value; the `At<T>`
// singleton's element_type is empty and EBO-collapses to 0 bytes at
// every use site (V-254 wrapper, V-257 grants) — proven below via
// CRUCIBLE_GRADED_LAYOUT_INVARIANT.
//
// ── Forward references (deferred deliverables, by construction) ─────
//
//   FIXY-V-253 — DimensionAxis::HwInstruction enumerator + tier_of_axis.
//   FIXY-V-254 — safety/Hw.h: the `Hw<Tier, P>` = Graded<Absolute,
//                HwInstructionLattice::At<Tier>, P> regime-1 wrapper.
//                The `row_hash_contribution<safety::Hw<Tier, Inner>>`
//                federation-cache discriminator ships THERE, in
//                safety/diag/RowHashFold.h, exactly like every other
//                wrapper (Vendor / HotPath / FpModePinned / ...).  A
//                lattice header pulls NO row_hash machinery — the
//                row_hash key is the WRAPPER, never the lattice At<>;
//                and `safety::Hw` does not exist until V-254, so the
//                specialization is deferred by construction.  Mirrors
//                VendorLattice → safety/Vendor.h and SimdIsaLattice
//                (V-250) → safety/SimdWidthPinned.h (V-256).
//   FIXY-V-257 — fixy/Hw.h: grant tags routing onto this axis.
//   FIXY-V-260 — CollisionCatalog H001/H002/H003: the cross-axis rules
//                (e.g. PrivilegedMsr × ¬Permission<Root> reject).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── HwInstruction — hardware-instruction capability taxonomy ────────
//
// Chain ordering: each tier strictly subsumes the admitted instruction
// class of every tier below it (capability-superset total order).
// Ordinal 0 = smallest set (NoneAllowed, no hw instructions); ordinal 4
// = largest set (PrivilegedMsr, ring-0 MSR/port I/O).
enum class HwInstruction : std::uint8_t {
    NoneAllowed         = 0,  // bottom — no hw instructions; stance::PureLinear default
    Scalar              = 1,  // scalar arithmetic + control flow; no SIMD
    Vectorizable        = 2,  // SIMD intrinsics (composes with V-256 SimdWidthPinned)
    NonDeterministicTsc = 3,  // rdtsc/rdtscp; couples to DetSafe downgrade
    PrivilegedMsr       = 4,  // top — rdmsr/wrmsr/IN/OUT ring-0; needs Permission<Root>
};

[[nodiscard]] consteval std::string_view hw_instruction_name(HwInstruction t) noexcept {
    switch (t) {
        case HwInstruction::NoneAllowed:         return "NoneAllowed";
        case HwInstruction::Scalar:              return "Scalar";
        case HwInstruction::Vectorizable:        return "Vectorizable";
        case HwInstruction::NonDeterministicTsc: return "NonDeterministicTsc";
        case HwInstruction::PrivilegedMsr:       return "PrivilegedMsr";
        default:                                 return std::string_view{"<unknown HwInstruction>"};
    }
}

struct HwInstructionLattice : ChainLatticeOps<HwInstruction> {
    [[nodiscard]] static constexpr HwInstruction bottom() noexcept {
        return HwInstruction::NoneAllowed;
    }
    [[nodiscard]] static constexpr HwInstruction top() noexcept {
        return HwInstruction::PrivilegedMsr;
    }
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "HwInstructionLattice";
    }

    template <HwInstruction T>
    struct At {
        struct element_type {
            using hw_instruction_value_type = HwInstruction;
            [[nodiscard]] constexpr operator hw_instruction_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };
        static constexpr HwInstruction tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case HwInstruction::NoneAllowed:         return "HwInstructionLattice::At<NoneAllowed>";
                case HwInstruction::Scalar:              return "HwInstructionLattice::At<Scalar>";
                case HwInstruction::Vectorizable:        return "HwInstructionLattice::At<Vectorizable>";
                case HwInstruction::NonDeterministicTsc: return "HwInstructionLattice::At<NonDeterministicTsc>";
                case HwInstruction::PrivilegedMsr:       return "HwInstructionLattice::At<PrivilegedMsr>";
                default:                                 return "HwInstructionLattice::At<?>";
            }
        }
    };
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::hw_instruction_lattice_self_test {

// Catalog cardinality — the capability chain has exactly 5 tiers.
inline constexpr std::size_t hw_instruction_count =
    std::meta::enumerators_of(^^HwInstruction).size();

static_assert(hw_instruction_count == 5,
    "HwInstruction diverged from {NoneAllowed, Scalar, Vectorizable, "
    "NonDeterministicTsc, PrivilegedMsr}.  Adding a new capability tier "
    "requires (a) appending at the next free ordinal (append-only per "
    "FOUND-I04), (b) the matching hw_instruction_name() switch arm, (c) "
    "the matching At<T> singleton name() arm, AND (d) the V-254 Hw<> "
    "wrapper's row_hash + V-260 collision rules.  Reusing an existing "
    "ordinal silently changes every stored row_hash without warning.");

// Bottom-element pin — ordinal 0 is the smallest set (NoneAllowed: no hw
// instructions, the stance::PureLinear strict default).
static_assert(std::to_underlying(HwInstruction::NoneAllowed) == 0);

// Top-element pin — ordinal 4 is the largest set (PrivilegedMsr).
static_assert(std::to_underlying(HwInstruction::PrivilegedMsr) == 4);

// Underlying type pin — uint8_t (mirrors ControlFlow / SyscallFamily;
// lets a future effect-row bridge derive indices without zero-extending).
static_assert(std::is_same_v<std::underlying_type_t<HwInstruction>, std::uint8_t>);

// Reflection-driven name coverage — every enumerator must resolve to a
// non-sentinel, non-empty name.  Auto-extends if the enum grows.
[[nodiscard]] consteval bool every_hw_instruction_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^HwInstruction));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto candidate = hw_instruction_name([:en:]);
        if (candidate == std::string_view{"<unknown HwInstruction>"}) return false;
        if (candidate.empty())                                        return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_hw_instruction_has_name(),
    "hw_instruction_name() switch missing an arm for at least one "
    "HwInstruction enumerator.");

// Concept conformance — chain lattice satisfies Lattice + BoundedLattice
// and NOT Semiring (the chain order has no independent ⊕/⊗; the
// "Tier-S Semiring" classification is the AXIS tier in DimensionTraits.h
// describing par=join composition, NOT a Semiring concept here).
static_assert(::crucible::algebra::Lattice<HwInstructionLattice>);
static_assert(::crucible::algebra::BoundedLattice<HwInstructionLattice>);
static_assert(!::crucible::algebra::Semiring<HwInstructionLattice>);

// Exhaustive lattice-axiom verifier on (axis)³ triples.  Chain orders
// are always distributive — failure indicates a leq/join/meet defect.
static_assert(verify_chain_lattice_exhaustive<HwInstructionLattice>(),
    "HwInstructionLattice chain-order lattice axioms failed at some "
    "triple — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<HwInstructionLattice>(),
    "HwInstructionLattice chain failed distributivity — leq/join/meet "
    "defect.");

// Bottom / top pins on the lattice surface (catches enum-reorder drift).
static_assert(HwInstructionLattice::bottom() == HwInstruction::NoneAllowed);
static_assert(HwInstructionLattice::top()    == HwInstruction::PrivilegedMsr);

// Lattice top-level diagnostic name pin.
static_assert(HwInstructionLattice::name() == std::string_view{"HwInstructionLattice"});

// Strict-chain order pin (bottom ⊏ top witness).
static_assert( HwInstructionLattice::leq(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr));
static_assert(!HwInstructionLattice::leq(HwInstruction::PrivilegedMsr, HwInstruction::NoneAllowed));

// Mid-chain ordering — every tier strictly subsumes the previous (the
// strict-default stance progression: Pure→prod→bench→hardening).
static_assert(HwInstructionLattice::leq(HwInstruction::NoneAllowed,         HwInstruction::Scalar));
static_assert(HwInstructionLattice::leq(HwInstruction::Scalar,              HwInstruction::Vectorizable));
static_assert(HwInstructionLattice::leq(HwInstruction::Vectorizable,        HwInstruction::NonDeterministicTsc));
static_assert(HwInstructionLattice::leq(HwInstruction::NonDeterministicTsc, HwInstruction::PrivilegedMsr));
// Transitive endpoints + the hot-path/Mimic-portable admission witness.
static_assert(HwInstructionLattice::leq(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr));
static_assert(HwInstructionLattice::leq(HwInstruction::Scalar,      HwInstruction::NonDeterministicTsc));

// Reverse direction must fail for non-equal pairs.
static_assert(!HwInstructionLattice::leq(HwInstruction::Scalar,              HwInstruction::NoneAllowed));
static_assert(!HwInstructionLattice::leq(HwInstruction::PrivilegedMsr,       HwInstruction::NonDeterministicTsc));
static_assert(!HwInstructionLattice::leq(HwInstruction::NonDeterministicTsc, HwInstruction::Vectorizable));

// Join semantics — par=join (wider-instruction-class-dominates).
// Composing a Vectorizable site with an rdtsc site yields the higher
// tier (the region as a whole reads the TSC).
static_assert(HwInstructionLattice::join(HwInstruction::Vectorizable,
                                         HwInstruction::NonDeterministicTsc)
              == HwInstruction::NonDeterministicTsc);
// NoneAllowed is the join identity (composing with a no-hw site never
// widens the instruction class).
static_assert(HwInstructionLattice::join(HwInstruction::NoneAllowed,
                                         HwInstruction::Scalar)
              == HwInstruction::Scalar);

// Meet semantics — and=meet (tighter-instruction-floor).  At an admission
// gate, meeting a permissive binding with a tight policy yields the floor.
static_assert(HwInstructionLattice::meet(HwInstruction::PrivilegedMsr,
                                         HwInstruction::Scalar)
              == HwInstruction::Scalar);

// At<T> singleton — empty element_type for EBO collapse at every use
// site.  V-254's `Graded<Absolute, At<T>, P>` relies on this.
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::NoneAllowed>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::Scalar>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::Vectorizable>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::NonDeterministicTsc>::element_type>);
static_assert(std::is_empty_v<HwInstructionLattice::At<HwInstruction::PrivilegedMsr>::element_type>);

// At<T>::tier pins the enum value at the type level — what V-254+
// wrappers key on for compile-time admission decisions.
static_assert(HwInstructionLattice::At<HwInstruction::Vectorizable>::tier
              == HwInstruction::Vectorizable);

// At<I>::name() coverage — reflection-driven, mirrors the enum-name probe.
[[nodiscard]] consteval bool every_at_hw_instruction_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^HwInstruction));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (HwInstructionLattice::At<([:en:])>::name() ==
            std::string_view{"HwInstructionLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_hw_instruction_has_name(),
    "HwInstructionLattice::At<I>::name() switch missing an arm.");

// ── Layout invariants — Graded<Absolute, At<T>, P> == sizeof(P) ─────
//
// Extra-rigor proof (beyond the std::is_empty_v witnesses) that the
// regime-1 EBO collapse actually holds for the V-254 wrapper shape.
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using NoneAllowedGraded = Graded<ModalityKind::Absolute,
                                 HwInstructionLattice::At<HwInstruction::NoneAllowed>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneAllowedGraded, double);

template <typename T_>
using PrivilegedMsrGraded = Graded<ModalityKind::Absolute,
                                   HwInstructionLattice::At<HwInstruction::PrivilegedMsr>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PrivilegedMsrGraded, EightByteValue);

// Runtime smoke test — per feedback_algebra_runtime_smoke_test_discipline:
// pure static_asserts can mask consteval/SFINAE/inline-body bugs; runtime
// ops with non-constant arguments catch them.
inline void hw_instruction_lattice_runtime_smoke_test() {
    HwInstruction a = HwInstruction::NoneAllowed;
    HwInstruction b = HwInstruction::PrivilegedMsr;
    [[maybe_unused]] bool          rl = HwInstructionLattice::leq(a, b);
    [[maybe_unused]] HwInstruction rj = HwInstructionLattice::join(a, b);
    [[maybe_unused]] HwInstruction rm = HwInstructionLattice::meet(a, b);
    [[maybe_unused]] HwInstruction bot = HwInstructionLattice::bottom();
    [[maybe_unused]] HwInstruction topv = HwInstructionLattice::top();

    // Mid-chain witnesses — the production stance progression.
    HwInstruction prod  = HwInstruction::Vectorizable;
    HwInstruction bench = HwInstruction::NonDeterministicTsc;
    [[maybe_unused]] HwInstruction rj2 = HwInstructionLattice::join(prod, bench);
    [[maybe_unused]] HwInstruction rm2 = HwInstructionLattice::meet(prod, bench);
    [[maybe_unused]] bool prod_admits_bench = HwInstructionLattice::leq(prod, bench);

    // At<T>::element_type round-trip — verify the singleton's conversion
    // materializes the right tier at runtime, not just at consteval.
    HwInstructionLattice::At<HwInstruction::Vectorizable>::element_type vec_pin{};
    [[maybe_unused]] HwInstruction vec_recovered = vec_pin;

    // Graded carrier round-trip on the regime-1 EBO shape.
    OneByteValue payload{7};
    NoneAllowedGraded<OneByteValue> initial{
        payload, HwInstructionLattice::At<HwInstruction::NoneAllowed>::bottom()};
    auto widened  = initial.weaken(HwInstructionLattice::At<HwInstruction::NoneAllowed>::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto grade   = widened.grade();
    [[maybe_unused]] auto peeked  = composed.peek().c;
}

}  // namespace detail::hw_instruction_lattice_self_test

}  // namespace crucible::algebra::lattices
