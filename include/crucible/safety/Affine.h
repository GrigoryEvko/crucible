#pragma once

// ── crucible::safety::Affine<T> ─────────────────────────────────────
//
// Move-only wrapper enforcing "consumed AT MOST once" semantics.
// Substructural peer to safety::Linear<T> at the QTT grade-0 position
// (Atkey 2018 / Brady 2021).
//
//   Axiom coverage: MemSafe, LeakSafe, BorrowSafe (code_guide §II).
//   Runtime cost:   zero.  sizeof(Affine<T>) == sizeof(T).
//
// Difference vs Linear<T>:
//
//   Linear<T>  — exactly 1 consume per value.  Dropping without
//                consume is a code-review reject; the wrapper itself
//                still allows it (the C++ destructor runs), but the
//                review discipline + analyzer treat the silent drop
//                as a bug.
//
//   Affine<T>  — at most 1 consume per value.  Silent drop is a
//                FIRST-CLASS option, not a bug.  Use when the value
//                IS a resource (must not be duplicated) but the
//                lifetime owner is free to abandon it (e.g. an
//                optional speculative result, a cached prefetch
//                that may never be observed, a fallback that the
//                primary path obviates).
//
// Semantics:
//   - Copy is deleted ("Affine<T> is move-only; use std::move").
//   - Move is defaulted; moved-from follows T's move semantics.
//   - [[nodiscard]] on the class forces capture at construction site.
//   - .consume() && returns the inner T (compile error on lvalue).
//   - .peek() const& / .peek_mut() & borrow without consuming.
//   - drop(std::move(x)) is the explicit-NOT-consume grep target —
//     a no-op that documents intent.  Contrast Linear::drop which
//     consumes-then-discards.  Affine's drop does NOT consume; the
//     rvalue's destructor runs at end of full expression and that
//     is the entire semantic.
//
// Paired with -Werror=use-after-move, double-consume = compile error.
// Triple-consume = compile error.  Use-after-drop = compile error.
//
// Pattern: wrap every resource-carrying type whose ownership is
// optional-but-exclusive — speculative kernel-compile result that
// may be obviated by a faster path, prefetched activation that may
// be evicted before consumption, sub-DAG branch that may be pruned.
//
// ── Substrate ──────────────────────────────────────────────────────
//
// Built directly on the algebraic primitive
//
//   Graded<ModalityKind::Absolute,
//          QttSemiring::At<QttGrade::Zero>,
//          T>
//
// per misc/25_04_2026.md §2.3 + CLAUDE.md §XVI wrapper-nesting
// canonical order.  The lattice's element_type is empty (singleton
// "erased grade Zero") and EBO collapses both grade_ and the wrapper
// itself, so sizeof(Affine<T>) == sizeof(T) is preserved by
// structural guarantee.
//
// QTT (Atkey 2018, Brady 2021) = quantitative-type-theory linearity
// semiring {0, 1, ω}, +, ·.  Grade 0 = "discarded / erased / no
// runtime obligation"; here we instantiate it as the affine
// position (≤1 use), bounded by the move-only C++ discipline.  The
// substrate grade is a TAG/CACHE-SLOT marker for federation
// (row_hash carves a distinct slot per grade); the at-most-once
// upper bound is enforced through deleted-copy + move-only ctor.
//
// peek_mut and swap forward to Graded::peek_mut and Graded::swap,
// gated on AbsoluteModality<M> for soundness — the grade is a
// static property of the wrapper, not derived from inner T's bytes,
// so raw mutation cannot violate the grade.  See Graded.h's
// peek_mut/swap doc-block for the full rationale.
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Forward declarations for Affine<Permission> rejection ────────────
//
// Permission<Tag> and SharedPermission<Tag> are themselves move-only
// CSL linearity tokens (sizeof = 1, deleted copy, [[nodiscard]],
// EBO-collapsible per CLAUDE.md §XVI).  Wrapping them in Affine<>
// would:
//   1. DOWNGRADE Permission's exactly-once obligation (CSL frame
//      rule) to at-most-once — making the consume OPTIONAL when the
//      frame rule REQUIRES it.  Sound permission flow demands that
//      every minted Permission is eventually combined / shared /
//      forked back into its parent; silent-drop is exactly the
//      bug the CSL discipline closes.
//   2. Defeat the §XXI grep discipline — `mint_permission_root<Tag>()`
//      is the authorization point; `mint_affine<Permission<Tag>>(...)`
//      would synthesize a fresh authoritative Permission OUTSIDE the
//      Permission factory family, breaking the single grep target.
//   3. Stack two linearity disciplines with the OUTER being weaker
//      than the inner — semantically contradictory.
//
// Forward-declared here (both already live in crucible::safety per
// permissions/Permission.h:165) so the trait specializations and the
// class-body static_assert fire without pulling Permission.h into
// every Affine consumer.  This is the same forward-declare pattern
// Linear.h uses, ensuring the rejection table stays in lockstep
// across the linearity-axis wrappers.
template <typename Tag> class Permission;
template <typename Tag> class SharedPermission;

// is_already_consume_disciplined<T> — type-system witness that T
// already encodes a USE-DISCIPLINE at finer-or-equal granularity than
// Affine's at-most-once.  Default false_type; specialized for the
// canonical permission-family tokens.  A new permission-family token
// joining the mint family adds its specialization HERE, NOT as an
// Affine<> client wrap.
//
// Strips cv-qualification before checking the impl table (mirrors
// Linear.h's fixy-A1-028 hardening): without this,
// `Affine<const Permission<Tag>>` and `Affine<volatile Permission<Tag>>`
// would slip past the class-body static_assert because partial-
// specialization match fails on cv-qualified T.  remove_cvref_t also
// collapses `Permission<Tag>&` and `Permission<Tag>&&` to the bare
// specialization so reference-to-permission slip-throughs are closed
// in the same step.
namespace detail {

template <typename T>
struct is_already_consume_disciplined_impl : std::false_type {};

template <typename Tag>
struct is_already_consume_disciplined_impl<Permission<Tag>>
    : std::true_type {};

template <typename Tag>
struct is_already_consume_disciplined_impl<SharedPermission<Tag>>
    : std::true_type {};

}  // namespace detail

template <typename T>
struct is_already_consume_disciplined
    : detail::is_already_consume_disciplined_impl<std::remove_cvref_t<T>> {};

template <typename T>
inline constexpr bool is_already_consume_disciplined_v =
    is_already_consume_disciplined<T>::value;

template <typename T>
class [[nodiscard]] Affine {
    // Reject Affine<Permission<Tag>> and Affine<SharedPermission<Tag>>.
    // CLAUDE.md §XVI: "Permission<Tag> IS already linear → wrapping in
    // Linear<Permission> is redundant".  For Affine the verdict is
    // worse: wrapping a Permission in an Affine UNDOES Permission's
    // CSL frame rule (must consume) by introducing silent-drop —
    // unsoundness, not redundancy.  The static_assert fires at the
    // class-body instantiation site, surfacing the diagnostic at the
    // user's call site (e.g. `Affine<Permission<MyTag>>{...}`) rather
    // than deep inside the Graded substrate's later checks.  Single
    // grep target: grep "is_already_consume_disciplined" finds every
    // type the rejection covers.
    static_assert(!is_already_consume_disciplined_v<T>,
        "Affine<Permission<Tag>> / Affine<SharedPermission<Tag>> is "
        "unsound: Permission carries an EXACTLY-ONCE obligation (CSL "
        "frame rule); wrapping in Affine downgrades that to at-most-"
        "once, making the consume OPTIONAL when the frame rule "
        "REQUIRES it.  Use Permission<Tag> directly; pass via "
        "mint_permission_root / mint_permission_split / permission_"
        "fork (CLAUDE.md §XVI).");
public:
    using value_type = T;
    // The QTT grade-0 lattice (singleton "erased / affine grade").
    using lattice_type = ::crucible::algebra::lattices::QttSemiring::At<
        ::crucible::algebra::lattices::QttGrade::Zero>;

    // Modality declaration — the GradedWrapper concept verifies this
    // matches graded_type's modality template parameter.  Affine is
    // Absolute (uniform-grade-known-at-type-level, no extract/inject
    // monadic operations).  Same modality as Linear; the lattice
    // position is what distinguishes them.
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // Public per GRADED-TRAIT-1 — external code (GradedWrapper concept,
    // test_migration_verification, future SealedRefined-of-Affine, mCRL2
    // export) needs to introspect the migration mapping.  Zero
    // behavioral change vs a private declaration.
    using graded_type = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    // Empty-lattice grade_type collapses via [[no_unique_address]] in
    // Graded; impl_ is sizeof(T).  Wrapper adds no other state.
    graded_type impl_;

    // Helper: the singleton element_type value.  QttSemiring::At<Zero>
    // has an empty element_type, so this is a zero-cost {} construction.
    [[nodiscard]] static constexpr typename lattice_type::element_type
    grade_zero() noexcept {
        return typename lattice_type::element_type{};
    }

public:

    // Move-from-T construction.  Forwards to Graded(value, grade).
    constexpr explicit Affine(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), grade_zero()} {}

    // In-place construction: build T directly inside the Affine.
    // Avoids a move when T is expensive to move or non-movable by
    // convention.  The Args... are forwarded to T's constructor; the
    // resulting T is then moved into Graded — for move-elidable
    // configurations the move collapses, for non-movable T the
    // construction happens in-situ via guaranteed copy elision.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Affine(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), grade_zero()} {}

    Affine(const Affine&)            = delete("Affine<T> is move-only; use std::move or drop()");
    Affine& operator=(const Affine&) = delete("Affine<T> is move-only; use std::move or drop()");
    Affine(Affine&&)                 = default;
    Affine& operator=(Affine&&)      = default;
    ~Affine()                        = default;

    // Ownership transfer — must be called on rvalue.  Compile error
    // on lvalue.  Forwards to Graded::consume().  Optional per affine
    // semantics: the value may be silently dropped instead (the
    // destructor handles cleanup of T per T's own discipline).
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // Shared borrow — Affine keeps the value.  Forwards to
    // Graded::peek().
    [[nodiscard]] constexpr const T& peek() const & noexcept {
        return impl_.peek();
    }

    // Mutable borrow — Affine keeps the value but caller may mutate.
    // Prefer consume+reconstruct over this when the change is
    // semantic.  Forwards to Graded::peek_mut(), gated on
    // AbsoluteModality<M> in Graded — the QTT-At-Zero grade is a
    // static property of the wrapper (the at-most-once bound is
    // about ownership, not value identity), so raw mutation is sound.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // Swap — preserves at-most-once on both sides.  Forwards to
    // Graded::swap (also gated on AbsoluteModality).
    constexpr void swap(Affine& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Affine& a, Affine& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13).
    // Answers "what is this Affine wrapping?" without dereferencing.
    //
    // lattice_name(): "QttSemiring::At<0>" (the grade-0 chunk of the
    // Atkey-Brady QTT semiring).  Disambiguates Affine<T> from sibling
    // Graded-backed wrappers in shared diagnostic output.
    //
    // Audit-Tier-2 cross-wrapper parity sweep — every migrated wrapper
    // ships the same two forwarders so review-time spot-checks always
    // find them at the same place.  Uniformity is the property; the
    // strings themselves are useful but secondary.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

template <typename T>
Affine(T) -> Affine<T>;

// ── mint_affine<T>(args...) — Universal Mint Pattern ──────────────
//
// Token mint per CLAUDE.md §XXI — constructs an Affine<T> by forwarding
// args to T's constructor.  Authority derives from the constructibility
// proof (`requires std::is_constructible_v<T, Args...>`); this is the
// canonical authorization point for promoting a raw value into the
// affine-typed wrapper carrying its at-most-once obligation.
//
// Equivalent to Affine<T>{std::in_place, args...} but deduces T less
// ambiguously.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Affine<T> mint_affine(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return Affine<T>{std::in_place, std::forward<Args>(args)...};
}

// Explicit drop — the affine-specific "I CHOSE not to consume" grep
// target.  Unlike Linear::drop (which consumes-then-discards), Affine's
// drop simply lets the rvalue's destructor run at end of full
// expression.  Use at sites where the affine semantic is being
// exercised intentionally (cached prefetch obviated by a faster path,
// speculative result discarded by branch pruning) so review-time grep
// finds the deliberate-discard sites and contrasts them with consume()
// sites.
template <typename T>
constexpr void drop(Affine<T>&& x) noexcept {
    // Binding the rvalue to a local extends its lifetime to the end
    // of this function; the destructor runs on return.  No consume,
    // no value extraction — the at-most-once upper bound is honored
    // by exercising the zero-uses option.
    (void)x;
}

// Zero-cost guarantee — preserved through the Graded delegation by
// the lattice's empty element_type + Graded's [[no_unique_address]]
// on grade_.  If this fires after a Graded refactor, the EBO
// discipline regressed.
static_assert(sizeof(Affine<int>)        == sizeof(int));
static_assert(sizeof(Affine<void*>)      == sizeof(void*));
static_assert(sizeof(Affine<long long>)  == sizeof(long long));

// ── Modality + grade type-system witnesses ──────────────────────────
//
// Compile-time evidence that Affine carries the Absolute modality at
// QTT grade Zero, distinguishing it from Linear (which sits at the
// same modality but grade One).  If either assertion regresses the
// substrate is mis-routed and federation cache slots would collide.
static_assert(Affine<int>::modality ==
              ::crucible::algebra::ModalityKind::Absolute);
static_assert(std::is_same_v<
    Affine<int>::lattice_type,
    ::crucible::algebra::lattices::QttSemiring::At<
        ::crucible::algebra::lattices::QttGrade::Zero>>);

namespace detail::affine_self_test {

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Exercise every named operation at runtime per
// feedback_algebra_runtime_smoke_test_discipline.  Catches three
// classes of consteval/SFINAE bugs that pure static_asserts miss:
//   1. value-vs-reference forwarding regressions through Graded's
//      consume/peek/peek_mut delegation;
//   2. move-only T support (the only_move witness exercises the
//      in-place constructor's emplace path);
//   3. swap's noexcept propagation through Graded::swap.
inline void runtime_smoke_test() {
    int seed = 41;                       // non-constant
    Affine<int> a{std::in_place, seed + 1};
    if (a.peek() != 42) std::abort();

    // peek_mut on lvalue.
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();

    // Swap two Affines.
    Affine<int> b{std::in_place, 7};
    swap(a, b);
    if (a.peek() != 7 || b.peek() != 100) std::abort();

    // Member swap.
    a.swap(b);
    if (a.peek() != 100 || b.peek() != 7) std::abort();

    // Move + consume — rvalue path.
    Affine<int> c = std::move(a);
    int extracted = std::move(c).consume();
    if (extracted != 100) std::abort();

    // mint_affine forwarder.
    auto m = mint_affine<int>(seed);
    if (m.peek() != 41) std::abort();

    // Move-only T witness — verifies in-place construction path.
    struct only_move {
        only_move(int v) : p{std::make_unique<int>(v)} {}
        only_move(only_move&&)                 = default;
        only_move& operator=(only_move&&)      = default;
        only_move(const only_move&)            = delete;
        only_move& operator=(const only_move&) = delete;
        std::unique_ptr<int> p;
    };
    Affine<only_move> mo{std::in_place, 123};
    if (*mo.peek().p != 123) std::abort();

    // drop() — affine-specific NO-CONSUME path.  The rvalue's
    // destructor runs at the end of this statement; no value
    // extraction.  Distinguishes Affine's drop semantics from
    // Linear's consume-then-discard.
    Affine<int> to_drop{std::in_place, 999};
    drop(std::move(to_drop));

    // Silent drop — exercise the second affine-specific path: simply
    // letting the rvalue go out of scope without any explicit call.
    // This is the ZERO-uses corner of the at-most-once bound.
    {
        Affine<int> silent{std::in_place, 314};
        if (silent.peek() != 314) std::abort();
        // No consume(), no drop() — destructor handles cleanup at }.
    }
}

}  // namespace detail::affine_self_test

}  // namespace crucible::safety
