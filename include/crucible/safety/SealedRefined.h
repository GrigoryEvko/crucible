#pragma once

// ── crucible::safety::SealedRefined<Pred, T> ───────────────────────
//
// `Refined<Pred, T>` minus the destructive `into()` rvalue extraction.
// Forces every post-construction transformation through a fresh
// re-construction (which re-runs the predicate), eliminating the
// "extract → mutate behind the predicate's back → silently re-wrap"
// footgun.
//
//   Axiom coverage: TypeSafe (predicate-checked construction),
//                   InitSafe (no escape hatch leaves an unchecked
//                   value flowing through internal APIs).
//   Runtime cost:   zero — sizeof(SealedRefined<P, T>) ==
//                   sizeof(Refined<P, T>) == sizeof(T) when the
//                   underlying Graded<Absolute, BoolLattice<P>, T>
//                   collapses via EBO.
//
// ── When to use SealedRefined<P, T> over Refined<P, T> ─────────────
//
// `Refined<P, T>` is the right primitive when callers occasionally
// need to extract the underlying T (e.g., to consume it into an
// API that takes a bare T, then immediately discard the wrapper).
// The `into() &&` rvalue method makes the extraction explicit and
// auditable — the act of consuming is grep-discoverable.
//
// `SealedRefined<P, T>` is the right primitive when:
//
//   - The predicate captures an INVARIANT that downstream code
//     depends on continuously (not just at the moment of
//     construction).  Extracting the value to mutate it is a
//     LOAD-BEARING discipline violation, not just a discouraged
//     pattern.
//
//   - Future maintainers will be tempted to write
//     `auto x = sealed.into(); mutate(x); auto re = SealedRefined{x};`
//     — but this loses the predicate continuity if `mutate` violates
//     P transiently between the two re-checks.  SealedRefined deletes
//     `into()` so the only path is to construct a NEW SealedRefined
//     directly from a (mutated) input that satisfies P.
//
//   - The wrapped T has its OWN mutation surface (vector::push_back,
//     string::operator+=, etc.).  Refined<P, vector<int>> can be
//     consumed into the underlying vector and grown past whatever
//     bound P enforces; SealedRefined<P, vector<int>> can only be
//     read.
//
// ── Why not just `const Refined<P, T>` ─────────────────────────────
//
// `const Refined<P, T>` is almost the right discipline, but suffers
// two problems:
//
//   1. const-correctness leaks: `Refined<P, T>` is movable, so a
//      `const Refined<P, T>&` parameter accepts a moved-from rvalue
//      that the caller can re-assign behind the API's back.  The
//      const qualifier on the parameter doesn't propagate to the
//      caller's value.
//
//   2. into() is `&&`-qualified: even with `const Refined<P, T>`,
//      a `std::move(refined).into()` call by an unrelated caller
//      pulls the value out.  const-as-discipline is reviewer-only,
//      not type-system-enforced.
//
// SealedRefined deletes `into()` AT THE TYPE LEVEL: any caller
// attempting `std::move(sealed).into()` produces a compile error,
// regardless of const-qualification or move-from chains.
//
// ── MIGRATE-11 (replaces dropped #248) ─────────────────────────────
//
// 25_04_2026.md §2 graded foundation refactor enumerated this as a
// new wrapper to ship alongside the migrated Linear/Refined/Tagged/
// Secret/Monotonic/AppendOnly/SharedPermission family.  The original
// Lean task #248 was dropped; this wrapper realizes the C++-side
// implementation independently.
//
// Sits over the same Graded<Absolute, BoolLattice<P>, T> substrate
// as Refined<P, T>.  Same lattice, same modality, same storage —
// only the API surface differs (no into() rvalue extractor, no
// destructive consume path).  The graded_type alias is identical
// to Refined's, so the GradedWrapper concept (#499) accepts both
// uniformly.
//
// See safety/Refined.h for the underlying refinement discipline,
// algebra/Graded.h for the substrate, algebra/lattices/BoolLattice.h
// for the predicate-lattice machinery.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/BoolLattice.h>
#include <crucible/safety/Refined.h>  // for predicate concepts

#include <compare>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <auto Pred, typename T>
class [[nodiscard]] SealedRefined {
    // Same Graded substrate as Refined<Pred, T> — identical lattice,
    // modality, storage layout.  The wrapper adds no state; the
    // "sealed" property is enforced by the absence of a destructive
    // extractor in the public surface.
    using lattice_type = ::crucible::algebra::lattices::BoolLattice<
        std::remove_cv_t<decltype(Pred)>>;

public:
    using value_type     = T;
    using predicate_type = decltype(Pred);
    using graded_type    = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

public:
    // Trusted-construction tag.  Mirrors Refined::Trusted — the
    // caller has already proven the invariant (re-wrapping internal
    // already-validated data, deserializing from a content-hashed
    // source whose hash has been verified).
    struct Trusted {};

    // Checked construction — fires the predicate's contract.  The
    // PredicateInvocableOn concept upgrades a Pred(T) invocability
    // mismatch from a contract-clause SFINAE wall into a clean
    // concept-violation diagnostic at the call site.
    constexpr explicit SealedRefined(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
        pre(Pred(v))
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // Trusted construction — no predicate check.
    constexpr SealedRefined(T v, Trusted)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // Conversion from Refined<Pred, T>.  Trusted because Refined's
    // own invariant proves Pred(value).  Consumes the Refined.
    constexpr explicit SealedRefined(Refined<Pred, T>&& r)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(r).into(), typename lattice_type::element_type{}} {}

    // Copy/move are defaulted — moving a SealedRefined doesn't
    // violate sealing because the moved-to value carries the same
    // (predicate-satisfying) bytes.  The discipline is "no destructive
    // EXTRACTION", not "no MOVEMENT".
    SealedRefined(const SealedRefined&)            = default;
    SealedRefined(SealedRefined&&)                 = default;
    SealedRefined& operator=(const SealedRefined&) = default;
    SealedRefined& operator=(SealedRefined&&)      = default;

    // Read-only access — forwards through Graded::peek().  This is
    // the ONLY way to observe the underlying T.
    [[nodiscard]] constexpr const T& value() const noexcept {
        return impl_.peek();
    }

    // No `into() &&` — the load-bearing difference from Refined.
    // No `value_mut()` — no mutable accessor.  Any change to the
    // wrapped value requires constructing a fresh SealedRefined,
    // which re-fires the predicate.

    // Equality / ordering on the underlying value.
    friend constexpr bool operator==(const SealedRefined& a, const SealedRefined& b)
        noexcept(noexcept(a.impl_.peek() == b.impl_.peek()))
    {
        return a.impl_.peek() == b.impl_.peek();
    }

    friend constexpr auto operator<=>(const SealedRefined& a, const SealedRefined& b)
        noexcept(noexcept(a.impl_.peek() <=> b.impl_.peek()))
        requires std::three_way_comparable<T>
    {
        return a.impl_.peek() <=> b.impl_.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13).
    //
    // lattice_name(): "BoolLattice<Pred>" — same as Refined<Pred, T>
    // (same substrate, different API surface).  External code can
    // distinguish SealedRefined from Refined by the wrapper class
    // identity, not by the lattice name.
    //
    // Audit-Tier-2 cross-wrapper parity sweep — every migrated
    // wrapper ships these two consteval forwarders.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

// Zero-cost guarantee — SealedRefined adds zero state over
// Graded<Absolute, BoolLattice<P>, T>, which is itself zero-cost
// over T (BoolLattice's element_type is empty).  Identical sizeof
// to Refined<P, T> and to bare T.
//
// Witness instantiations.  Use the same `positive` / `non_null` /
// `power_of_two` etc. predicates that Refined.h uses (they are in
// scope through the Refined.h include above).
static_assert(sizeof(SealedRefined<positive,    int>)   == sizeof(int));
static_assert(sizeof(SealedRefined<non_null,    void*>) == sizeof(void*));

}  // namespace crucible::safety
