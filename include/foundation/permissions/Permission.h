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
//
// Linearity is the deleted copy and the move-only transfer, and nothing
// more: a token consumed twice after std::move is a use-after-move that
// the compiler does not diagnose (task #34, decided with fixy/Qtt.h).
// A Debug-only consumed byte was rejected there because production
// layout pins on the empty token are Debug-independent.
//
// Every mint below is one template.  A call may lead with an execution
// context, or with two for the asymmetric split, and the permissions
// come last; the leading contexts are counted off the argument list,
// and the fit of each tag to each context is one concept on the
// template head.  The old header spelled each mint as a token overload
// and a context overload that repeated the same checks and body, and
// befriended both.
//
// Old spelling: include/crucible/permissions/Permission.h, namespaces
// crucible::safety and crucible::permissions::tag.  The friends that
// reached the private constructor from the inheritance and federation
// headers are not carried, because neither header is ported and a
// friend naming an absent type is an open door.

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Ctx.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <meta>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::permissions {

// An unconstrained tag fails silently rather than loudly: a
// `Permission<Tag*>` compiles, but the splits_into specialization
// written for `Tag` never matches it and the pool instantiates under a
// different key, so every discipline check quietly stops applying.

template <typename T>
concept PermissionTag = std::is_class_v<T> && std::is_empty_v<T>;

template <typename Tag>
class Permission;

namespace tag {

struct DiskSpilledRegionTag {};
struct HugePageTag {};
struct MmapRegionTag {};
struct GpuMemoryTag {};
struct NetworkBufferTag {};

}  // namespace tag

template <typename Tag>
class SharedPermission;
template <typename Tag>
class SharedPermissionGuard;
template <typename Tag>
class SharedPermissionPool;

namespace detail {
class ForkRebuildKey;
struct ForkRebuildAccess;

// A friend declaration of a constrained function template must repeat
// the constraint exactly, so the public mints cannot be friended here
// without dragging their whole requires-clauses, and every type those
// clauses name, into this header.  permission_fork_ is the body those
// mints delegate to and carries no constraint of its own, so it is the
// one join primitive this header can name.  It is defined in
// PermissionFork.h and is the only holder of the rebuild key.
template <bool Spawn, typename... Children, typename Ctx, typename Parent, typename... Callables>
constexpr Permission<Parent> permission_fork_(Ctx const& ctx, Permission<Parent>&& parent,
                                              Callables&&... callables) noexcept;
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
    using type = ::foundation::effects::Row<>;
};

template <typename Tag>
using permission_row_t = typename permission_row<Tag>::type;

template <typename Tag>
inline constexpr bool permission_row_empty_v = ::foundation::effects::row_size_v<permission_row_t<Tag>> == 0;

template <typename Tag, typename Ctx>
concept CtxAdmitsPermission = ::foundation::effects::IsExecCtx<Ctx>
                           && ::foundation::effects::is_subrow_v<permission_row_t<Tag>, typename Ctx::row_type>;

template <>
struct permission_row<::foundation::permissions::tag::DiskSpilledRegionTag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};

template <>
struct permission_row<::foundation::permissions::tag::HugePageTag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

template <>
struct permission_row<::foundation::permissions::tag::MmapRegionTag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

template <>
struct permission_row<::foundation::permissions::tag::GpuMemoryTag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::Alloc>;
};

template <>
struct permission_row<::foundation::permissions::tag::NetworkBufferTag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

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

// The shape of a mint's argument list: zero, one or two execution
// contexts, then the permissions.  A context argument is anything the
// context trait recognizes after the reference is stripped; a
// permission argument is a Permission passed as an rvalue, because
// every mint consumes the tokens it is given.  The count of leading
// contexts is what each mint's fit concept dispatches on.

namespace detail {

template <typename A>
concept CtxArg = ::foundation::effects::IsExecCtx<A>;

template <typename A>
concept PermArg = !std::is_lvalue_reference_v<A> && is_permission_v<A>;

template <typename A>
using perm_tag_t = typename is_permission_impl<std::remove_cvref_t<A>>::tag_type;

template <typename... Args>
[[nodiscard]] consteval std::size_t leading_ctx_count() noexcept {
    std::size_t count = 0;
    bool still_ctx = true;
    ((still_ctx = still_ctx && CtxArg<Args>, count += still_ctx ? 1 : 0), ...);
    return count;
}

// True when Args is Ctxs..., Perms... with at most MaxCtx contexts and
// exactly NPerms permissions, or any number of permissions when NPerms
// is zero.
template <std::size_t MaxCtx, std::size_t NPerms, typename... Args>
[[nodiscard]] consteval bool mint_args_shaped_() noexcept {
    constexpr std::size_t n_ctx = leading_ctx_count<Args...>();
    if (n_ctx > MaxCtx) return false;
    constexpr std::size_t n_perm = sizeof...(Args) - n_ctx;
    if (NPerms != 0 && n_perm != NPerms) return false;
    if (n_perm == 0) return false;
    bool perms = true;
    std::size_t index = 0;
    ((perms = perms && (index < n_ctx || PermArg<Args>), ++index), ...);
    return perms;
}

template <std::size_t MaxCtx, std::size_t NPerms, typename... Args>
concept MintArgs = mint_args_shaped_<MaxCtx, NPerms, Args...>();

// The permission tags of Args past the leading contexts, as a tuple of
// tags, and the row check every ctx-bound mint shares: each named tag
// is admitted by the one context.
template <typename... Args, std::size_t... Is>
consteval auto perm_tags_(std::index_sequence<Is...>) noexcept
    -> std::tuple<perm_tag_t<Args...[Is + leading_ctx_count<Args...>()]>...>;

template <typename... Args>
using perm_tags_t =
    decltype(perm_tags_<Args...>(std::make_index_sequence<sizeof...(Args) - leading_ctx_count<Args...>()>{}));

template <typename Ctx, typename... Tags>
inline constexpr bool ctx_admits_all_v = (CtxAdmitsPermission<Tags, std::remove_cvref_t<Ctx>> && ...);

template <typename Ctx, typename TagTuple>
struct ctx_admits_tuple;

template <typename Ctx, typename... Tags>
struct ctx_admits_tuple<Ctx, std::tuple<Tags...>> : std::bool_constant<ctx_admits_all_v<Ctx, Tags...>> {};

template <typename Ctx, typename TagTuple>
inline constexpr bool ctx_admits_tuple_v = ctx_admits_tuple<Ctx, TagTuple>::value;

}  // namespace detail

// The fit of one mint call.  With no context the call is the token
// form, whose row requirement is a static_assert in the body so the
// message names the ctx-bound spelling to use instead.  With one
// context every tag the call names, parents and children alike, must be
// admitted by it.  The split alone also takes two contexts, one per
// child, for a parent that is torn across two scopes.

template <typename Tag, typename... Args>
concept PermissionRootArgs =
    (sizeof...(Args) == 0) || (sizeof...(Args) == 1 && (detail::ctx_admits_all_v<Args, Tag> && ...));

template <typename L, typename R, typename... Args>
concept PermissionSplitArgs = detail::MintArgs<2, 1, Args...>
                           && (detail::leading_ctx_count<Args...>() == 0
                               || (detail::leading_ctx_count<Args...>() == 1
                                   && detail::ctx_admits_all_v<Args...[0], detail::perm_tag_t<Args...[1]>, L, R>)
                               || (detail::leading_ctx_count<Args...>() == 2 && detail::ctx_admits_all_v<Args...[0], L>
                                   && detail::ctx_admits_all_v<Args...[1], R>));

template <typename In, typename... Args>
concept PermissionCombineArgs =
    detail::MintArgs<1, 2, Args...>
    && (detail::leading_ctx_count<Args...>() == 0
        || detail::ctx_admits_all_v<Args...[0], In, detail::perm_tag_t<Args...[1]>, detail::perm_tag_t<Args...[2]>>);

template <typename ChildrenTuple, typename... Args>
concept PermissionSplitNArgs = detail::MintArgs<1, 1, Args...>
                            && (detail::leading_ctx_count<Args...>() == 0
                                || (detail::ctx_admits_all_v<Args...[0], detail::perm_tag_t<Args...[1]>>
                                    && detail::ctx_admits_tuple_v<Args...[0], ChildrenTuple>));

template <typename Parent, typename... Args>
concept PermissionCombineNArgs = detail::MintArgs<1, 0, Args...>
                              && (detail::leading_ctx_count<Args...>() == 0
                                  || (detail::ctx_admits_all_v<Args...[0], Parent>
                                      && detail::ctx_admits_tuple_v<Args...[0], detail::perm_tags_t<Args...>>));

template <typename... Args>
concept PermissionShareArgs = detail::MintArgs<1, 1, Args...>
                           && (detail::leading_ctx_count<Args...>() == 0
                               || detail::ctx_admits_all_v<Args...[0], detail::perm_tag_t<Args...[1]>>);

template <typename Tag, typename... Args>
    requires PermissionRootArgs<Tag, Args...>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root(Args const&...) noexcept;

template <typename L, typename R, typename... Args>
    requires PermissionSplitArgs<L, R, Args...>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>> mint_permission_split(Args&&...) noexcept;

template <typename In, typename... Args>
    requires PermissionCombineArgs<In, Args...>
[[nodiscard]] constexpr Permission<In> mint_permission_combine(Args&&...) noexcept;

template <typename... Children, typename... Args>
    requires PermissionSplitNArgs<std::tuple<Children...>, Args...>
[[nodiscard]] constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Args&&...) noexcept;

template <typename Parent, typename... Args>
    requires PermissionCombineNArgs<Parent, Args...>
[[nodiscard]] constexpr Permission<Parent> mint_permission_combine_n(Args&&...) noexcept;

// The friendship that gates construction lives on this key rather than
// inside Permission, for the reason Capability.h gives for cap_mint_key.
// A friend declaration of a constrained function template must repeat
// the constraint exactly.  An edit to a mint's requires-clause that is
// not mirrored resolves the friendship to a different overload, and
// every minting site then fails with a private-member error far from
// the cause.  Five mints make that hazard five times over.
//
// The repetition does not go away, because the language requires the
// constraint on a friend declaration to match.  What changes is where
// the five declarations sit: in one class whose only purpose is to hold
// them, in front of whoever edits a requires-clause, rather than in the
// middle of Permission's class body.  Permission also stops handing the
// mints access to everything else it declares.
//
// The key must not move into a nested namespace.  A templated friend
// declaration introduces a new declaration into the innermost enclosing
// namespace of the befriending class when no matching declaration is
// already visible there, so a key inside a detail namespace would
// befriend a fresh detail-scope mint and silently open the gate.
class perm_mint_key {
    constexpr perm_mint_key() noexcept = default;

    // Every entry below is another way to forge authority over a
    // region.  Additions need review.  Each mint is one template, so
    // each is one entry, whether it is called with a context or
    // without.

    template <typename Tag, typename... Args>
        requires PermissionRootArgs<Tag, Args...>
    friend constexpr Permission<Tag> mint_permission_root(Args const&...) noexcept;

    template <typename L, typename R, typename... Args>
        requires PermissionSplitArgs<L, R, Args...>
    friend constexpr std::pair<Permission<L>, Permission<R>> mint_permission_split(Args&&...) noexcept;

    template <typename In, typename... Args>
        requires PermissionCombineArgs<In, Args...>
    friend constexpr Permission<In> mint_permission_combine(Args&&...) noexcept;

    template <typename... Children, typename... Args>
        requires PermissionSplitNArgs<std::tuple<Children...>, Args...>
    friend constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Args&&...) noexcept;

    template <typename Parent, typename... Args>
        requires PermissionCombineNArgs<Parent, Args...>
    friend constexpr Permission<Parent> mint_permission_combine_n(Args&&...) noexcept;

    // The soundness gate on the post-join rebuild is its own passkey,
    // not this friendship, which only reaches the key.
    friend struct ::foundation::permissions::detail::ForkRebuildAccess;
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

public:
    using tag_type = Tag;

    // Holding the key is the proof of authority, and only the five
    // mints and the post-join rebuild can make one, so this is the sole
    // route to a Permission.  Permission itself befriends nobody.
    explicit constexpr Permission(perm_mint_key) noexcept {}

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
template <typename Tag, typename... Args>
    requires PermissionRootArgs<Tag, Args...>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root(Args const&...) noexcept {
    static_assert(sizeof...(Args) == 1 || permission_row_empty_v<Tag>,
                  "mint_permission_root<Tag>() without an ExecCtx is only valid for "
                  "permission_row<Tag> == Row<>.  Effectful permission tags must be "
                  "minted with mint_permission_root<Tag>(ctx) so Ctx admits the tag's row.");
    return Permission<Tag>{perm_mint_key{}};
}

template <typename Tag, ::foundation::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag> admit_permission(Ctx const&, Permission<Tag>&& perm) noexcept {
    return std::move(perm);
}

template <typename Tag, ::foundation::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag> permission_handoff(Ctx const& ctx, Permission<Tag>&& perm) noexcept {
    return admit_permission(ctx, std::move(perm));
}

template <typename L, typename R, typename... Args>
    requires PermissionSplitArgs<L, R, Args...>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>> mint_permission_split(Args&&...) noexcept {
    using In = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    static_assert(detail::leading_ctx_count<Args...>() != 0
                      || (permission_row_empty_v<In> && permission_row_empty_v<L> && permission_row_empty_v<R>),
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
    return std::pair<Permission<L>, Permission<R>>{Permission<L>{perm_mint_key{}}, Permission<R>{perm_mint_key{}}};
}

template <typename In, typename... Args>
    requires PermissionCombineArgs<In, Args...>
[[nodiscard]] constexpr Permission<In> mint_permission_combine(Args&&...) noexcept {
    using L = detail::perm_tag_t<Args...[sizeof...(Args) - 2]>;
    using R = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    static_assert(detail::leading_ctx_count<Args...>() != 0
                      || (permission_row_empty_v<In> && permission_row_empty_v<L> && permission_row_empty_v<R>),
                  "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                  "without ExecCtx is only valid for Row<> permission tags.");
    static_assert(splits_into_v<In, L, R>, "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                                           "requires splits_into<In, L, R>::value true.");
    static_assert(splits_into_authoring_witness_v<In, L, R>, "splits_into_authoring_witness<In, L, R> missing for "
                                                             "combine; declare it next to the splits_into "
                                                             "specialization.");
    return Permission<In>{perm_mint_key{}};
}

template <typename... Children, typename... Args>
    requires PermissionSplitNArgs<std::tuple<Children...>, Args...>
[[nodiscard]] constexpr std::tuple<Permission<Children>...> mint_permission_split_n(Args&&...) noexcept {
    using In = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    static_assert(detail::leading_ctx_count<Args...>() != 0
                      || (permission_row_empty_v<In> && (permission_row_empty_v<Children> && ...)),
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
    return std::tuple<Permission<Children>...>{Permission<Children>{perm_mint_key{}}...};
}

namespace detail {

template <typename Parent, typename TagTuple>
struct combine_n_manifest;

template <typename Parent, typename... Children>
struct combine_n_manifest<Parent, std::tuple<Children...>> {
    static constexpr bool rows_empty = permission_row_empty_v<Parent> && (permission_row_empty_v<Children> && ...);
    static constexpr bool declared = splits_into_pack_v<Parent, Children...>;
    static constexpr bool witnessed = splits_into_pack_authoring_witness_v<Parent, Children...>;
    static constexpr bool distinct = all_distinct_tags_v<Children...>;
};

}  // namespace detail

template <typename Parent, typename... Args>
    requires PermissionCombineNArgs<Parent, Args...>
[[nodiscard]] constexpr Permission<Parent> mint_permission_combine_n(Args&&...) noexcept {
    using manifest = detail::combine_n_manifest<Parent, detail::perm_tags_t<Args...>>;
    static_assert(detail::leading_ctx_count<Args...>() != 0 || manifest::rows_empty,
                  "mint_permission_combine_n<Parent, Children...>(...) without ExecCtx "
                  "is only valid when every permission row is Row<>.");
    static_assert(manifest::declared, "mint_permission_combine_n<Parent, Children...>("
                                      "Permission<Children>&&...) requires "
                                      "splits_into_pack<Parent, Children...>::value true.  "
                                      "The combine call must mirror the prior split_n; "
                                      "declare the manifest in the same TU as the tags.");
    static_assert(manifest::witnessed, "splits_into_pack_authoring_witness<Parent, Children...> "
                                       "missing for combine_n; declare next to the "
                                       "splits_into_pack specialization.");
    static_assert(manifest::distinct, "mint_permission_combine_n<Parent, Children...> "
                                      "requires the child tags to be PAIRWISE DISTINCT — folding "
                                      "two Permission<A> back into one parent would require two "
                                      "aliasing tokens to have existed.");
    return Permission<Parent>{perm_mint_key{}};
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

    // permission_fork_ is the sole friend, and that friendship is the
    // whole gate.  It is reached only through mint_permission_fork or
    // its inline sibling, each of which takes the parent Permission by
    // rvalue and consumes it at the split.  A caller holding the key has
    // therefore already surrendered the very permission the rebuild
    // hands back.
    //
    // Until the fix for #169 the friend was a nullary free function
    // template at namespace scope, `rebuild_parent_after_fork_`.  It
    // took no argument, carried no constraint, and was itself friended
    // to build the key, so the chain was a closed loop whose entry point
    // any translation unit could call:
    // `detail::rebuild_parent_after_fork_<AnyTag>()` minted a Permission
    // for a tag the caller did not own, with no manifest, no context and
    // no token.  Keep this friend a function that CONSUMES a
    // Permission<Parent>.  A friend that takes nothing proves nothing.
    template <bool USpawn, typename... UChildren, typename UCtx, typename UParent, typename... UCallables>
    friend constexpr Permission<UParent> permission_fork_(UCtx const&, Permission<UParent>&&, UCallables&&...) noexcept;
};

struct ForkRebuildAccess {
    // rebuild carries no constraint on T because the proof lives in the
    // key rather than here.  The only holder of a key is
    // permission_fork_, which reached this point by consuming a
    // Permission<Parent> at the split.  Constraining T here would
    // restate that proof at a point which cannot see the children the
    // parent was split into.
    //
    // That sentence holds only while the key's friend list names one
    // function that consumes a parent permission.  It was false before
    // the fix for #169, when the friend took no argument at all.
    template <typename T>
    [[nodiscard]] static constexpr Permission<T> rebuild(ForkRebuildKey) noexcept {
        return Permission<T>{perm_mint_key{}};
    }
};

}  // namespace detail

// The access check here is genuine because this scope is befriended by
// neither key.  Both assertions fail if the constructor they name
// becomes public, and both are the only thing that would report it.
//
// ForkRebuildKey is the higher-stakes of the two: ForkRebuildAccess is a
// public struct whose rebuild<T> is public and unconstrained, so this
// private constructor is the whole of what stands between a caller and
// a Permission for an arbitrary tag.  It had no assertion at all until
// this one.
static_assert(!std::is_default_constructible_v<detail::ForkRebuildKey>,
              "The default constructor of ForkRebuildKey must not be public.  Only permission_fork_ "
              "is friended to build one, and that friendship is the whole gate on "
              "ForkRebuildAccess::rebuild.");
static_assert(std::is_empty_v<detail::ForkRebuildKey>, "ForkRebuildKey must stay empty, so that passing it "
                                                       "costs nothing.");

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

template <typename... Args>
    requires PermissionShareArgs<Args...>
[[nodiscard]] constexpr SharedPermission<detail::perm_tag_t<Args...[sizeof...(Args) - 1]>>
mint_permission_share(Args&&...) noexcept;

template <typename Tag>
class [[nodiscard]] SharedPermission {
    constexpr SharedPermission() noexcept = default;

    template <typename T>
    friend class SharedPermissionGuard;

    template <typename... Args>
        requires PermissionShareArgs<Args...>
    friend constexpr SharedPermission<detail::perm_tag_t<Args...[sizeof...(Args) - 1]>>
    mint_permission_share(Args&&...) noexcept;

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
    using lattice_type = ::foundation::algebra::lattices::FractionalLattice;
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    using graded_type = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, lattice_type, Tag>;

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
    using Tag = ::foundation::diag::SharedPermissionPoolSaturated;
    std::fprintf(stderr,
                 "crucible: fatal contract violation: %.*s\n"
                 "  description: %.*s\n"
                 "  remediation: %.*s\n",
                 static_cast<int>(Tag::name.size()), Tag::name.data(), static_cast<int>(Tag::description.size()),
                 Tag::description.data(), static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
    std::abort();
}

// The context a pool operation may run in: none, which is the token
// form and asks the tag's row to be empty in the body, or one that
// admits the tag's row.
namespace detail {
struct no_ctx {};
}  // namespace detail

template <typename Tag, typename Ctx>
concept PoolCtx = std::same_as<Ctx, detail::no_ctx> || CtxAdmitsPermission<Tag, Ctx>;

template <typename Tag>
class SharedPermissionPool : public ::foundation::Pinned<SharedPermissionPool<Tag>> {
public:
    using tag_type = Tag;

    static constexpr std::uint64_t EXCLUSIVE_OUT_BIT = std::uint64_t{1} << 63;
    static constexpr std::uint64_t COUNT_MASK = EXCLUSIVE_OUT_BIT - std::uint64_t{1};

    constexpr explicit SharedPermissionPool(Permission<Tag>&& exc) noexcept : parked_{std::move(exc)}, state_{0} {}

    template <typename Ctx = detail::no_ctx>
        requires PoolCtx<Tag, Ctx>
    [[nodiscard]] std::optional<SharedPermissionGuard<Tag>> lend(Ctx const& = {}) noexcept {
        static_assert(!std::same_as<Ctx, detail::no_ctx> || permission_row_empty_v<Tag>,
                      "SharedPermissionPool<Tag>::lend() without ExecCtx is only valid "
                      "for permission_row<Tag> == Row<>.  Effectful permission tags "
                      "must use lend(ctx).");
        return lend_raw_();
    }

    // The mode transition has to be indivisible.  Reading a plain count
    // of zero and then taking the parked permission loses to a lend that
    // increments in between, leaving two holders of one region.  Folding
    // the count and the upgraded-out flag into one word makes the test
    // and the claim a single compare-exchange: a lend that was about to
    // succeed retries and then fails on the flag, and a lend that
    // already incremented makes this compare-exchange fail.
    template <typename Ctx = detail::no_ctx>
        requires PoolCtx<Tag, Ctx>
    [[nodiscard]] std::optional<Permission<Tag>> try_upgrade(Ctx const& = {}) noexcept {
        static_assert(!std::same_as<Ctx, detail::no_ctx> || permission_row_empty_v<Tag>,
                      "SharedPermissionPool<Tag>::try_upgrade() without ExecCtx is only "
                      "valid for permission_row<Tag> == Row<>.  Effectful permission "
                      "tags must use try_upgrade(ctx).");
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
template <typename... Args>
    requires PermissionShareArgs<Args...>
[[nodiscard]] constexpr SharedPermission<detail::perm_tag_t<Args... [sizeof...(Args) - 1]>>
mint_permission_share(Args&&...) noexcept {
    using Tag = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    static_assert(detail::leading_ctx_count<Args...>() != 0 || permission_row_empty_v<Tag>,
                  "mint_permission_share(Permission<Tag>&&) without ExecCtx is only "
                  "valid for permission_row<Tag> == Row<>.  Effectful permission tags "
                  "must use mint_permission_share(ctx, Permission<Tag>&&).");
    return SharedPermission<Tag>{};
}

// Runs the body under a lent share.  A body that returns a value comes
// back in an optional, empty when the lend failed.  A separate
// declaration serves a void-returning body, because there is no
// optional<void> to carry the lend-failed case: the bool says whether
// the body ran, and that declaration carries no [[nodiscard]], as its
// two predecessors did not.  The call leads with a context or with the
// pool, and the body is last; one shared body serves both.

namespace detail {
    template <typename... Args>
    [[nodiscard]] consteval bool with_shared_read_shaped() noexcept {
        constexpr std::size_t n_ctx = leading_ctx_count<Args...>();
        if (n_ctx > 1 || sizeof...(Args) != n_ctx + 2) return false;
        using Pool = std::remove_cvref_t<Args...[n_ctx]>;
        if constexpr (requires { typename Pool::tag_type; }) {
            using Tag = typename Pool::tag_type;
            if (!std::is_same_v<Pool, SharedPermissionPool<Tag>>) return false;
            if (!std::is_lvalue_reference_v<Args...[n_ctx]>) return false;
            if (!std::is_invocable_v<Args...[n_ctx + 1], SharedPermission<Tag>>) return false;
            if constexpr (n_ctx == 1) {
                return CtxAdmitsPermission<Tag, std::remove_cvref_t<Args...[0]>>;
            } else {
                return true;
            }
        } else {
            return false;
        }
    }

    template <typename... Args>
    using shared_read_tag_t = typename std::remove_cvref_t<Args...[sizeof...(Args) - 2]>::tag_type;

    template <typename... Args>
    using shared_read_result_t =
        std::invoke_result_t<Args...[sizeof...(Args) - 1], SharedPermission<shared_read_tag_t<Args...>>>;

    template <typename... Args>
    inline constexpr bool shared_read_nothrow_v =
        std::is_nothrow_invocable_v<Args...[sizeof...(Args) - 1], SharedPermission<shared_read_tag_t<Args...>>>;

    template <typename... Args>
    auto with_shared_read_(Args && ... args) noexcept(shared_read_nothrow_v<Args...>) {
        using Tag = shared_read_tag_t<Args...>;
        using Body = Args...[sizeof...(Args) - 1];
        using Result = shared_read_result_t<Args...>;
        auto forwarded = std::forward_as_tuple(std::forward<Args>(args)...);
        SharedPermissionPool<Tag>& pool = std::get<sizeof...(Args) - 2>(forwarded);
        Body&& body = std::get<sizeof...(Args) - 1>(std::move(forwarded));
        auto guard_opt = [&] {
            if constexpr (leading_ctx_count<Args...>() == 1) {
                return pool.lend(std::get<0>(forwarded));
            } else {
                return pool.lend();
            }
        }();
        if constexpr (std::is_void_v<Result>) {
            if (!guard_opt) return false;
            std::forward<Body>(body)(guard_opt->token());
            return true;
        } else {
            if (!guard_opt) return std::optional<Result>{std::nullopt};
            return std::optional<Result>{std::forward<Body>(body)(guard_opt->token())};
        }
    }

}  // namespace detail

template <typename... Args>
concept WithSharedReadArgs = detail::with_shared_read_shaped<Args...>();

template <typename... Args>
    requires WithSharedReadArgs<Args...> && (!std::is_void_v<detail::shared_read_result_t<Args...>>)
[[nodiscard]] std::optional<detail::shared_read_result_t<Args...>>
with_shared_read(Args&&... args) noexcept(detail::shared_read_nothrow_v<Args...>) {
    return detail::with_shared_read_(std::forward<Args>(args)...);
}

template <typename... Args>
    requires WithSharedReadArgs<Args...> && std::is_void_v<detail::shared_read_result_t<Args...>>
bool with_shared_read(Args&&... args) noexcept(detail::shared_read_nothrow_v<Args...>) {
    return detail::with_shared_read_(std::forward<Args>(args)...);
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
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

template <>
struct permission_row<detail::seplog_block_tag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::Block>;
};

template <>
struct permission_row<detail::seplog_multi_effect_tag> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;
};

// The contexts here are the self-test witnesses of foundation/effects/Ctx.h,
// in the shape of the named contexts the layer above defines: a background
// drain row, the same with IO, the test-runner row, and the empty
// foreground row.
using seplog_bg_drain_ctx = ::foundation::effects::detail::exec_ctx_self_test::BgWitness;
using seplog_bg_compile_ctx = ::foundation::effects::detail::exec_ctx_self_test::BgIoWitness;
using seplog_test_runner_ctx = ::foundation::effects::detail::exec_ctx_self_test::TestWitnessCtx;
using seplog_hot_fg_ctx = ::foundation::effects::detail::exec_ctx_self_test::FgWitness;

static_assert(permission_row_empty_v<detail::seplog_test_tag>);
static_assert(!permission_row_empty_v<detail::seplog_io_tag>);
static_assert(CtxAdmitsPermission<detail::seplog_io_tag, seplog_bg_compile_ctx>);
static_assert(!CtxAdmitsPermission<detail::seplog_io_tag, seplog_hot_fg_ctx>);
static_assert(!CtxAdmitsPermission<detail::seplog_block_tag, seplog_bg_compile_ctx>);
static_assert(CtxAdmitsPermission<detail::seplog_multi_effect_tag, seplog_test_runner_ctx>);
static_assert(!CtxAdmitsPermission<detail::seplog_multi_effect_tag, seplog_bg_compile_ctx>);
static_assert(CtxAdmitsPermission<::foundation::permissions::tag::GpuMemoryTag, seplog_bg_drain_ctx>);
static_assert(!CtxAdmitsPermission<::foundation::permissions::tag::DiskSpilledRegionTag, seplog_bg_compile_ctx>);
static_assert(CtxAdmitsPermission<::foundation::permissions::tag::MmapRegionTag, seplog_bg_compile_ctx>);
static_assert(CtxAdmitsPermission<::foundation::permissions::tag::NetworkBufferTag, seplog_bg_compile_ctx>);

// Every canonical tag of the tag namespace is walked once: each is a
// permission tag with a non-empty row that the test runner admits and
// the foreground refuses, and its token has the layout and the
// linearity every token has.  A tag added to the namespace is checked
// by this walk without a new assertion.
namespace detail::seplog_roster {

template <typename Tag>
[[nodiscard]] consteval bool token_is_sound() noexcept {
    return sizeof(Permission<Tag>) == 1
        && std::is_trivially_destructible_v<Permission<Tag>> && !std::is_copy_constructible_v<Permission<Tag>>
        && !std::is_copy_assignable_v<Permission<Tag>> && std::is_move_constructible_v<Permission<Tag>>
        && std::is_nothrow_move_constructible_v<Permission<Tag>>
        // The key is the sole route in.  Both halves are load-bearing:
        // drop the first and a token is default-constructible by
        // anyone, drop the second and the mints cannot build one.
        && !std::is_default_constructible_v<Permission<Tag>>
        && std::is_constructible_v<Permission<Tag>, perm_mint_key>
        // Explicit, so that a copy of the key cannot convert itself
        // into a token without the construction being written out.
        && !std::is_convertible_v<perm_mint_key, Permission<Tag>>;
}

// A translation unit that holds no friendship cannot make a key, so it
// cannot reach the constructor above however it spells the call.
static_assert(!std::is_default_constructible_v<perm_mint_key>,
              "The default constructor of perm_mint_key must not be public.  Only the five mints and the "
              "post-join rebuild are friended to build one.");
static_assert(std::is_empty_v<perm_mint_key>, "perm_mint_key must stay empty, so that passing it costs nothing.");

[[nodiscard]] consteval bool every_canonical_tag_is_sound() noexcept {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^::foundation::permissions::tag, std::meta::access_context::unchecked()));
    bool sound = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_class_type(member)) {
            using Tag = [:member:];
            if (!PermissionTag<Tag>) sound = false;
            if (!token_is_sound<Tag>()) sound = false;
            if (permission_row_empty_v<Tag>) sound = false;
            if (!CtxAdmitsPermission<Tag, seplog_test_runner_ctx>) sound = false;
            if (CtxAdmitsPermission<Tag, seplog_hot_fg_ctx>) sound = false;
        }
    }
#pragma GCC diagnostic pop
    return sound;
}
static_assert(every_canonical_tag_is_sound(), "a tag in permissions::tag is not a sound effectful token: "
                                              "every canonical tag is an empty class with a non-empty row, "
                                              "admitted by the test runner and refused by the foreground.");

static_assert(token_is_sound<seplog_test_tag>(), "Permission<Tag> must be a 1-byte, trivially destructible, "
                                                 "non-copyable, nothrow-movable token");

}  // namespace detail::seplog_roster

static_assert(all_distinct_tags_v<>);
static_assert(all_distinct_tags_v<detail::seplog_test_left>);
static_assert(all_distinct_tags_v<detail::seplog_test_left, detail::seplog_test_right>);
static_assert(all_distinct_tags_v<detail::seplog_test_tag, detail::seplog_test_left, detail::seplog_test_right>);
static_assert(!all_distinct_tags_v<detail::seplog_test_left, detail::seplog_test_left>);
static_assert(!all_distinct_tags_v<detail::seplog_test_tag, detail::seplog_test_left, detail::seplog_test_tag>);
static_assert(!all_distinct_tags_v<detail::seplog_test_left, detail::seplog_test_left, detail::seplog_test_right>);

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

// The argument-shape concepts: the token forms, the ctx-bound forms,
// and the shapes that are neither.  An lvalue permission is refused,
// because every mint consumes what it is given.
static_assert(PermissionRootArgs<detail::seplog_test_tag>);
static_assert(PermissionRootArgs<detail::seplog_io_tag, seplog_bg_compile_ctx>);
static_assert(!PermissionRootArgs<detail::seplog_io_tag, seplog_hot_fg_ctx>);
static_assert(!PermissionRootArgs<detail::seplog_test_tag, int>);
static_assert(
    PermissionSplitArgs<detail::seplog_test_left, detail::seplog_test_right, Permission<detail::seplog_test_tag>>);
static_assert(PermissionSplitArgs<detail::seplog_test_left, detail::seplog_test_right, seplog_bg_drain_ctx const&,
                                  Permission<detail::seplog_test_tag>>);
static_assert(PermissionSplitArgs<detail::seplog_test_left, detail::seplog_test_right, seplog_bg_drain_ctx const&,
                                  seplog_hot_fg_ctx const&, Permission<detail::seplog_test_tag>>);
static_assert(
    !PermissionSplitArgs<detail::seplog_test_left, detail::seplog_test_right, Permission<detail::seplog_test_tag>&>,
    "an lvalue permission is not consumed, so it is not a split argument");
static_assert(
    !PermissionSplitArgs<detail::seplog_test_left, detail::seplog_test_right, seplog_bg_drain_ctx const&,
                         seplog_hot_fg_ctx const&, seplog_hot_fg_ctx const&, Permission<detail::seplog_test_tag>>,
    "a split takes at most two contexts");
static_assert(!PermissionSplitArgs<detail::seplog_io_tag, detail::seplog_test_right, seplog_hot_fg_ctx const&,
                                   Permission<detail::seplog_test_tag>>,
              "the one context must admit every tag the split names");
static_assert(PermissionCombineArgs<detail::seplog_test_tag, Permission<detail::seplog_test_left>,
                                    Permission<detail::seplog_test_right>>);
static_assert(!PermissionCombineArgs<detail::seplog_test_tag, Permission<detail::seplog_test_left>>,
              "a combine takes exactly two permissions");
static_assert(PermissionSplitNArgs<std::tuple<detail::seplog_test_left>, Permission<detail::seplog_test_tag>>);
static_assert(PermissionSplitNArgs<std::tuple<detail::seplog_test_left>, seplog_bg_drain_ctx const&,
                                   Permission<detail::seplog_test_tag>>);
static_assert(!PermissionSplitNArgs<std::tuple<detail::seplog_io_tag>, seplog_hot_fg_ctx const&,
                                    Permission<detail::seplog_test_tag>>,
              "the context must admit every child");
static_assert(!PermissionSplitNArgs<std::tuple<detail::seplog_test_left>, seplog_bg_drain_ctx const&>,
              "a split needs its parent permission");
static_assert(PermissionCombineNArgs<detail::seplog_test_tag, Permission<detail::seplog_test_left>,
                                     Permission<detail::seplog_test_right>>);
static_assert(
    PermissionCombineNArgs<detail::seplog_test_tag, seplog_bg_drain_ctx const&, Permission<detail::seplog_test_left>>);
static_assert(
    !PermissionCombineNArgs<detail::seplog_io_tag, seplog_hot_fg_ctx const&, Permission<detail::seplog_test_left>>,
    "the context must admit the parent");
static_assert(PermissionShareArgs<Permission<detail::seplog_test_tag>>);
static_assert(PermissionShareArgs<seplog_bg_compile_ctx const&, Permission<detail::seplog_io_tag>>);
static_assert(!PermissionShareArgs<seplog_hot_fg_ctx const&, Permission<detail::seplog_io_tag>>);
static_assert(!PermissionShareArgs<Permission<detail::seplog_test_tag>, Permission<detail::seplog_test_tag>>,
              "a share converts exactly one token");
static_assert(std::is_same_v<detail::perm_tags_t<seplog_bg_drain_ctx const&, Permission<detail::seplog_test_left>,
                                                 Permission<detail::seplog_test_right>>,
                             std::tuple<detail::seplog_test_left, detail::seplog_test_right>>);

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

}  // namespace foundation::permissions
