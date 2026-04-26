#pragma once

// ── crucible::algebra::lattices::ConfLattice ────────────────────────
//
// Two-element confidentiality lattice (Public ⊑ Secret) — the
// foundation for Secret<T> per 25_04_2026.md §2.3:
//
//     using Secret<T> = Graded<Comonad, ConfLattice::At<Conf::Secret>, T>;
//
// References:
//   Abadi & Plotkin (2008).   "A Calculus for Cryptographic Protocols
//                              with Information-Flow Modalities."
//   Orchard, Liepelt, Eades (2023).  "Graded Modal Types for Integrity
//                              and Confidentiality." arXiv:2309.04324.
//
// ── Algebraic structure ─────────────────────────────────────────────
//
// ConfLattice carries TWO graded views over the same two-element
// carrier Conf ∈ {Public, Secret}:
//
//   1. Lattice (chain order):  Public ⊑ Secret
//      - leq:    pointwise on the chain
//      - join:   max under chain order  (raises classification)
//      - meet:   min under chain order  (lowers classification)
//      - bottom = Public, top = Secret
//
//   2. Comonad-form modality (in Graded<Comonad, ConfLattice, T>):
//      - extract: counit; named declassify in Secret<T>'s alias.
//        Information flows OUT of the wrapper through a grep-able
//        secret_policy::* tag, never silently.
//
// ── At<Conf>: singleton sub-lattice at a fixed classification ───────
//
// ConfLattice::At<Conf::Secret> is a single-element sub-lattice with
// EMPTY element_type — Secret<T> values are always at the Secret
// position (Public-classified values are plain T, not Secret<T>),
// so the runtime grade is implicit at the type level.  Empty
// element_type + [[no_unique_address]] gives sizeof(Secret<T>) ==
// sizeof(T), matching the existing safety::Secret<T> zero-overhead
// guarantee that MIGRATE-3 (#463) preserves.
//
//   Axiom coverage: TypeSafe — Conf is a strong enum; Comonad
//                   modality forces declassification through a named
//                   counit (the secret_policy::* discipline lifts to
//                   Graded::extract).  DetSafe — every operation is
//                   constexpr.
//   Runtime cost:   zero — empty element_type collapses via EBO when
//                   used through At<Conf::Secret>.
//
// See ALGEBRA-3 (Graded.h), ALGEBRA-2 (Lattice.h), MIGRATE-3 (#463)
// for the Secret<T> alias instantiation.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Conf classification level ───────────────────────────────────────
enum class Conf : std::int8_t {
    Public = 0,
    Secret = 1,
};

// Cardinality + diagnostic name via reflection — auto-bumps on
// future classification-level extensions; reflection-based
// name-coverage assertion catches missing switch arms.
inline constexpr std::size_t conf_count =
    std::meta::enumerators_of(^^Conf).size();

[[nodiscard]] consteval std::string_view conf_name(Conf c) noexcept {
    switch (c) {
        case Conf::Public: return "Public";
        case Conf::Secret: return "Secret";
    }
    return std::string_view{"<unknown Conf>"};
}

// ── Full ConfLattice (chain order Public ⊑ Secret) ──────────────────
struct ConfLattice {
    using element_type = Conf;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return Conf::Public;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return Conf::Secret;
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return leq(a, b) ? b : a;  // max — raise classification
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;  // min — lower classification
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "ConfLattice";
    }

    // ── At<C>: singleton sub-lattice at a type-level classification ─
    //
    // Used by Secret<T> per 25_04_2026.md §2.3:
    //   using Secret<T> = Graded<Comonad, ConfLattice::At<Conf::Secret>, T>;
    template <Conf C>
    struct At {
        // Empty tag carrying C at the type level.  Conversion to Conf
        // recovers the classification for diagnostics / serialization.
        struct element_type {
            using conf_value_type = Conf;
            [[nodiscard]] constexpr operator conf_value_type() const noexcept {
                return C;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr Conf classification = C;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (C) {
                case Conf::Public: return "ConfLattice::At<Public>";
                case Conf::Secret: return "ConfLattice::At<Secret>";
            }
            return "ConfLattice::At<?>";
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
//
// Suffixed `Tier` to avoid collision with Conf::Public / Conf::Secret
// enumerators in user code that does `using namespace ...`.
namespace conf {
    using PublicTier = ConfLattice::At<Conf::Public>;
    using SecretTier = ConfLattice::At<Conf::Secret>;
}  // namespace conf

// ── Self-test (compile-time + reflection-driven name coverage) ──────
namespace detail::conf_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(conf_count == 2,
    "Conf catalog diverged from {Public, Secret}; confirm intent.");

[[nodiscard]] consteval bool every_conf_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Conf));
    template for (constexpr auto en : enumerators) {
        if (conf_name([:en:]) == std::string_view{"<unknown Conf>"}) {
            return false;
        }
    }
    return true;
}
static_assert(every_conf_has_name(),
    "conf_name() switch missing arm for at least one Conf — add the "
    "arm or the new classification leaks the '<unknown Conf>' sentinel.");

// Concept conformance.
static_assert(Lattice<ConfLattice>);
static_assert(BoundedLattice<ConfLattice>);
static_assert(Lattice<ConfLattice::At<Conf::Public>>);
static_assert(Lattice<ConfLattice::At<Conf::Secret>>);
static_assert(BoundedLattice<ConfLattice::At<Conf::Secret>>);

// Empty element_type for EBO collapse — load-bearing for Secret<T>'s
// zero-overhead guarantee.
static_assert(std::is_empty_v<ConfLattice::At<Conf::Public>::element_type>);
static_assert(std::is_empty_v<ConfLattice::At<Conf::Secret>::element_type>);

// EXHAUSTIVE lattice axiom coverage over (Conf)³ = 8 triples.
[[nodiscard]] consteval bool exhaustive_lattice_check() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Conf));
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<ConfLattice>(
                        [:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
    return true;
}
static_assert(exhaustive_lattice_check(),
    "ConfLattice's chain-order lattice axioms must hold at EVERY "
    "(Conf)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");

// Identities.
static_assert(ConfLattice::leq(Conf::Public, Conf::Secret),
    "Public ⊑ Secret in the confidentiality chain.");
static_assert(!ConfLattice::leq(Conf::Secret, Conf::Public),
    "Secret ⋢ Public — declassification doesn't go via lattice "
    "weakening, only via the named declassify counit.");
static_assert(ConfLattice::join(Conf::Public, Conf::Secret) == Conf::Secret,
    "Joining mixed classifications raises to the higher one.");
static_assert(ConfLattice::meet(Conf::Public, Conf::Secret) == Conf::Public,
    "Meeting mixed classifications lowers to the lower one.");

// Diagnostic names.
static_assert(ConfLattice::name() == "ConfLattice");
static_assert(ConfLattice::At<Conf::Public>::name() == "ConfLattice::At<Public>");
static_assert(ConfLattice::At<Conf::Secret>::name() == "ConfLattice::At<Secret>");
static_assert(conf_name(Conf::Public) == "Public");
static_assert(conf_name(Conf::Secret) == "Secret");

// Reflection-driven coverage check on At<C>::name().
[[nodiscard]] consteval bool every_at_conf_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Conf));
    template for (constexpr auto en : enumerators) {
        if (ConfLattice::At<([:en:])>::name() ==
            std::string_view{"ConfLattice::At<?>"}) {
            return false;
        }
    }
    return true;
}
static_assert(every_at_conf_has_name(),
    "ConfLattice::At<C>::name() switch missing an arm for at least "
    "one Conf — add the arm or the new classification leaks the "
    "'ConfLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(conf::PublicTier::classification == Conf::Public);
static_assert(conf::SecretTier::classification == Conf::Secret);

// ── Layout invariants on Graded<...,At<C>,T> ────────────────────────
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using SecretGraded = Graded<ModalityKind::Comonad, conf::SecretTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SecretGraded, EightByteValue);

// ── Runtime smoke test ──────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Full ConfLattice at runtime.
    Conf a = Conf::Public;
    Conf b = Conf::Secret;
    [[maybe_unused]] bool l1 = ConfLattice::leq(a, b);
    [[maybe_unused]] Conf j1 = ConfLattice::join(a, b);
    [[maybe_unused]] Conf m1 = ConfLattice::meet(a, b);
    [[maybe_unused]] Conf bot = ConfLattice::bottom();
    [[maybe_unused]] Conf top = ConfLattice::top();

    // Graded<Comonad, At<Secret>, T> at runtime.
    OneByteValue v{42};
    SecretGraded<OneByteValue> initial{v, conf::SecretTier::bottom()};
    auto widened   = initial.weaken(conf::SecretTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(conf::SecretTier::top());

    // Comonad counit (extract) — only available because modality is Comonad.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    // Conversion: At<Conf::Secret>::element_type → Conf at runtime.
    conf::SecretTier::element_type e{};
    [[maybe_unused]] Conf c = e;
}

}  // namespace detail::conf_lattice_self_test

}  // namespace crucible::algebra::lattices
