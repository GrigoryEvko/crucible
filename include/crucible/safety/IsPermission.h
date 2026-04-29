#pragma once

// ── crucible::safety::extract::is_permission_v / is_shared_permission_v
//    + permission_tag_t<T> / shared_permission_tag_t<T> ────────────────
//
// FOUND-D04 of 28_04_2026_effects.md §10 + 27_04_2026.md §5.5.  The
// dispatcher-side reading surface for CSL permission tokens.  The
// underlying predicates and partial specializations already live in
// `::crucible::safety::detail` (permissions/Permission.h:765-783 +
// concepts at 799-809 — the file lives in the permissions/ directory
// but the namespace is crucible::safety, mirroring the rest of the
// safety/ tree); FOUND-D04 re-exports them into `crucible::safety::
// extract` alongside `is_owned_region_v` (FOUND-D03) so the
// dispatcher's per-parameter introspection happens through ONE
// namespace, not three.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_permission_v<T>           Variable template: true iff T (after
//                                cv-ref stripping) is a specialization
//                                of Permission<Tag> for some Tag.
//   is_shared_permission_v<T>    Same for SharedPermission<Tag>.
//   IsPermission<T>              Concept form for `requires`-clauses.
//   IsSharedPermission<T>        Same for shared-fractional permissions.
//   IsPermissionFor<T, Tag>      Concept binding T == Permission<Tag>
//                                for a SPECIFIC Tag.  Used by
//                                FOUND-D11's inferred_permission_tags_t
//                                to verify a parameter exposes the
//                                expected region tag.
//   IsSharedPermissionFor<T,Tag> Same for SharedPermission<Tag>.
//   permission_tag_t<T>          Alias to Tag when T is a Permission;
//                                ill-formed (constraint failure)
//                                otherwise.
//   shared_permission_tag_t<T>   Alias to Tag when T is a
//                                SharedPermission; ill-formed
//                                otherwise.
//
// ── Why the constrained extractor matters ───────────────────────────
//
// `permission_tag_t<T>` is constrained on `is_permission_v<T>`.
// Without the requires-clause, instantiating `permission_tag_t<int>`
// would silently fall through to a primary-template default
// (`tag_type = void`) and propagate `void` into downstream type
// machinery — breaking any consumer that does
// `Permission<permission_tag_t<T>>` to round-trip the tag.  With the
// constraint, the alias rejects non-Permission arguments at the call
// boundary with a single requires-clause diagnostic.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Forwarder from `crucible::safety::detail::is_permission_impl` +
// `is_shared_permission_impl` (those impls live in the
// `crucible::safety` namespace despite the file path being
// `permissions/Permission.h` — see CLAUDE.md §L0 wrapper-namespace
// convention).  The detail-namespace partial specializations
// (Permission.h:767-781) carry the load-bearing `tag_type = Tag`
// typedef; this header just exposes them through the dispatcher's
// reading namespace `crucible::safety::extract` and adds the
// `requires`-constrained alias surface that FOUND-D's
// parameter-shape protocol consumes.
//
// `std::remove_cvref_t<T>` strips reference and top-level cv qualifiers
// before the trait check so call sites can write the predicate against
// the parameter type directly without manual decay.  Pointers are NOT
// stripped — `Permission<Tag>*` is NOT a Permission, by design.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — partial specialization is the only true case;
//              everything else is false.  No silent conversions, no
//              implicit Permission ↔ SharedPermission aliasing.
//   DetSafe — same T → same value, deterministically; no hidden
//              state.

#include <crucible/permissions/Permission.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── Public surface — variable templates ────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_permission_v =
    ::crucible::safety::detail::is_permission_impl<
        std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_shared_permission_v =
    ::crucible::safety::detail::is_shared_permission_impl<
        std::remove_cvref_t<T>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Public surface — concept aliases ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
concept IsPermission = is_permission_v<T>;

template <typename T>
concept IsSharedPermission = is_shared_permission_v<T>;

// ── Tag-bound concept aliases ───────────────────────────────────────
//
// IsPermissionFor<T, Tag> binds the wrapper-detection check AND the
// specific tag identity.  The dispatcher's parameter-shape
// recognizers (FOUND-D11+) consume this when checking that a
// function parameter exposes a Permission for a SPECIFIC region.

template <typename T, typename Tag>
concept IsPermissionFor =
    is_permission_v<T> &&
    std::is_same_v<
        typename ::crucible::safety::detail::is_permission_impl<
            std::remove_cvref_t<T>>::tag_type,
        Tag>;

template <typename T, typename Tag>
concept IsSharedPermissionFor =
    is_shared_permission_v<T> &&
    std::is_same_v<
        typename ::crucible::safety::detail::is_shared_permission_impl<
            std::remove_cvref_t<T>>::tag_type,
        Tag>;

// ═════════════════════════════════════════════════════════════════════
// ── Tag extractors (constrained) ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `requires`-constrained on the matching predicate so non-matching
// arguments are rejected at the alias declaration rather than
// resolving to `void` from a primary-template default.

template <typename T>
    requires is_permission_v<T>
using permission_tag_t =
    typename ::crucible::safety::detail::is_permission_impl<
        std::remove_cvref_t<T>>::tag_type;

template <typename T>
    requires is_shared_permission_v<T>
using shared_permission_tag_t =
    typename ::crucible::safety::detail::is_shared_permission_impl<
        std::remove_cvref_t<T>>::tag_type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Adversarial cases verified at compile time.  Every claim is
// duplicated by the sentinel TU under the project's full warning
// matrix.

namespace detail::is_permission_self_test {

struct test_tag_a {};
struct test_tag_b {};

using P_a  = ::crucible::safety::Permission<test_tag_a>;
using P_b  = ::crucible::safety::Permission<test_tag_b>;
using SP_a = ::crucible::safety::SharedPermission<test_tag_a>;
using SP_b = ::crucible::safety::SharedPermission<test_tag_b>;
using SG_a = ::crucible::safety::SharedPermissionGuard<test_tag_a>;

// ── is_permission_v positives ─────────────────────────────────────

static_assert(is_permission_v<P_a>);
static_assert(is_permission_v<P_b>);

// Cv-ref stripping — every reference category resolves identically.
static_assert(is_permission_v<P_a&>);
static_assert(is_permission_v<P_a&&>);
static_assert(is_permission_v<P_a const>);
static_assert(is_permission_v<P_a const&>);
static_assert(is_permission_v<P_a const&&>);
static_assert(is_permission_v<P_a volatile>);
static_assert(is_permission_v<P_a const volatile>);

// ── is_permission_v negatives ─────────────────────────────────────

static_assert(!is_permission_v<int>);
static_assert(!is_permission_v<int*>);
static_assert(!is_permission_v<int&>);
static_assert(!is_permission_v<void>);
static_assert(!is_permission_v<test_tag_a>);

// SharedPermission and its guard are NOT Permission — distinct CSL
// surfaces (linear vs fractional vs RAII guard).  Confusing them at
// the trait level would let a fractional handle slip into a
// linear-only consumer.
static_assert(!is_permission_v<SP_a>);
static_assert(!is_permission_v<SG_a>);

// remove_cvref does NOT strip pointers.
static_assert(!is_permission_v<P_a*>);
static_assert(!is_permission_v<P_a* const>);
static_assert(!is_permission_v<P_a const*>);

// ── is_shared_permission_v positives ──────────────────────────────

static_assert(is_shared_permission_v<SP_a>);
static_assert(is_shared_permission_v<SP_b>);
static_assert(is_shared_permission_v<SP_a&>);
static_assert(is_shared_permission_v<SP_a&&>);
static_assert(is_shared_permission_v<SP_a const>);
static_assert(is_shared_permission_v<SP_a const&>);
static_assert(is_shared_permission_v<SP_a volatile>);

// ── is_shared_permission_v negatives ──────────────────────────────

static_assert(!is_shared_permission_v<int>);
static_assert(!is_shared_permission_v<P_a>);
static_assert(!is_shared_permission_v<SG_a>);
static_assert(!is_shared_permission_v<test_tag_a>);
static_assert(!is_shared_permission_v<SP_a*>);

// ── Concept form ─────────────────────────────────────────────────

static_assert(IsPermission<P_a>);
static_assert(IsPermission<P_a&&>);
static_assert(IsPermission<P_a const&>);
static_assert(!IsPermission<int>);
static_assert(!IsPermission<SP_a>);

static_assert(IsSharedPermission<SP_a>);
static_assert(IsSharedPermission<SP_a&&>);
static_assert(!IsSharedPermission<int>);
static_assert(!IsSharedPermission<P_a>);

// ── Tag extraction ────────────────────────────────────────────────

static_assert(std::is_same_v<permission_tag_t<P_a>, test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_b>, test_tag_b>);

static_assert(std::is_same_v<
    shared_permission_tag_t<SP_a>, test_tag_a>);
static_assert(std::is_same_v<
    shared_permission_tag_t<SP_b>, test_tag_b>);

// Cv-ref stripping — tag extraction unwraps.
static_assert(std::is_same_v<permission_tag_t<P_a&>,        test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_a const&>,  test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_a&&>,       test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_a const&&>, test_tag_a>);

static_assert(std::is_same_v<
    shared_permission_tag_t<SP_a&>, test_tag_a>);
static_assert(std::is_same_v<
    shared_permission_tag_t<SP_a const&>, test_tag_a>);
static_assert(std::is_same_v<
    shared_permission_tag_t<SP_a&&>, test_tag_a>);

// Distinct tags → distinct trait extractions.  Two
// Permission<different-tag> are always distinguished even though both
// have sizeof == 1 (empty class) — TypeSafe at the type level.
static_assert(!std::is_same_v<
    permission_tag_t<P_a>, permission_tag_t<P_b>>);
static_assert(!std::is_same_v<
    shared_permission_tag_t<SP_a>, shared_permission_tag_t<SP_b>>);

// Permission<Tag> and SharedPermission<Tag> share the SAME Tag —
// the wrapper differs (linear vs fractional) but the underlying
// region tag is identical, so a SharedPermission<X> cannot be
// confused with a Permission<X> at the trait level even though
// they project the same tag.
static_assert(std::is_same_v<
    permission_tag_t<P_a>,
    shared_permission_tag_t<SP_a>>);

// ── IsPermissionFor / IsSharedPermissionFor ───────────────────────

static_assert(IsPermissionFor<P_a, test_tag_a>);
static_assert(IsPermissionFor<P_a const&, test_tag_a>);
static_assert(IsPermissionFor<P_a&&, test_tag_a>);
static_assert(!IsPermissionFor<P_a, test_tag_b>);  // wrong tag
static_assert(!IsPermissionFor<P_b, test_tag_a>);  // wrong tag
static_assert(!IsPermissionFor<int, test_tag_a>);  // not a Permission
static_assert(!IsPermissionFor<SP_a, test_tag_a>); // not a Permission
                                                   // (it's Shared)

static_assert(IsSharedPermissionFor<SP_a, test_tag_a>);
static_assert(IsSharedPermissionFor<SP_a const&, test_tag_a>);
static_assert(!IsSharedPermissionFor<SP_a, test_tag_b>);
static_assert(!IsSharedPermissionFor<P_a, test_tag_a>);

}  // namespace detail::is_permission_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the runtime-smoke-test discipline.  The traits are purely
// type-level; "exercising at runtime" means computing the predicate
// values with a non-constant flag flow and confirming the trait's
// claims are not optimized into something else.

inline bool is_permission_smoke_test() noexcept {
    using namespace detail::is_permission_self_test;

    // Volatile-bounded loop ensures the trait reads survive
    // dead-code elimination.
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_permission_v<P_a>;
        ok = ok && is_permission_v<P_a&&>;
        ok = ok && !is_permission_v<int>;
        ok = ok && !is_permission_v<SP_a>;

        ok = ok && is_shared_permission_v<SP_a>;
        ok = ok && !is_shared_permission_v<P_a>;
        ok = ok && !is_shared_permission_v<int>;

        ok = ok && IsPermission<P_a>;
        ok = ok && !IsPermission<SP_a>;
        ok = ok && IsSharedPermission<SP_a>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
