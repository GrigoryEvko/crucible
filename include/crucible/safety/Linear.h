#pragma once

// ── crucible::safety::Linear<T> ─────────────────────────────────────
//
// Move-only wrapper enforcing "consumed exactly once" semantics.
//
//   Axiom coverage: MemSafe, LeakSafe, BorrowSafe (code_guide §II).
//   Runtime cost:   zero.  sizeof(Linear<T>) == sizeof(T).
//
// - Copy is deleted with a reason string.
// - Move is defaulted; moved-from state is left to T's move semantics.
// - [[nodiscard]] on the class forces capture at construction sites.
// - .consume() && returns the inner T (compile error on lvalue).
// - .peek() const& / .peek_mut() & borrow without consuming.
// - drop(std::move(x)) explicit discard.
// - In-place construction via std::in_place_t avoids a move.
//
// Paired with -Werror=use-after-move, double-consume = compile error.
//
// Pattern: wrap every resource-carrying type (file handle, mmap region,
// arena-owned object with drop semantics, channel endpoint).
//
// ── MIGRATED to Graded<Absolute, QttSemiring::At<One>, T>  (#461) ──
//
// As of MIGRATE-1 (2026-04-26) Linear<T> is a thin wrapper around the
// algebraic primitive
//
//   Graded<ModalityKind::Absolute,
//          QttSemiring::At<QttGrade::One>,
//          T>
//
// per misc/25_04_2026.md §2.3.  The wrapper preserves every existing
// public API surface (consume / peek / peek_mut / swap / mint_linear /
// drop / in_place ctor / [[nodiscard]] / move-only deletion).  Storage
// is delegated to Graded; the lattice's element_type is empty
// (singleton "linearity grade One") and EBO collapses both grade_ and
// the wrapper itself, so sizeof(Linear<T>) == sizeof(T) is preserved
// by structural guarantee.
//
// QTT (Atkey 2018, Brady 2021) = quantitative-type-theory linearity
// semiring {0, 1, ω}, +, ·.  Grade 0 = "discarded", Grade 1 = "exactly
// once" (Linear<T>'s discipline), Grade ω = "unrestricted" (standard
// C++ value semantics).  Linear<T> instantiates only the grade-1
// case; future Affine<T> / Unrestricted<T> wrappers (if needed) would
// alias the other two grades over the same Graded substrate.
//
// peek_mut and swap forward to Graded::peek_mut and Graded::swap,
// which are gated on AbsoluteModality<M> for soundness — the lattice
// position is a static property of the wrapper, not derived from the
// inner T's bytes, so raw mutation cannot violate the grade.  See
// Graded.h's peek_mut/swap doc-block for the full rationale.
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

// ── Forward declarations for Linear<Permission> rejection (fixy-A1-004) ─
//
// Permission<Tag> and SharedPermission<Tag> are themselves move-only
// CSL linearity tokens (sizeof = 1, deleted copy, EBO-collapsible per
// CLAUDE.md §XVI).  Wrapping them in Linear<> would:
//   1. Stack two linearity disciplines without adding a new bug class
//      Linear catches — both already enforce exactly-once via deleted
//      copy + [[nodiscard]] + use-after-move diagnostic.
//   2. Break the EBO-collapse path: Permission's stateless 1-byte
//      footprint relies on direct embedding; nesting inside Linear's
//      Graded substrate adds a second-pass through the move-only
//      class hierarchy with no payload.
//   3. Defeat the §XXI grep discipline — `mint_permission_root<Tag>()`
//      is the authorization point; `mint_linear<Permission<Tag>>(...)`
//      would synthesize a fresh authoritative Permission OUTSIDE the
//      Permission factory family, breaking the single grep target.
//
// Forward-declared here (both already live in crucible::safety per
// permissions/Permission.h:165) so the trait specializations and the
// class-body static_assert can fire without pulling Permission.h
// (1345 LOC + transitive ExecCtx infrastructure) into every Linear
// consumer.  This is the same forward-declare pattern Permission.h
// itself uses for `class Linear` references.
template <typename Tag> class Permission;
template <typename Tag> class SharedPermission;

// is_already_linear<T> — type-system witness that T's value-level
// discipline already encodes the exactly-once obligation.  Default
// false_type; specialized for Permission<Tag> and SharedPermission<Tag>.
// New permission-family tokens that join the canonical mint family
// (e.g. future ReadView token, FederatedPeerPermission) add their
// specialization HERE, not as a Linear<> client wrap.
//
// fixy-A1-028 (#1565 / fixy-L-01 #1517): the public trait
// `is_already_linear<T>` strips cv-qualification before checking
// the impl table.  Without this, `Linear<const Permission<Tag>>`
// and `Linear<volatile Permission<Tag>>` slip past the class-body
// static_assert (partial-specialization match fails on cv-qualified
// T), producing the documented-rejected-but-compiles drift CLAUDE.md
// §XVI calls out.  remove_cvref_t also collapses `Permission<Tag>&`
// and `Permission<Tag>&&` to the bare specialization so reference-
// to-permission slip-throughs are closed in the same step.
namespace detail {

template <typename T>
struct is_already_linear_impl : std::false_type {};

template <typename Tag>
struct is_already_linear_impl<Permission<Tag>> : std::true_type {};

template <typename Tag>
struct is_already_linear_impl<SharedPermission<Tag>> : std::true_type {};

}  // namespace detail

template <typename T>
struct is_already_linear
    : detail::is_already_linear_impl<std::remove_cvref_t<T>> {};

template <typename T>
inline constexpr bool is_already_linear_v = is_already_linear<T>::value;

template <typename T>
class [[nodiscard]] Linear {
    // fixy-A1-004: reject Linear<Permission<Tag>> and
    // Linear<SharedPermission<Tag>>.  CLAUDE.md §XVI:
    //   "Permission<Tag> IS already linear → wrapping in Linear<Permission>
    //    is redundant".
    // The static_assert fires at the class-body instantiation site, so
    // the diagnostic surfaces at the user's call site (e.g.
    // `Linear<Permission<MyTag>>{...}`) rather than deep inside the
    // Graded substrate's later checks.  Single grep target:
    // grep "is_already_linear" finds every type the rejection covers.
    static_assert(!is_already_linear_v<T>,
        "Linear<Permission<Tag>> / Linear<SharedPermission<Tag>> is "
        "redundant: Permission IS already a move-only linearity token "
        "(deleted copy, [[nodiscard]], sizeof = 1, EBO-collapsible).  "
        "Wrapping it in Linear<> stacks two disciplines without adding "
        "a new bug class and defeats the §XXI mint-grep discipline.  "
        "Use Permission<Tag> directly; pass via mint_permission_root / "
        "mint_permission_split / permission_fork (CLAUDE.md §XVI).");
public:
    using value_type = T;
    // The QTT grade-1 lattice (singleton "exactly one ownership").
    using lattice_type = ::crucible::algebra::lattices::QttSemiring::At<
        ::crucible::algebra::lattices::QttGrade::One>;

    // Modality declaration — Round-4 CHEAT-5: the GradedWrapper
    // concept verifies this matches graded_type's modality template
    // parameter.  Linear is Absolute (uniform-grade-known-at-type-
    // level, no extract/inject monadic operations).
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // Public per GRADED-TRAIT-1 — external code (GradedWrapper concept,
    // test_migration_verification, future SealedRefined-of-Linear,
    // mCRL2 export) needs to introspect the migration mapping.  Zero
    // behavioral change vs the prior private declaration.
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    // Empty-lattice grade_type collapses via [[no_unique_address]] in
    // Graded; impl_ is sizeof(T).  Wrapper adds no other state.
    graded_type impl_;

    // Helper: the singleton element_type value.  QttSemiring::At<One>
    // has an empty element_type, so this is a zero-cost {} construction.
    [[nodiscard]] static constexpr typename lattice_type::element_type
    grade_one() noexcept {
        return typename lattice_type::element_type{};
    }

public:

    // Move-from-T construction.  Forwards to Graded(value, grade).
    constexpr explicit Linear(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), grade_one()} {}

    // In-place construction: build T directly inside the Linear.
    // Avoids a move when T is expensive to move or non-movable by
    // convention.  The Args... are forwarded to T's constructor; the
    // resulting T is then moved into Graded — for move-elidable
    // configurations the move collapses, for non-movable T the
    // construction happens in-situ via guaranteed copy elision.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Linear(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), grade_one()} {}

    Linear(const Linear&)            = delete("Linear<T> is move-only; use std::move or drop()");
    Linear& operator=(const Linear&) = delete("Linear<T> is move-only; use std::move or drop()");
    Linear(Linear&&)                 = default;
    Linear& operator=(Linear&&)      = default;
    ~Linear()                        = default;

    // Ownership transfer — must be called on rvalue.  Compile error
    // on lvalue.  Forwards to Graded::consume().
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // Shared borrow — Linear keeps the value.  Forwards to
    // Graded::peek().
    [[nodiscard]] constexpr const T& peek() const & noexcept {
        return impl_.peek();
    }

    // Mutable borrow — Linear keeps the value but caller may mutate.
    // Prefer consume+reconstruct over this when the change is
    // semantic.  Forwards to Graded::peek_mut(), gated on
    // AbsoluteModality<M> in Graded — the QTT-At-One grade is a
    // static property of the wrapper (linearity is about ownership,
    // not value identity), so raw mutation is sound.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // Swap — preserves linearity on both sides.  Forwards to
    // Graded::swap (also gated on AbsoluteModality).
    constexpr void swap(Linear& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Linear& a, Linear& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13).
    // Answers "what is this Linear wrapping?" without dereferencing.
    //
    // lattice_name(): "QttSemiring::At<One>" (the grade-1 chunk of the
    // Atkey-Brady QTT semiring).  Disambiguates Linear<T> from sibling
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
Linear(T) -> Linear<T>;

// ── mint_linear<T>(args...) — Universal Mint Pattern ──────────────
//
// Token mint per CLAUDE.md §XXI — constructs a Linear<T> by forwarding
// args to T's constructor.  Authority derives from the constructibility
// proof (`requires std::is_constructible_v<T, Args...>`); this is the
// canonical authorization point for promoting a raw value into the
// linear-typed wrapper carrying its consume-once obligation.
//
// Equivalent to Linear<T>{std::in_place, args...} but deduces T less
// ambiguously.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> mint_linear(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return Linear<T>{std::in_place, std::forward<Args>(args)...};
}

// Explicit discard — equivalent to `let _ = std::move(x).consume();`
template <typename T>
constexpr void drop(Linear<T>&& x)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    (void)std::move(x).consume();
}

// Zero-cost guarantee — preserved through the Graded delegation by
// the lattice's empty element_type + Graded's [[no_unique_address]]
// on grade_.  If this fires after a Graded refactor, the EBO
// discipline regressed.
static_assert(sizeof(Linear<int>)        == sizeof(int));
static_assert(sizeof(Linear<void*>)      == sizeof(void*));
static_assert(sizeof(Linear<long long>)  == sizeof(long long));

namespace detail::linear_self_test {

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
    Linear<int> a{std::in_place, seed + 1};
    if (a.peek() != 42) std::abort();

    // peek_mut on lvalue.
    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();

    // Swap two Linears.
    Linear<int> b{std::in_place, 7};
    swap(a, b);
    if (a.peek() != 7 || b.peek() != 100) std::abort();

    // Member swap.
    a.swap(b);
    if (a.peek() != 100 || b.peek() != 7) std::abort();

    // Move + consume — rvalue path.
    Linear<int> c = std::move(a);
    int extracted = std::move(c).consume();
    if (extracted != 100) std::abort();

    // mint_linear forwarder.
    auto m = mint_linear<int>(seed);
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
    Linear<only_move> mo{std::in_place, 123};
    if (*mo.peek().p != 123) std::abort();

    // drop() consumes without binding the result.
    Linear<int> to_drop{std::in_place, 999};
    drop(std::move(to_drop));
}

}  // namespace detail::linear_self_test

} // namespace crucible::safety
