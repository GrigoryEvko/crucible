#pragma once

// ── crucible::algebra::lattices::DetSafeLattice ─────────────────────
//
// Seven-tier total-order chain lattice over the determinism-safety
// spectrum — the grading axis underlying the 8th safety axiom (DetSafe
// per CLAUDE.md §II.8) and the load-bearing Cipher write-fence from
// 28_04_2026_effects.md §3.4.
//
// ── The classification ──────────────────────────────────────────────
//
// Each tier names a CLASS of non-deterministic source.  A value is
// at tier T iff its bytes were produced using ONLY tier-T sources
// (or any source LOWER in the impurity chain, which is structurally
// less impure).
//
//     Pure                    — no observable side channel; the bytes
//                                are a pure function of declared
//                                inputs (computation graph, weights,
//                                seeded RNG counters).  The strongest
//                                replay-safety promise.
//     PhiloxRng               — bytes derive from Philox4x32 counter-
//                                based PRNG seeded by Crucible's
//                                master_counter.  Replay-safe across
//                                hardware: same (counter, key) → same
//                                bits (Salmon-Moraes-Dror-Shaw 2011).
//                                Distinguished from Pure because the
//                                RNG STATE is observable but
//                                deterministically reproducible.
//     MonotonicClockRead      — bytes derive from `std::chrono::
//                                steady_clock::now()` or equivalent
//                                monotonic clock.  Replay-unsafe
//                                across runs (clock advances) but
//                                bounded within a run.
//     WallClockRead           — bytes derive from
//                                `std::chrono::system_clock::now()`
//                                (wall-clock time).  Replay-unsafe
//                                across runs AND non-monotonic (NTP
//                                slew can step backwards).
//     EntropyRead             — bytes derive from `/dev/urandom`,
//                                `getrandom(2)`, or hardware RNG.
//                                Cryptographically random; trivially
//                                replay-unsafe.
//     FilesystemMtime         — bytes derive from a filesystem
//                                modification timestamp.  Externally
//                                mutable; trivially replay-unsafe.
//     NonDeterministicSyscall — bytes derive from any syscall whose
//                                output is not pinnable by Crucible
//                                (network reads, /proc inspection,
//                                signal-driven readers, etc.).  The
//                                bottom of the chain — least replay-
//                                safe; satisfies only NDS-tolerating
//                                consumers.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class DetSafeTier ∈ the seven tiers above.
// Order:   NonDeterministicSyscall ⊑ FilesystemMtime ⊑ EntropyRead
//                ⊑ WallClockRead ⊑ MonotonicClockRead ⊑ PhiloxRng
//                ⊑ Pure.
//
// Bottom = NonDeterministicSyscall (weakest replay-safety; satisfies
//                                   only NDS-tolerating consumers).
// Top    = Pure                    (strongest replay-safety;
//                                   satisfies every consumer).
// Join   = max                     (the more-pure of two providers —
//                                   what BOTH consumers will accept).
// Meet   = min                     (the less-pure of two providers —
//                                   what EITHER consumer will accept).
//
// ── Direction convention (matches Tolerance / Consistency / Lifetime) ─
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-determinism consumer is satisfied by a stronger-
// determinism provider" — a value computed under Pure subsumes any
// consumer asking for at most PhiloxRng / MonotonicClockRead / etc.,
// because Pure is the strongest determinism promise possible.
//
// This is the Crucible-standard subsumption-up direction, shared
// with ToleranceLattice (BITEXACT = top), ConsistencyLattice (STRONG
// = top), LifetimeLattice (PER_FLEET = top).
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.1 SPEC ───────────────
//
// The spec's enum ordinals (Pure=0 ... NDS=6) put Pure at the BOTTOM
// of the chain.  This implementation INVERTS that ordering (NDS=0 ...
// Pure=6) to keep the lattice's chain direction uniform with
// Tolerance/Consistency/Lifetime — which all put the strongest
// constraint at the TOP.  The SEMANTIC contract from the spec
// ("Pure satisfies any consumer") is preserved exactly:
//
//   DetSafe<Pure>::satisfies<R>  = leq(R, Pure)   = true for all R ✓
//   DetSafe<NDS>::satisfies<Pure> = leq(Pure, NDS) = false        ✓
//
// The only effect of the inversion is that `DetSafeTier::Pure ==
// uint8_t{6}` rather than `uint8_t{0}` — purely an enum-encoding
// choice with zero impact on type-level semantics.  Production
// callers that pattern-match on the underlying integer (rare) need
// to be aware; callers that use the enum names (the vast majority)
// see no difference.
//
//   Axiom coverage:
//     TypeSafe — DetSafeTier is a strong scoped enum (`enum class :
//                uint8_t`); cross-tier mixing requires
//                `std::to_underlying` and is surfaced at the call
//                site instead of silently typed.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     seven-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tier via
//     `DetSafeLattice::At<DetSafeTier::Pure>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors ConfLattice::At<Conf>, ToleranceLattice::At<Tolerance>,
// ConsistencyLattice::At<Consistency>: a per-DetSafeTier singleton
// sub-lattice with empty element_type, used when an op's DetSafe
// tier is fixed at the type level (typical for kernel templates,
// Cipher-fence call sites, Philox return types).
// `Graded<Absolute, DetSafeLattice::At<DetSafeTier::Pure>, T>` pays
// zero runtime overhead for the grade itself.
//
// See ALGEBRA-17 (this file, FOUND-G13) for the lattice itself;
// FOUND-G14 (safety/DetSafe.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §3.4 + §4.3.1 for the production-call-site
// rationale and the Cipher write-fence design; CRUCIBLE.md §II.8
// for the underlying axiom.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── DetSafeTier — chain over the determinism-safety spectrum ────────
//
// Ordinal convention: NDS=0 (bottom) ... Pure=6 (top), matching the
// Tolerance/Consistency/Lifetime project convention (bottom=0).
// This INVERTS the 28_04 §4.3.1 spec's ordinal hint; semantic
// contract (Pure satisfies any consumer) is preserved.  See lattice
// docblock above for the divergence rationale.
enum class DetSafeTier : std::uint8_t {
    NonDeterministicSyscall = 0,    // bottom: any non-pinnable syscall
    FilesystemMtime         = 1,    // externally mutable timestamp
    EntropyRead             = 2,    // /dev/urandom, getrandom(2), hardware RNG
    WallClockRead           = 3,    // system_clock::now() — non-monotonic
    MonotonicClockRead      = 4,    // steady_clock::now() — bounded within run
    PhiloxRng               = 5,    // counter-based PRNG, replay-deterministic
    Pure                    = 6,    // top: pure function of declared inputs
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t det_safe_tier_count =
    std::meta::enumerators_of(^^DetSafeTier).size();

[[nodiscard]] consteval std::string_view det_safe_tier_name(DetSafeTier t) noexcept {
    switch (t) {
        case DetSafeTier::NonDeterministicSyscall: return "NonDeterministicSyscall";
        case DetSafeTier::FilesystemMtime:         return "FilesystemMtime";
        case DetSafeTier::EntropyRead:             return "EntropyRead";
        case DetSafeTier::WallClockRead:           return "WallClockRead";
        case DetSafeTier::MonotonicClockRead:      return "MonotonicClockRead";
        case DetSafeTier::PhiloxRng:               return "PhiloxRng";
        case DetSafeTier::Pure:                    return "Pure";
        default:                                    return std::string_view{"<unknown DetSafeTier>"};
    }
}

// ── Full DetSafeLattice (chain order) ───────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<DetSafeTier> — see
// ChainLattice.h for the rationale.
struct DetSafeLattice : ChainLatticeOps<DetSafeTier> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return DetSafeTier::NonDeterministicSyscall;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return DetSafeTier::Pure;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "DetSafeLattice";
    }

    // ── At<T>: singleton sub-lattice at a fixed type-level tier ─────
    //
    // Used by per-call-site DetSafe-pinned wrappers:
    //   using PureCounter =
    //       Graded<Absolute, DetSafeLattice::At<Pure>, ...>;
    template <DetSafeTier T>
    struct At {
        struct element_type {
            using det_safe_tier_value_type = DetSafeTier;
            [[nodiscard]] constexpr operator det_safe_tier_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr DetSafeTier tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case DetSafeTier::NonDeterministicSyscall:
                    return "DetSafeLattice::At<NonDeterministicSyscall>";
                case DetSafeTier::FilesystemMtime:
                    return "DetSafeLattice::At<FilesystemMtime>";
                case DetSafeTier::EntropyRead:
                    return "DetSafeLattice::At<EntropyRead>";
                case DetSafeTier::WallClockRead:
                    return "DetSafeLattice::At<WallClockRead>";
                case DetSafeTier::MonotonicClockRead:
                    return "DetSafeLattice::At<MonotonicClockRead>";
                case DetSafeTier::PhiloxRng:
                    return "DetSafeLattice::At<PhiloxRng>";
                case DetSafeTier::Pure:
                    return "DetSafeLattice::At<Pure>";
                default:
                    return "DetSafeLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace det_safe_tier {
    using NdsTier        = DetSafeLattice::At<DetSafeTier::NonDeterministicSyscall>;
    using FsMtimeTier    = DetSafeLattice::At<DetSafeTier::FilesystemMtime>;
    using EntropyTier    = DetSafeLattice::At<DetSafeTier::EntropyRead>;
    using WallClockTier  = DetSafeLattice::At<DetSafeTier::WallClockRead>;
    using MonoClockTier  = DetSafeLattice::At<DetSafeTier::MonotonicClockRead>;
    using PhiloxTier     = DetSafeLattice::At<DetSafeTier::PhiloxRng>;
    using PureTier       = DetSafeLattice::At<DetSafeTier::Pure>;
}  // namespace det_safe_tier

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::det_safe_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(det_safe_tier_count == 7,
    "DetSafeTier catalog diverged from {NDS, FsMtime, Entropy, WallClock, "
    "MonoClock, Philox, Pure}; confirm intent and update the Cipher "
    "write-fence + Philox return type plumbing.");

[[nodiscard]] consteval bool every_det_safe_tier_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DetSafeTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (det_safe_tier_name([:en:]) ==
            std::string_view{"<unknown DetSafeTier>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_det_safe_tier_has_name(),
    "det_safe_tier_name() switch missing arm for at least one tier — "
    "add the arm or the new tier leaks the '<unknown DetSafeTier>' "
    "sentinel into Augur's debug output.");

// Concept conformance — full lattice + each At<T> sub-lattice.
static_assert(Lattice<DetSafeLattice>);
static_assert(BoundedLattice<DetSafeLattice>);
static_assert(Lattice<det_safe_tier::NdsTier>);
static_assert(Lattice<det_safe_tier::PureTier>);
static_assert(BoundedLattice<det_safe_tier::PureTier>);

// Negative concept assertions — pin DetSafeLattice's character.
static_assert(!UnboundedLattice<DetSafeLattice>);
static_assert(!Semiring<DetSafeLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<det_safe_tier::NdsTier::element_type>);
static_assert(std::is_empty_v<det_safe_tier::PureTier::element_type>);
static_assert(std::is_empty_v<det_safe_tier::PhiloxTier::element_type>);
static_assert(std::is_empty_v<det_safe_tier::MonoClockTier::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (DetSafeTier)³ = 343 triples each.  Both verifiers extracted into
// ChainLattice.h — adding a new tier auto-extends coverage.
static_assert(verify_chain_lattice_exhaustive<DetSafeLattice>(),
    "DetSafeLattice's chain-order lattice axioms must hold at every "
    "(DetSafeTier)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<DetSafeLattice>(),
    "DetSafeLattice's chain order must satisfy distributivity at every "
    "(DetSafeTier)³ triple — a chain order always does, so failure "
    "would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing, with Pure
// at the top (strongest replay-safety) and NDS at the bottom.
static_assert( DetSafeLattice::leq(DetSafeTier::NonDeterministicSyscall, DetSafeTier::FilesystemMtime));
static_assert( DetSafeLattice::leq(DetSafeTier::FilesystemMtime,         DetSafeTier::EntropyRead));
static_assert( DetSafeLattice::leq(DetSafeTier::EntropyRead,             DetSafeTier::WallClockRead));
static_assert( DetSafeLattice::leq(DetSafeTier::WallClockRead,           DetSafeTier::MonotonicClockRead));
static_assert( DetSafeLattice::leq(DetSafeTier::MonotonicClockRead,      DetSafeTier::PhiloxRng));
static_assert( DetSafeLattice::leq(DetSafeTier::PhiloxRng,               DetSafeTier::Pure));
static_assert( DetSafeLattice::leq(DetSafeTier::NonDeterministicSyscall, DetSafeTier::Pure));   // transitive endpoints
static_assert(!DetSafeLattice::leq(DetSafeTier::Pure,                    DetSafeTier::NonDeterministicSyscall));
static_assert(!DetSafeLattice::leq(DetSafeTier::PhiloxRng,               DetSafeTier::MonotonicClockRead));

// Pin bottom / top to the chain endpoints.
static_assert(DetSafeLattice::bottom() == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::top()    == DetSafeTier::Pure);

// Join strengthens (max); meet weakens (min).
static_assert(DetSafeLattice::join(DetSafeTier::NonDeterministicSyscall, DetSafeTier::Pure)
              == DetSafeTier::Pure);
static_assert(DetSafeLattice::join(DetSafeTier::PhiloxRng, DetSafeTier::MonotonicClockRead)
              == DetSafeTier::PhiloxRng);
static_assert(DetSafeLattice::meet(DetSafeTier::NonDeterministicSyscall, DetSafeTier::Pure)
              == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::meet(DetSafeTier::PhiloxRng, DetSafeTier::MonotonicClockRead)
              == DetSafeTier::MonotonicClockRead);

// Diagnostic names.
static_assert(DetSafeLattice::name() == "DetSafeLattice");
static_assert(det_safe_tier::NdsTier::name()        == "DetSafeLattice::At<NonDeterministicSyscall>");
static_assert(det_safe_tier::FsMtimeTier::name()    == "DetSafeLattice::At<FilesystemMtime>");
static_assert(det_safe_tier::EntropyTier::name()    == "DetSafeLattice::At<EntropyRead>");
static_assert(det_safe_tier::WallClockTier::name()  == "DetSafeLattice::At<WallClockRead>");
static_assert(det_safe_tier::MonoClockTier::name()  == "DetSafeLattice::At<MonotonicClockRead>");
static_assert(det_safe_tier::PhiloxTier::name()     == "DetSafeLattice::At<PhiloxRng>");
static_assert(det_safe_tier::PureTier::name()       == "DetSafeLattice::At<Pure>");

// Reflection-driven coverage check on At<T>::name().
[[nodiscard]] consteval bool every_at_det_safe_tier_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DetSafeTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (DetSafeLattice::At<([:en:])>::name() ==
            std::string_view{"DetSafeLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_det_safe_tier_has_name(),
    "DetSafeLattice::At<T>::name() switch missing an arm for at "
    "least one tier — add the arm or the new tier leaks the "
    "'DetSafeLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(det_safe_tier::NdsTier::tier        == DetSafeTier::NonDeterministicSyscall);
static_assert(det_safe_tier::FsMtimeTier::tier    == DetSafeTier::FilesystemMtime);
static_assert(det_safe_tier::EntropyTier::tier    == DetSafeTier::EntropyRead);
static_assert(det_safe_tier::WallClockTier::tier  == DetSafeTier::WallClockRead);
static_assert(det_safe_tier::MonoClockTier::tier  == DetSafeTier::MonotonicClockRead);
static_assert(det_safe_tier::PhiloxTier::tier     == DetSafeTier::PhiloxRng);
static_assert(det_safe_tier::PureTier::tier       == DetSafeTier::Pure);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// PureTier — the most semantically-loaded tier (Cipher write-fence
// gate, Philox return type for now-pinned values).  Witnessed against
// arithmetic T to pin parity across the trivially-default-constructible
// axis.
template <typename T_>
using PureGraded = Graded<ModalityKind::Absolute, det_safe_tier::PureTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, double);

// PhiloxTier — Philox.h::generate return type tier.
template <typename T_>
using PhiloxGraded = Graded<ModalityKind::Absolute, det_safe_tier::PhiloxTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PhiloxGraded, EightByteValue);

// NdsTier — escape-hatch tier for genuinely impure sources.
template <typename T_>
using NdsGraded = Graded<ModalityKind::Absolute, det_safe_tier::NdsTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NdsGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.
inline void runtime_smoke_test() {
    // Full DetSafeLattice ops at runtime.
    DetSafeTier a = DetSafeTier::NonDeterministicSyscall;
    DetSafeTier b = DetSafeTier::Pure;
    [[maybe_unused]] bool        l1   = DetSafeLattice::leq(a, b);
    [[maybe_unused]] DetSafeTier j1   = DetSafeLattice::join(a, b);
    [[maybe_unused]] DetSafeTier m1   = DetSafeLattice::meet(a, b);
    [[maybe_unused]] DetSafeTier bot  = DetSafeLattice::bottom();
    [[maybe_unused]] DetSafeTier top  = DetSafeLattice::top();

    // Mid-tier ops — chain through the clock-read portion.
    DetSafeTier mono   = DetSafeTier::MonotonicClockRead;
    DetSafeTier philox = DetSafeTier::PhiloxRng;
    [[maybe_unused]] DetSafeTier j2 = DetSafeLattice::join(mono, philox);    // PhiloxRng (more pure)
    [[maybe_unused]] DetSafeTier m2 = DetSafeLattice::meet(mono, philox);    // MonotonicClockRead

    // Graded<Absolute, PureTier, T> at runtime.
    OneByteValue v{42};
    PureGraded<OneByteValue> initial{v, det_safe_tier::PureTier::bottom()};
    auto widened   = initial.weaken(det_safe_tier::PureTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(det_safe_tier::PureTier::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<DetSafeTier>::element_type → DetSafeTier at runtime.
    det_safe_tier::PureTier::element_type e{};
    [[maybe_unused]] DetSafeTier rec = e;
}

}  // namespace detail::det_safe_lattice_self_test

}  // namespace crucible::algebra::lattices
