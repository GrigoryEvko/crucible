#pragma once

// Permission<Tag> mechanizes the frame rule of concurrent separation
// logic.  Move-only linearity stands in for the separating conjunction:
// at most one owner of a region exists at any moment, so two threads
// holding permissions for disjoint tags provably cannot conflict.
//
// Nothing ties a tag to the memory it names, and nothing confines the
// holder's writes to that memory.  Both are obligations on the code
// that holds the token.  Keeping the permission inside a move-only
// handle, so the handle's methods are the gated operations, discharges
// the second one in practice.
//
// A permission stored as a bare member of a type that is itself shared
// between threads defeats linearity, because the sharing is invisible
// to the type system.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_FractionalLattice.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/_Diagnostic.h>
#include <crucible/safety/_Pinned.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// An unconstrained tag fails silently rather than loudly: a
// `Permission<Tag*>` compiles, but the splits_into specialization
// written for `Tag` never matches it and the pool instantiates under a
// different key, so every discipline check quietly stops applying.

template <typename T>
concept PermissionTag = std::is_class_v<T> && std::is_empty_v<T>;

template <typename Tag>
class Permission;

}  // namespace crucible::safety

namespace crucible::permissions {

namespace tag {

struct DiskSpilledRegionTag {};
struct HugePageTag {};
struct MmapRegionTag {};
struct GpuMemoryTag {};
struct NetworkBufferTag {};

}  // namespace tag

namespace detail {

template <typename Tag>
struct mint_permission_inherit_minter_;

struct FederationMintAccess;

}  // namespace detail

}  // namespace crucible::permissions

namespace crucible::safety {

template <typename Tag>
class SharedPermission;
template <typename Tag>
class SharedPermissionGuard;
template <typename Tag>
class SharedPermissionPool;

template <typename T, typename Tag>
class OwnedRegion;

namespace detail {
class ForkRebuildKey;
struct ForkRebuildAccess;

// A friend declaration of a constrained function template must repeat
// the constraint exactly.  Friending the structured-join primitives
// directly would therefore drag their whole requires-clauses, and every
// type those clauses name, into this header.  The rebuild routes
// through this constraint-free helper instead.
template <typename Parent>
[[nodiscard]] constexpr Permission<Parent> rebuild_parent_after_fork_() noexcept;
}  // namespace detail

// The declarative manifest of valid splits.  C++ has no orphan rule, so
// a foreign translation unit can specialize this trait for a tag it does
// not own and forge cross-region authority.  A manifest must therefore
// be declared in the same translation unit as the parent tag, and a
// build-time source scan rejects one that is not.

template <typename Parent, typename L, typename R>
struct splits_into : std::false_type {};

template <typename Parent, typename L, typename R>
inline constexpr bool splits_into_v = splits_into<Parent, L, R>::value;

template <typename Parent, typename... Children>
struct splits_into_pack : std::false_type {};

template <typename Parent, typename... Children>
inline constexpr bool splits_into_pack_v = splits_into_pack<Parent, Children...>::value;

// A second trait that every legitimate split must specialize alongside
// the first, in the same translation unit.  The duplication is not
// redundant.  A forged specialization of splits_into alone is caught
// here, at the mint gate.  A forged specialization of both is caught by
// the build-time source scan, which looks for either trait name outside
// the locations allowed to author manifests.  Defeating the discipline
// takes both.

template <typename Parent, typename L, typename R>
struct splits_into_authoring_witness : std::false_type {};

template <typename Parent, typename L, typename R>
inline constexpr bool splits_into_authoring_witness_v = splits_into_authoring_witness<Parent, L, R>::value;

template <typename Parent, typename... Children>
struct splits_into_pack_authoring_witness : std::false_type {};

template <typename Parent, typename... Children>
inline constexpr bool splits_into_pack_authoring_witness_v =
    splits_into_pack_authoring_witness<Parent, Children...>::value;

template <typename Parent, typename L, typename R>
inline constexpr bool well_authored_split_v =
    splits_into_v<Parent, L, R> && splits_into_authoring_witness_v<Parent, L, R>;

template <typename Parent, typename... Children>
inline constexpr bool well_authored_split_pack_v =
    splits_into_pack_v<Parent, Children...> && splits_into_pack_authoring_witness_v<Parent, Children...>;

// The split manifests say which parent a set of children decomposes,
// but nothing about whether those children name disjoint regions.  A
// manifest declaring the same tag twice would mint two linear tokens
// for one region and hand each to a different thread, which is the race
// the frame rule exists to rule out.

namespace detail {

template <typename... Children>
struct all_distinct_tags_rec : std::true_type {};

template <typename Head, typename... Rest>
struct all_distinct_tags_rec<Head, Rest...>
    : std::bool_constant<(!std::is_same_v<Head, Rest> && ...) && all_distinct_tags_rec<Rest...>::value> {};

}  // namespace detail

template <typename... Children>
inline constexpr bool all_distinct_tags_v = detail::all_distinct_tags_rec<Children...>::value;

template <typename Tag>
struct permission_row {
    using type = ::crucible::effects::Row<>;
};

template <typename Tag>
using permission_row_t = typename permission_row<Tag>::type;

template <typename Tag>
inline constexpr bool permission_row_empty_v = ::crucible::effects::row_size_v<permission_row_t<Tag>> == 0;

template <typename Tag, typename Ctx>
concept CtxAdmitsPermission = ::crucible::effects::IsExecCtx<Ctx>
                           && ::crucible::effects::is_subrow_v<permission_row_t<Tag>, typename Ctx::row_type>;

template <>
struct permission_row<::crucible::permissions::tag::DiskSpilledRegionTag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
};

template <>
struct permission_row<::crucible::permissions::tag::HugePageTag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};

template <>
struct permission_row<::crucible::permissions::tag::MmapRegionTag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};

template <>
struct permission_row<::crucible::permissions::tag::GpuMemoryTag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::Alloc>;
};

template <>
struct permission_row<::crucible::permissions::tag::NetworkBufferTag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};

// The tag constraint is a class-body static_assert rather than a
// requires-clause on the primary template.  A requires-clause would
// force every forward declaration of Permission to repeat it, which
// defeats forward-declare-and-specialize as a way of avoiding this
// header.

template <typename Tag>
class [[nodiscard]] Permission {
    static_assert(PermissionTag<Tag>, "Permission<Tag>: Tag must be an empty non-union class type "
                                      "(see PermissionTag concept above).  Pointers, references, "
                                      "primitives, enums, unions, and stateful classes are rejected. "
                                      "Per CSL convention, Tag is a phantom-type marker — typically "
                                      "an empty struct in a `tag::` namespace.");
    constexpr Permission() noexcept = default;

    // Every entry in the friend list below is another way to forge
    // authority over a region.  Additions need review.

    template <typename T>
    friend constexpr Permission<T> mint_permission_root() noexcept;

    template <typename T, ::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<T, Ctx>
    friend constexpr Permission<T> mint_permission_root(Ctx const&) noexcept;

    template <typename L, typename R, typename In>
    friend constexpr std::pair<Permission<L>, Permission<R>> mint_permission_split(Permission<In>&&) noexcept;

    template <typename L, typename R, typename In, ::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<In, Ctx> && CtxAdmitsPermission<L, Ctx> && CtxAdmitsPermission<R, Ctx>
    friend constexpr std::pair<Permission<L>, Permission<R>> mint_permission_split(Ctx const&,
                                                                                   Permission<In>&&) noexcept;

    template <typename L, typename R, typename In, ::crucible::effects::IsExecCtx LCtx,
              ::crucible::effects::IsExecCtx RCtx>
        requires CtxAdmitsPermission<L, LCtx> && CtxAdmitsPermission<R, RCtx>
    friend constexpr std::pair<Permission<L>, Permission<R>> mint_permission_split(LCtx const&, RCtx const&,
                                                                                   Permission<In>&&) noexcept;

    template <typename In, typename L, typename R>
    friend constexpr Permission<In> mint_permission_combine(Permission<L>&&, Permission<R>&&) noexcept;

    template <typename In, typename L, typename R, ::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<In, Ctx> && CtxAdmitsPermission<L, Ctx> && CtxAdmitsPermission<R, Ctx>
    friend constexpr Permission<In> mint_permission_combine(Ctx const&, Permission<L>&&, Permission<R>&&) noexcept;

    template <typename... Children, typename In>
    friend constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Permission<In>&&) noexcept;

    template <typename... Children, typename In, ::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<In, Ctx> && (CtxAdmitsPermission<Children, Ctx> && ...)
    friend constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Ctx const&, Permission<In>&&) noexcept;

    template <typename Parent, typename... Children>
    friend constexpr Permission<Parent> mint_permission_combine_n(Permission<Children>&&...) noexcept;

    template <typename Parent, ::crucible::effects::IsExecCtx Ctx, typename... Children>
        requires CtxAdmitsPermission<Parent, Ctx> && (CtxAdmitsPermission<Children, Ctx> && ...)
    friend constexpr Permission<Parent> mint_permission_combine_n(Ctx const&, Permission<Children>&&...) noexcept;

    // The soundness gate on the post-join rebuild is the passkey, not
    // this friendship, which only reaches the private constructor.
    friend struct ::crucible::safety::detail::ForkRebuildAccess;

    template <typename T>
    friend struct ::crucible::permissions::detail::mint_permission_inherit_minter_;

    // Federation peer tags are the one family for which no root mint
    // exists.  Their only path to a token runs through this helper.
    friend struct ::crucible::permissions::detail::FederationMintAccess;

    // The pool re-emits its parked permission once the count of
    // outstanding shares reaches zero.  Issuing it is sound because the
    // state-machine compare-exchange that authorises the issue proves no
    // other holder exists at that moment.
    template <typename T>
    friend class SharedPermissionPool;

public:
    using tag_type = Tag;

    Permission(const Permission&) = delete(
        "Permission<Tag>: linear — duplicating creates two simultaneous owners of the same region, breaking CSL's frame rule.  Use std::move to transfer.");
    Permission& operator=(const Permission&) =
        delete("Permission<Tag>: linear — assignment would overwrite an existing permission token.");
    constexpr Permission(Permission&&) noexcept = default;
    constexpr Permission& operator=(Permission&&) noexcept = default;
    ~Permission() = default;
};

// Discards the token where letting it fall out of scope would read as
// an oversight.

template <typename Tag>
constexpr void permission_drop(Permission<Tag>&&) noexcept {}

// Reentrant by design.  The contract is a fresh token per call, not one
// token per tag per program.  Soundness rides on each token's move-only
// linearity at the scope that holds it, so two live tokens for one tag
// are sound as long as neither is aliased.  Federation peer tags are the
// exception: for them both overloads here are deleted, and admittance is
// the only path to a token.
template <typename Tag>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root() noexcept {
    static_assert(permission_row_empty_v<Tag>,
                  "mint_permission_root<Tag>() without an ExecCtx is only valid for "
                  "permission_row<Tag> == Row<>.  Effectful permission tags must be "
                  "minted with mint_permission_root<Tag>(ctx) so Ctx admits the tag's row.");
    return Permission<Tag>{};
}

template <typename Tag, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root(Ctx const&) noexcept {
    return Permission<Tag>{};
}

template <typename Tag, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag> admit_permission(Ctx const&, Permission<Tag>&& perm) noexcept {
    return std::move(perm);
}

template <typename Tag, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag> permission_handoff(Ctx const& ctx, Permission<Tag>&& perm) noexcept {
    return admit_permission(ctx, std::move(perm));
}

template <typename L, typename R, typename In>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>>
mint_permission_split(Permission<In>&& parent) noexcept {
    static_assert(permission_row_empty_v<In> && permission_row_empty_v<L> && permission_row_empty_v<R>,
                  "mint_permission_split<L, R>(Permission<In>&&) without ExecCtx is "
                  "only valid when parent and child permission rows are Row<>.  Use "
                  "the ctx-bound split overload for row-bearing permission tags.");
    static_assert(splits_into_v<In, L, R>, "mint_permission_split<L, R>(Permission<In>&&) requires "
                                           "splits_into<In, L, R>::value to be specialized true.  "
                                           "Declare the split in the same TU that defines the tags.");
    static_assert(splits_into_authoring_witness_v<In, L, R>, "splits_into<In, L, R> is true but the accompanying "
                                                             "splits_into_authoring_witness<In, L, R> specialization "
                                                             "is missing.  Every legitimate split ships the witness in "
                                                             "the same TU as the trait.  Add `template <> struct "
                                                             "splits_into_authoring_witness<In, L, R> : "
                                                             "std::true_type {};` next to the splits_into "
                                                             "specialization.");
    static_assert(all_distinct_tags_v<L, R>, "mint_permission_split<L, R> requires L and R to be "
                                             "DISTINCT region tags.  A manifest declaring "
                                             "splits_into<In, A, A> would mint two Permission<A> from one "
                                             "parent — two linear tokens for the SAME region, aliasing "
                                             "the very disjointness the CSL frame rule proves.");
    (void)parent;
    return std::pair<Permission<L>, Permission<R>>{Permission<L>{}, Permission<R>{}};
}

template <typename L, typename R, typename In, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<In, Ctx> && CtxAdmitsPermission<L, Ctx> && CtxAdmitsPermission<R, Ctx>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>>
mint_permission_split(Ctx const&, Permission<In>&& parent) noexcept {
    static_assert(splits_into_v<In, L, R>, "mint_permission_split(ctx, Permission<In>&&) requires "
                                           "splits_into<In, L, R>::value to be specialized true.");
    static_assert(splits_into_authoring_witness_v<In, L, R>, "splits_into_authoring_witness<In, L, R> missing; "
                                                             "declare it next to the splits_into specialization in "
                                                             "the same TU.");
    static_assert(all_distinct_tags_v<L, R>, "mint_permission_split<L, R> requires L and R to be "
                                             "DISTINCT region tags (no Permission<A> aliasing).");
    (void)parent;
    return std::pair<Permission<L>, Permission<R>>{Permission<L>{}, Permission<R>{}};
}

template <typename L, typename R, typename In, ::crucible::effects::IsExecCtx LCtx, ::crucible::effects::IsExecCtx RCtx>
    requires CtxAdmitsPermission<L, LCtx> && CtxAdmitsPermission<R, RCtx>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>>
mint_permission_split(LCtx const&, RCtx const&, Permission<In>&& parent) noexcept {
    static_assert(splits_into_v<In, L, R>, "mint_permission_split(left_ctx, right_ctx, Permission<In>&&) "
                                           "requires splits_into<In, L, R>::value true.");
    static_assert(splits_into_authoring_witness_v<In, L, R>, "splits_into_authoring_witness<In, L, R> missing for "
                                                             "asymmetric-ctx split; declare it next to the "
                                                             "splits_into specialization.");
    static_assert(all_distinct_tags_v<L, R>, "mint_permission_split<L, R> requires L and R to be "
                                             "DISTINCT region tags (no Permission<A> aliasing).");
    (void)parent;
    return std::pair<Permission<L>, Permission<R>>{Permission<L>{}, Permission<R>{}};
}

template <typename In, typename L, typename R>
[[nodiscard]] constexpr Permission<In> mint_permission_combine(Permission<L>&& left, Permission<R>&& right) noexcept {
    static_assert(permission_row_empty_v<In> && permission_row_empty_v<L> && permission_row_empty_v<R>,
                  "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                  "without ExecCtx is only valid for Row<> permission tags.");
    static_assert(splits_into_v<In, L, R>, "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                                           "requires splits_into<In, L, R>::value true.");
    static_assert(splits_into_authoring_witness_v<In, L, R>, "splits_into_authoring_witness<In, L, R> missing for "
                                                             "combine; declare it next to the splits_into "
                                                             "specialization.");
    (void)left;
    (void)right;
    return Permission<In>{};
}

template <typename In, typename L, typename R, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<In, Ctx> && CtxAdmitsPermission<L, Ctx> && CtxAdmitsPermission<R, Ctx>
[[nodiscard]] constexpr Permission<In> mint_permission_combine(Ctx const&, Permission<L>&& left,
                                                               Permission<R>&& right) noexcept {
    static_assert(splits_into_v<In, L, R>, "mint_permission_combine(ctx, Permission<L>&&, "
                                           "Permission<R>&&) requires splits_into<In, L, R>::value true.");
    static_assert(splits_into_authoring_witness_v<In, L, R>, "splits_into_authoring_witness<In, L, R> missing for "
                                                             "ctx-bound combine; declare it next to the splits_into "
                                                             "specialization.");
    (void)left;
    (void)right;
    return Permission<In>{};
}

template <typename... Children, typename In>
[[nodiscard]] constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Permission<In>&& parent) noexcept {
    static_assert(permission_row_empty_v<In> && (permission_row_empty_v<Children> && ...),
                  "mint_permission_split_n<Children...>(Permission<In>&&) without ExecCtx "
                  "is only valid when every permission row is Row<>.  Use the ctx-bound "
                  "split_n overload for row-bearing permission tags.");
    static_assert(splits_into_pack_v<In, Children...>, "mint_permission_split_n<Children...>(Permission<In>&&) "
                                                       "requires splits_into_pack<In, Children...>::value true.");
    static_assert(splits_into_pack_authoring_witness_v<In, Children...>,
                  "splits_into_pack_authoring_witness<In, Children...> "
                  "missing; declare it next to the splits_into_pack "
                  "specialization in the same TU.");
    static_assert(all_distinct_tags_v<Children...>, "mint_permission_split_n<Children...> requires the "
                                                    "child tags to be PAIRWISE DISTINCT.  A manifest declaring "
                                                    "splits_into_pack<In, A, A, ...> would mint two Permission<A> "
                                                    "from one parent — aliasing the same region across the "
                                                    "disjoint children the CSL frame rule promises.");
    (void)parent;
    return std::tuple<Permission<Children>...>{Permission<Children>{}...};
}

template <typename... Children, typename In, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<In, Ctx> && (CtxAdmitsPermission<Children, Ctx> && ...)
[[nodiscard]] constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Ctx const&,
                                                                                    Permission<In>&& parent) noexcept {
    static_assert(splits_into_pack_v<In, Children...>, "mint_permission_split_n(ctx, Permission<In>&&) requires "
                                                       "splits_into_pack<In, Children...>::value true.");
    static_assert(splits_into_pack_authoring_witness_v<In, Children...>,
                  "splits_into_pack_authoring_witness<In, Children...> "
                  "missing for ctx-bound split_n; declare next to the "
                  "splits_into_pack specialization.");
    static_assert(all_distinct_tags_v<Children...>, "mint_permission_split_n<Children...> requires the "
                                                    "child tags to be PAIRWISE DISTINCT (no Permission<A> "
                                                    "aliasing across children).");
    (void)parent;
    return std::tuple<Permission<Children>...>{Permission<Children>{}...};
}

template <typename Parent, typename... Children>
[[nodiscard]] constexpr Permission<Parent> mint_permission_combine_n(Permission<Children>&&... children) noexcept {
    static_assert(permission_row_empty_v<Parent> && (permission_row_empty_v<Children> && ...),
                  "mint_permission_combine_n<Parent, Children...>(...) without ExecCtx "
                  "is only valid when every permission row is Row<>.");
    static_assert(splits_into_pack_v<Parent, Children...>, "mint_permission_combine_n<Parent, Children...>("
                                                           "Permission<Children>&&...) requires "
                                                           "splits_into_pack<Parent, Children...>::value true.  "
                                                           "The combine call must mirror the prior split_n; "
                                                           "declare the manifest in the same TU as the tags.");
    static_assert(splits_into_pack_authoring_witness_v<Parent, Children...>,
                  "splits_into_pack_authoring_witness<Parent, Children...> "
                  "missing for combine_n; declare next to the "
                  "splits_into_pack specialization.");
    static_assert(all_distinct_tags_v<Children...>, "mint_permission_combine_n<Parent, Children...> "
                                                    "requires the child tags to be PAIRWISE DISTINCT — folding "
                                                    "two Permission<A> back into one parent would require two "
                                                    "aliasing tokens to have existed.");
    (void)std::tie(children...);
    return Permission<Parent>{};
}

template <typename Parent, ::crucible::effects::IsExecCtx Ctx, typename... Children>
    requires CtxAdmitsPermission<Parent, Ctx> && (CtxAdmitsPermission<Children, Ctx> && ...)
[[nodiscard]] constexpr Permission<Parent> mint_permission_combine_n(Ctx const&,
                                                                     Permission<Children>&&... children) noexcept {
    static_assert(splits_into_pack_v<Parent, Children...>,
                  "mint_permission_combine_n(ctx, Permission<Children>&&...) "
                  "requires splits_into_pack<Parent, Children...>::value true.");
    static_assert(splits_into_pack_authoring_witness_v<Parent, Children...>,
                  "splits_into_pack_authoring_witness<Parent, Children...> "
                  "missing for ctx-bound combine_n; declare next to the "
                  "splits_into_pack specialization.");
    static_assert(all_distinct_tags_v<Children...>, "mint_permission_combine_n<Parent, Children...> "
                                                    "requires the child tags to be PAIRWISE DISTINCT (no "
                                                    "Permission<A> aliasing across children).");
    (void)std::tie(children...);
    return Permission<Parent>{};
}

// Reissuing the parent after a structured join is sound because every
// child callable consumed its child permission inside its own body and
// the join completed before the rebuild.  No child permission remains
// live, so the parent region is again exclusively available to the
// joining scope.
//
// The passkey is what confines that reissue to the structured-join
// primitives.  Its default constructor is private, so any other call
// site fails to construct the key it would have to pass.

namespace detail {

class ForkRebuildKey {
private:
    constexpr ForkRebuildKey() noexcept = default;

    template <typename UParent>
    friend constexpr Permission<UParent> rebuild_parent_after_fork_() noexcept;
};

struct ForkRebuildAccess {
    template <typename T>
    [[nodiscard]] static constexpr Permission<T> rebuild(ForkRebuildKey) noexcept {
        return Permission<T>{};
    }
};

template <typename Parent>
[[nodiscard]] constexpr Permission<Parent> rebuild_parent_after_fork_() noexcept {
    return ForkRebuildAccess::rebuild<Parent>(ForkRebuildKey{});
}

}  // namespace detail

// Fractional permissions generalize the binary own-or-not of plain
// separation logic to a share `e ↦_p v` for 0 < p ≤ 1.  A share of 1 is
// exclusive read-write, anything less is shared read, and shares summing
// to 1 recover the exclusive.  The split-merge law
// `e ↦_(p+q) v ⟺ e ↦_p v * e ↦_q v` is what licenses handing shares out
// and taking them back.
//
// The proof of a share and the lifetime of a share are separate objects
// here: the token below is the proof, and the guard further down is the
// lifetime.

template <typename Tag>
class [[nodiscard]] SharedPermission {
    constexpr SharedPermission() noexcept = default;

    template <typename T>
    friend class SharedPermissionGuard;
    template <typename T>
    friend constexpr SharedPermission<T> mint_permission_share(Permission<T>&&) noexcept;

    template <typename T, ::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<T, Ctx>
    friend constexpr SharedPermission<T> mint_permission_share(Ctx const&, Permission<T>&&) noexcept;

public:
    using tag_type = Tag;

    // The token confers nothing: the class is empty and exposes no
    // accessor.  The object that stands for a live shared read is the
    // guard, whose lifetime is the pool's refcount.  A token copied out
    // of a guard and stashed past that guard's destruction is therefore
    // not a stale read-proof, because it was never a read-proof.  Code
    // gates access on holding a guard.
    static constexpr bool confers_runtime_access = false;

    // Modelling this token as a graded value carrying its own share
    // would cost every instance its empty layout, and would put the
    // share in the wrong place: the authoritative share count is the
    // pool's atomic state, not anything an individual token knows.  The
    // alias exists only so the token introspects like the other graded
    // wrappers.  Its value type is the tag, because the proof's value is
    // its identity.
    using value_type = Tag;
    using lattice_type = ::crucible::algebra::lattices::FractionalLattice;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, Tag>;

    constexpr SharedPermission(const SharedPermission&) noexcept = default;
    constexpr SharedPermission(SharedPermission&&) noexcept = default;
    constexpr SharedPermission& operator=(const SharedPermission&) noexcept = default;
    constexpr SharedPermission& operator=(SharedPermission&&) noexcept = default;
    ~SharedPermission() = default;

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

template <typename Tag>
class [[nodiscard]] SharedPermissionGuard {
    SharedPermissionPool<Tag>* pool_ = nullptr;

    constexpr explicit SharedPermissionGuard(SharedPermissionPool<Tag>& p) noexcept : pool_{&p} {}
    friend class SharedPermissionPool<Tag>;

public:
    using tag_type = Tag;

    SharedPermissionGuard(const SharedPermissionGuard&) =
        delete("RAII guard owns one outstanding share — copy would double-count");
    SharedPermissionGuard& operator=(const SharedPermissionGuard&) =
        delete("RAII guard owns one outstanding share — assignment would double-count");
    constexpr SharedPermissionGuard(SharedPermissionGuard&& other) noexcept
        : pool_{std::exchange(other.pool_, nullptr)} {}
    SharedPermissionGuard& operator=(SharedPermissionGuard&&) =
        delete("RAII guard's lifetime is fixed at construction; reassignment would double-decrement");

    ~SharedPermissionGuard();

    [[nodiscard]] constexpr SharedPermission<Tag> token() const CRUCIBLE_LIFETIMEBOUND noexcept {
        return SharedPermission<Tag>{};
    }

    [[nodiscard]] constexpr bool holds_share() const noexcept { return pool_ != nullptr; }
};

[[noreturn]] CRUCIBLE_COLD inline void shared_permission_pool_saturated_abort_() noexcept {
    using Tag = diag::SharedPermissionPoolSaturated;
    std::fprintf(stderr,
                 "crucible: fatal contract violation: %.*s\n"
                 "  description: %.*s\n"
                 "  remediation: %.*s\n",
                 static_cast<int>(Tag::name.size()), Tag::name.data(), static_cast<int>(Tag::description.size()),
                 Tag::description.data(), static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
    std::abort();
}

template <typename Tag>
class SharedPermissionPool : public Pinned<SharedPermissionPool<Tag>> {
public:
    using tag_type = Tag;

    static constexpr std::uint64_t EXCLUSIVE_OUT_BIT = std::uint64_t{1} << 63;
    static constexpr std::uint64_t COUNT_MASK = EXCLUSIVE_OUT_BIT - std::uint64_t{1};

    constexpr explicit SharedPermissionPool(Permission<Tag>&& exc) noexcept : parked_{std::move(exc)}, state_{0} {}

    [[nodiscard]] std::optional<SharedPermissionGuard<Tag>> lend() noexcept {
        static_assert(permission_row_empty_v<Tag>, "SharedPermissionPool<Tag>::lend() without ExecCtx is only valid "
                                                   "for permission_row<Tag> == Row<>.  Effectful permission tags "
                                                   "must use lend(ctx).");
        return lend_raw_();
    }

    template <::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<Tag, Ctx>
    [[nodiscard]] std::optional<SharedPermissionGuard<Tag>> lend(Ctx const&) noexcept {
        return lend_raw_();
    }

    // The mode transition has to be indivisible.  Reading a plain count
    // of zero and then taking the parked permission loses to a lend that
    // increments in between, leaving two holders of one region.  Folding
    // the count and the upgraded-out flag into one word makes the test
    // and the claim a single compare-exchange: a lend that was about to
    // succeed retries and then fails on the flag, and a lend that
    // already incremented makes this compare-exchange fail.
    [[nodiscard]] std::optional<Permission<Tag>> try_upgrade() noexcept {
        static_assert(permission_row_empty_v<Tag>, "SharedPermissionPool<Tag>::try_upgrade() without ExecCtx is only "
                                                   "valid for permission_row<Tag> == Row<>.  Effectful permission "
                                                   "tags must use try_upgrade(ctx).");
        return try_upgrade_raw_();
    }

    template <::crucible::effects::IsExecCtx Ctx>
        requires CtxAdmitsPermission<Tag, Ctx>
    [[nodiscard]] std::optional<Permission<Tag>> try_upgrade(Ctx const&) noexcept {
        return try_upgrade_raw_();
    }

    void deposit_exclusive(Permission<Tag>&& exc) noexcept pre(!parked_.has_value()) {
        parked_ = std::move(exc);
        // The count is zero whenever the exclusive-out bit is set, so a
        // plain store of zero clears the bit without discarding a count.
        // Release, so a later lend sees the freshly parked permission.
        state_.store(0, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t outstanding() const noexcept {
        return state_.load(std::memory_order_acquire) & COUNT_MASK;
    }

    [[nodiscard]] bool is_exclusive_out() const noexcept {
        return (state_.load(std::memory_order_acquire) & EXCLUSIVE_OUT_BIT) != 0;
    }

private:
    [[nodiscard]] std::optional<SharedPermissionGuard<Tag>> lend_raw_() noexcept {
        std::uint64_t observed = state_.load(std::memory_order_acquire);
        for (;;) {
            if (observed & EXCLUSIVE_OUT_BIT) [[unlikely]] {
                return std::nullopt;
            }
            if ((observed & COUNT_MASK) == COUNT_MASK) [[unlikely]] {
                shared_permission_pool_saturated_abort_();
            }
            const std::uint64_t desired = observed + std::uint64_t{1};
            // Acquire-release, so this count update synchronizes with
            // the compare-exchange in try_upgrade.
            if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return SharedPermissionGuard<Tag>{*this};
            }
        }
    }

    [[nodiscard]] std::optional<Permission<Tag>> try_upgrade_raw_() noexcept {
        std::uint64_t expected = 0;
        if (!state_.compare_exchange_strong(expected, EXCLUSIVE_OUT_BIT, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            return std::nullopt;
        }
        // The parked permission is populated whenever the exclusive-out
        // bit is clear, and the exchange above just moved that bit from
        // clear to set, so the dereference here cannot be empty.
        Permission<Tag> exc = std::move(*parked_);
        parked_.reset();
        return exc;
    }

    friend class SharedPermissionGuard<Tag>;

    // Holds a value exactly when the exclusive-out bit is clear.
    std::optional<Permission<Tag>> parked_;

    alignas(64) std::atomic<std::uint64_t> state_;
};

// Acquire-release, so the compare-exchange in try_upgrade observes this
// decrement.
template <typename Tag>
inline SharedPermissionGuard<Tag>::~SharedPermissionGuard() {
    if (pool_ != nullptr) {
        pool_->state_.fetch_sub(std::uint64_t{1}, std::memory_order_acq_rel);
    }
}

// Converts an exclusive permission into an untracked share.  With no
// pool involved nothing counts the copies, so the exclusive can never be
// recovered.  This fits a one-shot share to a task that cannot outlive
// the caller.
template <typename Tag>
[[nodiscard]] constexpr SharedPermission<Tag> mint_permission_share(Permission<Tag>&& exc) noexcept {
    static_assert(permission_row_empty_v<Tag>, "mint_permission_share(Permission<Tag>&&) without ExecCtx is only "
                                               "valid for permission_row<Tag> == Row<>.  Effectful permission tags "
                                               "must use mint_permission_share(ctx, Permission<Tag>&&).");
    (void)exc;
    return SharedPermission<Tag>{};
}

template <typename Tag, ::crucible::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr SharedPermission<Tag> mint_permission_share(Ctx const&, Permission<Tag>&& exc) noexcept {
    (void)exc;
    return SharedPermission<Tag>{};
}

template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
[[nodiscard]] auto with_shared_read(SharedPermissionPool<Tag>& pool,
                                    Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
    -> std::optional<std::invoke_result_t<Body, SharedPermission<Tag>>>
    requires(!std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>)
{
    auto guard_opt = pool.lend();
    if (!guard_opt) return std::nullopt;
    return std::optional{std::forward<Body>(body)(guard_opt->token())};
}

template <typename Tag, ::crucible::effects::IsExecCtx Ctx, typename Body>
    requires CtxAdmitsPermission<Tag, Ctx>
          && std::is_invocable_v<Body, SharedPermission<Tag>>
             [[nodiscard]] auto
             with_shared_read(Ctx const& ctx, SharedPermissionPool<Tag>& pool,
                              Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
                 -> std::optional<std::invoke_result_t<Body, SharedPermission<Tag>>>
                 requires(!std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>)
{
    auto guard_opt = pool.lend(ctx);
    if (!guard_opt) return std::nullopt;
    return std::optional{std::forward<Body>(body)(guard_opt->token())};
}

// The concepts below carry an Is prefix because a concept and a class
// share one namespace lookup table, so a `concept Permission` would
// shadow the class of that name.

namespace detail {

template <typename T>
struct is_permission_impl : std::false_type {};

template <typename Tag>
struct is_permission_impl<Permission<Tag>> : std::true_type {
    using tag_type = Tag;
};

template <typename T>
struct is_shared_permission_impl : std::false_type {};

template <typename Tag>
struct is_shared_permission_impl<SharedPermission<Tag>> : std::true_type {
    using tag_type = Tag;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_permission_v = detail::is_permission_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_shared_permission_v = detail::is_shared_permission_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsPermission = is_permission_v<T>;

template <typename T>
concept IsSharedPermission = is_shared_permission_v<T>;

template <typename T, typename Tag>
concept IsPermissionFor =
    IsPermission<T> && std::is_same_v<typename detail::is_permission_impl<std::remove_cvref_t<T>>::tag_type, Tag>;

template <typename T, typename Tag>
concept IsSharedPermissionFor =
    IsSharedPermission<T>
    && std::is_same_v<typename detail::is_shared_permission_impl<std::remove_cvref_t<T>>::tag_type, Tag>;

// A separate overload for a void-returning body, because there is no
// optional<void> to carry the lend-failed case.  The bool says whether
// the body ran.
template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
          && std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>
bool with_shared_read(SharedPermissionPool<Tag>& pool,
                      Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>) {
    auto guard_opt = pool.lend();
    if (!guard_opt) return false;
    std::forward<Body>(body)(guard_opt->token());
    return true;
}

template <typename Tag, ::crucible::effects::IsExecCtx Ctx, typename Body>
    requires CtxAdmitsPermission<Tag, Ctx> && std::is_invocable_v<Body, SharedPermission<Tag>>
          && std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>
bool with_shared_read(Ctx const& ctx, SharedPermissionPool<Tag>& pool,
                      Body&& body) noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>) {
    auto guard_opt = pool.lend(ctx);
    if (!guard_opt) return false;
    std::forward<Body>(body)(guard_opt->token());
    return true;
}

namespace detail {
struct seplog_test_tag {};
struct seplog_test_left {};
struct seplog_test_right {};
struct seplog_io_tag {};
struct seplog_block_tag {};
struct seplog_multi_effect_tag {};
}  // namespace detail

template <>
struct permission_row<detail::seplog_io_tag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};

template <>
struct permission_row<detail::seplog_block_tag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::Block>;
};

template <>
struct permission_row<detail::seplog_multi_effect_tag> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
};

static_assert(permission_row_empty_v<detail::seplog_test_tag>);
static_assert(!permission_row_empty_v<detail::seplog_io_tag>);
static_assert(CtxAdmitsPermission<detail::seplog_io_tag, ::crucible::effects::BgCompileCtx>);
static_assert(!CtxAdmitsPermission<detail::seplog_io_tag, ::crucible::effects::HotFgCtx>);
static_assert(!CtxAdmitsPermission<detail::seplog_block_tag, ::crucible::effects::BgCompileCtx>);
static_assert(CtxAdmitsPermission<detail::seplog_multi_effect_tag, ::crucible::effects::TestRunnerCtx>);
static_assert(!CtxAdmitsPermission<detail::seplog_multi_effect_tag, ::crucible::effects::BgCompileCtx>);
static_assert(CtxAdmitsPermission<::crucible::permissions::tag::GpuMemoryTag, ::crucible::effects::BgDrainCtx>);
static_assert(
    !CtxAdmitsPermission<::crucible::permissions::tag::DiskSpilledRegionTag, ::crucible::effects::BgCompileCtx>);
static_assert(CtxAdmitsPermission<::crucible::permissions::tag::MmapRegionTag, ::crucible::effects::BgCompileCtx>);
static_assert(!CtxAdmitsPermission<::crucible::permissions::tag::MmapRegionTag, ::crucible::effects::HotFgCtx>);
static_assert(CtxAdmitsPermission<::crucible::permissions::tag::NetworkBufferTag, ::crucible::effects::BgCompileCtx>);
static_assert(!CtxAdmitsPermission<::crucible::permissions::tag::NetworkBufferTag, ::crucible::effects::HotFgCtx>);

static_assert(all_distinct_tags_v<>);
static_assert(all_distinct_tags_v<detail::seplog_test_left>);
static_assert(all_distinct_tags_v<detail::seplog_test_left, detail::seplog_test_right>);
static_assert(all_distinct_tags_v<detail::seplog_test_tag, detail::seplog_test_left, detail::seplog_test_right>);
static_assert(!all_distinct_tags_v<detail::seplog_test_left, detail::seplog_test_left>);
static_assert(!all_distinct_tags_v<detail::seplog_test_tag, detail::seplog_test_left, detail::seplog_test_tag>);
static_assert(!all_distinct_tags_v<detail::seplog_test_left, detail::seplog_test_left, detail::seplog_test_right>);

static_assert(sizeof(Permission<detail::seplog_test_tag>) == 1, "Permission<Tag> must be a 1-byte empty class");
static_assert(std::is_trivially_destructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> destructor must be trivial");
static_assert(!std::is_copy_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must NOT be copy-constructible (linear)");
static_assert(!std::is_copy_assignable_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must NOT be copy-assignable (linear)");
static_assert(std::is_move_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must be move-constructible (handoff)");
static_assert(std::is_nothrow_move_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> moves must be noexcept");

static_assert(sizeof(SharedPermission<detail::seplog_test_tag>) == 1,
              "SharedPermission<Tag> must be a 1-byte empty class");
static_assert(std::is_copy_constructible_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> MUST be copy-constructible (fractional)");
static_assert(std::is_copy_assignable_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> MUST be copy-assignable (fractional)");
static_assert(std::is_trivially_copyable_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> must be trivially-copyable (zero-cost copy)");
static_assert(std::is_trivially_destructible_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> destructor must be trivial");
static_assert(SharedPermission<detail::seplog_test_tag>::confers_runtime_access == false,
              "SharedPermission<Tag> must confer NO runtime access");

static_assert(!std::is_copy_constructible_v<SharedPermissionGuard<detail::seplog_test_tag>>,
              "SharedPermissionGuard<Tag> must NOT be copy-constructible");
static_assert(std::is_move_constructible_v<SharedPermissionGuard<detail::seplog_test_tag>>,
              "SharedPermissionGuard<Tag> must be move-constructible");
static_assert(sizeof(SharedPermissionGuard<detail::seplog_test_tag>) == sizeof(void*),
              "SharedPermissionGuard<Tag> must be exactly one pointer (the Pool*)");

static_assert(!std::is_copy_constructible_v<SharedPermissionPool<detail::seplog_test_tag>>,
              "SharedPermissionPool<Tag> must be Pinned (non-copyable)");
static_assert(!std::is_move_constructible_v<SharedPermissionPool<detail::seplog_test_tag>>,
              "SharedPermissionPool<Tag> must be Pinned (non-movable)");

static_assert(is_permission_v<Permission<detail::seplog_test_tag>>);
static_assert(is_permission_v<Permission<detail::seplog_test_tag>&&>);
static_assert(is_permission_v<const Permission<detail::seplog_test_tag>&>);
static_assert(!is_permission_v<int>);
static_assert(!is_permission_v<SharedPermission<detail::seplog_test_tag>>);
static_assert(!is_permission_v<SharedPermissionGuard<detail::seplog_test_tag>>);

static_assert(is_shared_permission_v<SharedPermission<detail::seplog_test_tag>>);
static_assert(is_shared_permission_v<const SharedPermission<detail::seplog_test_tag>&>);
static_assert(!is_shared_permission_v<int>);
static_assert(!is_shared_permission_v<Permission<detail::seplog_test_tag>>);
static_assert(!is_shared_permission_v<SharedPermissionGuard<detail::seplog_test_tag>>);

static_assert(IsPermission<Permission<detail::seplog_test_tag>>);
static_assert(IsSharedPermission<SharedPermission<detail::seplog_test_tag>>);
static_assert(IsPermissionFor<Permission<detail::seplog_test_tag>, detail::seplog_test_tag>);
static_assert(!IsPermissionFor<Permission<detail::seplog_test_tag>, detail::seplog_test_left>);
static_assert(IsSharedPermissionFor<SharedPermission<detail::seplog_test_tag>, detail::seplog_test_tag>);
static_assert(!IsSharedPermissionFor<SharedPermission<detail::seplog_test_tag>, detail::seplog_test_left>);

namespace detail {
struct seplog_combine_n_parent {};
struct seplog_combine_n_a {};
struct seplog_combine_n_b {};
struct seplog_combine_n_c {};
}  // namespace detail

template <>
struct splits_into_pack<detail::seplog_combine_n_parent, detail::seplog_combine_n_a, detail::seplog_combine_n_b,
                        detail::seplog_combine_n_c> : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<detail::seplog_combine_n_parent, detail::seplog_combine_n_a,
                                          detail::seplog_combine_n_b, detail::seplog_combine_n_c> : std::true_type {};

namespace detail {
constexpr bool combine_n_round_trip() noexcept {
    auto whole = mint_permission_root<seplog_combine_n_parent>();
    auto [a, b, c] =
        mint_permission_split_n<seplog_combine_n_a, seplog_combine_n_b, seplog_combine_n_c>(std::move(whole));
    auto rebuilt = mint_permission_combine_n<seplog_combine_n_parent>(std::move(a), std::move(b), std::move(c));
    (void)rebuilt;
    return true;
}
static_assert(combine_n_round_trip());
}  // namespace detail

}  // namespace crucible::safety
