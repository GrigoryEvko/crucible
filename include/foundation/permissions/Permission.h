#pragma once

// Permission<Tag, Brand> mechanizes the frame rule of concurrent
// separation logic.  Move-only linearity stands in for the separating
// conjunction: at most one owner of a region exists at any moment, so
// two threads holding permissions for disjoint tags provably cannot
// conflict.
//
// The tag names a kind of region and the brand names one instance of
// it.  A root mint gives every call site a fresh brand, so two regions
// minted for one tag are two types, a token for one cannot stand in
// for the other, and a borrow proof carries the identity of the region
// it was minted from.  Every derived token inherits the brand of the
// token it came from: the children of a split carry the parent's, a
// combine demands that its children agree, and a share carries the
// exclusive's.  foundation/Brand.h states the three facts a brand
// rests on.  A spelling that names no brand is the erased identity
// DefaultBrand, under which each token of one tag is one type.  So only a
// root that is the one live root of its tag can be on it: mint_singleton_root
// gives such a root, and a second live singleton root of one tag ends the
// process.  A root of mint_permission_root keeps its brand, and no
// conversion drops a brand.  The session erase door below is the one other
// route to the erased identity.  The rule erased-brand
// (utils/assay/guard/engines/tsast/erased_brand.py) counts each spelling on
// the erased identity.
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
// the compiler does not diagnose (decided alongside fixy/Qtt.h).
// A Debug-only consumed byte was rejected there because production
// layout pins on the empty token are Debug-independent.
//
// Every mint below is one template.  A call may lead with an execution
// context, or with two for the asymmetric split, and the permissions
// come last; the leading contexts are counted off the argument list,
// and the fit of each tag to each context is one concept on the
// template head.
//
// A federation peer token has a key of its own, federation_admission_key
// of foundation/permissions/FederationDoor.h, and the one friend of that
// key is the admission that fixy/Federation.h defines.
//
// The share, the guard and the pool of a region are in
// foundation/permissions/SharedPermission.h.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Record.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/diag/RowHash.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/FederationDoor.h>
#include <foundation/permissions/Fwd.h>
#include <foundation/reflect/Hash.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <meta>
#include <new>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace foundation::permissions {

// An unconstrained tag fails silently rather than loudly: a
// `Permission<Tag*>` compiles, but the can_split_into specialization
// written for `Tag` never matches it and the pool instantiates under a
// different key, so every discipline check quietly stops applying.

template <typename T>
concept PermissionTag = std::is_class_v<T> && std::is_empty_v<T>;

// Permission and SharedPermission are declared, with their defaulted
// brand, in Fwd.h.

namespace tag {

struct DiskSpilledRegionTag {};
struct HugePageTag {};
struct MmapRegionTag {};
struct GpuMemoryTag {};
struct NetworkBufferTag {};

// The tag of a peer of another organization, tag::FederatedPeer, is in
// foundation/permissions/FederationDoor.h, beside the key of its one
// constructor.

}  // namespace tag

// The declarative manifest of valid splits.  C++ has no orphan rule, so
// a foreign translation unit can specialize this trait for a tag it does
// not own and forge cross-region authority.  A manifest must therefore
// be declared in the same translation unit as the parent tag, and a
// build-time source scan rejects one that is not.

template <typename Parent, typename L, typename R>
struct can_split_into : std::false_type {};

template <typename Parent, typename L, typename R>
inline constexpr bool can_split_into_v = can_split_into<Parent, L, R>::value;

template <typename Parent, typename... Children>
struct can_split_into_pack : std::false_type {};

template <typename Parent, typename... Children>
inline constexpr bool can_split_into_pack_v = can_split_into_pack<Parent, Children...>::value;

// A second trait that every legitimate split must specialize alongside
// the first, in the same translation unit.  The duplication is not
// redundant.  A forged specialization of can_split_into alone is caught
// here, at the mint gate.  A forged specialization of both is caught by
// the build-time source scan, which looks for either trait name outside
// the locations allowed to author manifests.  Defeating the discipline
// takes both.

template <typename Parent, typename L, typename R>
struct has_split_authoring_witness : std::false_type {};

template <typename Parent, typename L, typename R>
inline constexpr bool has_split_authoring_witness_v = has_split_authoring_witness<Parent, L, R>::value;

template <typename Parent, typename... Children>
struct has_split_pack_authoring_witness : std::false_type {};

template <typename Parent, typename... Children>
inline constexpr bool has_split_pack_authoring_witness_v = has_split_pack_authoring_witness<Parent, Children...>::value;

template <typename Parent, typename L, typename R>
inline constexpr bool well_authored_split_v =
    can_split_into_v<Parent, L, R> && has_split_authoring_witness_v<Parent, L, R>;

template <typename Parent, typename... Children>
inline constexpr bool well_authored_split_pack_v =
    can_split_into_pack_v<Parent, Children...> && has_split_pack_authoring_witness_v<Parent, Children...>;

// The split manifests say which parent a set of children decomposes,
// but nothing about whether those children name disjoint regions.  A
// manifest declaring the same tag twice would mint two linear tokens
// for one region and hand each to a different thread, which is the race
// the frame rule exists to rule out.
//
// The test is a function at namespace scope that is not a template, so
// no translation unit can specialize it to call two equal tags distinct.

namespace detail {

// True when no two of the tags are one type, read through their aliases.
// Complexity: quadratic in the number of tags.
[[nodiscard]] consteval bool tags_are_distinct_(std::vector<std::meta::info> const& tags) {
    for (std::size_t first = 0; first < tags.size(); ++first) {
        for (std::size_t second = first + 1; second < tags.size(); ++second) {
            if (std::meta::dealias(tags[first]) == std::meta::dealias(tags[second])) return false;
        }
    }
    return true;
}

}  // namespace detail

[[nodiscard]] consteval bool tags_are_distinct(std::initializer_list<std::meta::info> tags) {
    return detail::tags_are_distinct_(std::vector<std::meta::info>(tags));
}

// The same test over a pack, for a requires-clause.
template <typename... Tags>
concept DistinctTags = tags_are_distinct({^^Tags...});

// ── The effect row of a permission tag ───────────────────────────────
//
// A tag's row is the set of effects that touching its region incurs,
// and it is what a context must admit before a token for the tag can be
// minted under it.  The relation from tag to row is closed: it is the
// edge variables of the namespace below, or a `permission_row` member
// the tag declares about itself, and nothing else.  A tag with neither
// has no row, and every mint of it is a hard error naming the fix.
//
// The previous shape was a primary template answering Row<> for every
// tag nobody had specialized it for.  Under that shape an unregistered
// tag was a pure tag by default, so a region whose touches block or do
// IO could be minted under the foreground context by the one omission
// this relation now refuses to make for the author.
//
// Three sources, in this order of precedence:
//   1. an edge `edge<Tag, Row<...>>` in permission_rows, the enumerable
//      form, which every_class_in_has_edge audits for the canonical tags.
//      Only this header holds a declaration in the namespace.  The seal
//      of the namespace stops the build at the first read after a member
//      that another file adds, and the rule home of the quarantine plugin
//      refuses such a member in each other file;
//   2. `using permission_row = Row<...>;` inside the tag, for a tag
//      template such as a channel's Whole<UserTag>, whose instances
//      cannot each be an edge;
//   3. `using parent_type = Parent;` inside the tag, for a tag derived
//      from another, such as a region shard, which has its parent's row.
// A tag that declares both an edge and a member is an error, so one
// row has one source.

namespace permission_rows {

inline constexpr ::foundation::fail_closed::edge<
    tag::DiskSpilledRegionTag,
    ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>>
    disk_spilled_region{};
inline constexpr ::foundation::fail_closed::edge<tag::HugePageTag,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::IO>>
    huge_page{};
inline constexpr ::foundation::fail_closed::edge<tag::MmapRegionTag,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::IO>>
    mmap_region{};
inline constexpr ::foundation::fail_closed::edge<tag::GpuMemoryTag,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::Alloc>>
    gpu_memory{};
inline constexpr ::foundation::fail_closed::edge<tag::NetworkBufferTag,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::IO>>
    network_buffer{};

// Every read counts the members against this seal, so a member that
// another file adds stops the build rather than giving a tag a row.
inline constexpr ::foundation::fail_closed::seal sealed{.members = 5};

}  // namespace permission_rows

// Each answer below about the row of a tag is a function at namespace
// scope that is not a template, so no translation unit can specialize it
// to give a tag a lighter row.  A tag states its row in its own
// declarations or in the edge namespace above, and nothing else states it.
namespace detail {

// The type that a member alias of the tag names, read through its
// aliases, or a null reflection when the tag declares no such alias.
[[nodiscard]] consteval std::meta::info member_alias_of(std::meta::info tag, std::string_view name) {
    const std::meta::info member = ::foundation::reflect::member_named(tag, name);
    if (member == std::meta::info{} || !std::meta::is_type_alias(member)) return {};
    return std::meta::dealias(member);
}

// How the declarations of a tag state its row.
enum class row_shape : unsigned char {
    none,  // no source declares a row
    by_edge,  // one edge of permission_rows
    by_member,  // a permission_row member
    by_parent,  // the row of the parent_type member
    declared_twice,  // an edge and a permission_row member
    derived_with_own_row,  // a parent_type, and an edge or a permission_row member
    two_edges,  // more than one edge of permission_rows
};

// Has no constant definition.  A call to it stops the constant
// evaluation, and the diagnostic names it.
void a_row_cell_names_another_tag() noexcept;

class row_cell;

// Reads the declarations of the tag one time, and the edges that
// permission_rows holds when it has rows_members members.
[[nodiscard]] consteval row_cell walk_row(std::meta::info tag, std::size_t rows_members);

// What one walk found about the row of one tag, at one count of the
// members of permission_rows.  The cell is the value of row_cell_at, so a
// repeated question about a tag reads it through a pointer.  The two
// constructors are private and user-provided, and the walk is their one
// friend.  So a specialization of row_cell_at can only hold the walk of
// another tag, which the read refuses.
class row_cell {
public:
    [[nodiscard]] consteval std::meta::info tag() const noexcept { return tag_; }
    [[nodiscard]] consteval std::size_t rows_members() const noexcept { return rows_members_; }
    [[nodiscard]] consteval row_shape shape() const noexcept { return shape_; }
    [[nodiscard]] consteval std::meta::info row() const noexcept { return row_; }
    [[nodiscard]] consteval std::meta::info parent() const noexcept { return parent_; }

private:
    consteval row_cell() noexcept {}
    consteval row_cell(const row_cell& other) noexcept
        : tag_{other.tag_},
          rows_members_{other.rows_members_},
          shape_{other.shape_},
          row_{other.row_},
          parent_{other.parent_} {}

    std::meta::info tag_{};
    std::size_t rows_members_ = 0;
    row_shape shape_ = row_shape::none;
    std::meta::info row_ = ^^void;
    std::meta::info parent_{};

    friend consteval row_cell walk_row(std::meta::info tag, std::size_t rows_members);
};

// Reads the edges of permission_rows one time, and counts the edges from
// the tag in that one read.  Complexity: linear in the edges.
[[nodiscard]] consteval row_cell walk_row(std::meta::info tag, std::size_t rows_members) {
    row_cell cell{};
    cell.tag_ = tag;
    cell.rows_members_ = rows_members;
    const ::foundation::fail_closed::edge_list rows = ::foundation::fail_closed::edges_of(^^permission_rows);
    const std::meta::info wanted = std::meta::dealias(tag);
    std::size_t edges = 0;
    std::meta::info edge_row = ^^void;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const ::foundation::fail_closed::edge_ends edge = rows[index];
        if (edge.from != wanted) continue;
        if (edges == 0) edge_row = edge.to;
        ++edges;
    }
    const std::meta::info member_row = member_alias_of(tag, "permission_row");
    const std::meta::info parent = member_alias_of(tag, "parent_type");
    const bool by_member = member_row != std::meta::info{};
    if (edges != 0 && by_member) {
        cell.shape_ = row_shape::declared_twice;
    } else if (parent != std::meta::info{} && (edges != 0 || by_member)) {
        cell.shape_ = row_shape::derived_with_own_row;
    } else if (edges > 1) {
        cell.shape_ = row_shape::two_edges;
    } else if (edges == 1) {
        cell.shape_ = row_shape::by_edge;
        cell.row_ = edge_row;
    } else if (by_member) {
        cell.shape_ = row_shape::by_member;
        cell.row_ = member_row;
    } else if (parent != std::meta::info{}) {
        cell.shape_ = row_shape::by_parent;
        cell.parent_ = parent;
    }
    return cell;
}

// The walk of the tag Tag when permission_rows has RowsMembers members.
// GCC evaluates the initializer one time for each key.
template <std::meta::info Tag, std::size_t RowsMembers>
inline constexpr row_cell row_cell_at = walk_row(Tag, RowsMembers);

// The member count of permission_rows at the point of the evaluation that
// asks.  The count is the cost of a cached question about a row, because
// members_of builds a vector of each member.
[[nodiscard]] consteval std::size_t rows_members_now_() {
    // The count is not const.  GCC evaluates a const integral local with a
    // constant initializer one time, where the function is defined, so a
    // const count would miss each edge of a later part of the namespace.
    std::size_t rows_members =
        std::meta::members_of(^^permission_rows, std::meta::access_context::unprivileged()).size();
    return rows_members;
}

// The cell of the tag at one member count of permission_rows.  A cell
// that names another tag or another count is the walk of another tag,
// which a specialization put in the place of this walk, and the read
// stops the build.
[[nodiscard]] consteval const row_cell& read_row_cell_(std::meta::info tag, std::size_t rows_members) {
    const row_cell& cell = std::meta::extract<const row_cell&>(std::meta::substitute(
        ^^row_cell_at, {std::meta::reflect_constant(tag), std::meta::reflect_constant(rows_members)}));
    if (cell.tag() != tag || cell.rows_members() != rows_members) a_row_cell_names_another_tag();
    return cell;
}

// The reflection of the row of the tag at one member count of
// permission_rows, or of void when no source declares one.  Each refusal
// is thrown here, in the evaluation that asked, so the diagnostic names
// the question.
[[nodiscard]] consteval std::meta::info permission_row_source_at_(std::meta::info tag, std::size_t rows_members) {
    const row_cell& cell = read_row_cell_(tag, rows_members);
    switch (cell.shape()) {
        case row_shape::declared_twice:
            throw std::meta::exception(u8"permission_row: a tag declares its row twice, as an edge in "
                                       u8"foundation::permissions::permission_rows and as a permission_row member.  "
                                       u8"One row has one source; remove one of them.",
                                       tag);
        // A derived tag has the row of its parent and no row of its own.  An
        // edge or a member for a shard would replace the row of its parent
        // with a lighter one, and a region whose touches do IO would mint its
        // shards under a context that admits none.
        case row_shape::derived_with_own_row:
            throw std::meta::exception(u8"permission_row: a derived tag declares a row of its own.  A tag with a "
                                       u8"parent_type has the row of its parent; remove its edge in "
                                       u8"foundation::permissions::permission_rows or its permission_row member.",
                                       tag);
        // The relation is a function of its From, so two edges are refused
        // by the query that reads it as one.
        case row_shape::two_edges:
            return ::foundation::fail_closed::unique_target(^^permission_rows, tag);
        case row_shape::by_parent:
            return permission_row_source_at_(cell.parent(), rows_members);
        default:
            return cell.row();
    }
}

// The reflection of the row of the tag at the member count of
// permission_rows now, or of void when no source declares one.
[[nodiscard]] consteval std::meta::info permission_row_source(std::meta::info tag) {
    return permission_row_source_at_(tag, rows_members_now_());
}

// The row that permission_row_source gave for the tag, after a check that
// it is one.  A tag with no row, or with a row that is no
// foundation::effects::Row, stops the build here.
[[nodiscard]] consteval std::meta::info checked_row_(std::meta::info tag, std::meta::info row) {
    if (row == ^^void) {
        throw std::meta::exception(u8"permission_row: no effect row is declared for this permission tag, so no "
                                   u8"token for it can be minted.  Declare `using permission_row = "
                                   u8"foundation::effects::Row<...>;` inside the tag.  A pure tag declares Row<> "
                                   u8"explicitly.  The edge namespace foundation::permissions::permission_rows is for "
                                   u8"the tags of Permission.h and ReadView.h.",
                                   tag);
    }
    if (!::foundation::effects::is_effect_row(row)) {
        throw std::meta::exception(
            u8"permission_row: the row declared for this permission tag is not a foundation::effects::Row.", tag);
    }
    return row;
}

// The reflection of the row of the tag, after the check of checked_row_.
[[nodiscard]] consteval std::meta::info permission_row_of(std::meta::info tag) {
    return checked_row_(tag, permission_row_source(tag));
}

// What the rows of the tags of one mint say together.  A mint without a
// context reads the facts one time.  It evaluates each assertion that
// asks has_permission_row or permission_row_empty only when the facts say
// that the assertion fails, so the diagnostic names the tag.
struct rows_facts {
    bool declared = false;  // each tag declares a row
    bool empty = false;  // each tag declares a foundation::effects::Row with no effect
};

// The facts of the tags from first to last, in the order of the
// conjunction of the mint that asks.  The first tag with no row stops the
// read, as a false has_permission_row stops that conjunction.  A row that
// is no Row is not empty here, and the assertion that asks
// permission_row_empty then gives the refusal of checked_row_.  An empty
// range holds each fact and reads nothing.  The read goes through a
// pointer, because a std::span costs each includer the instantiation of
// its constructors.  Complexity: linear in the number of tags, with one
// read of the member count of permission_rows.
[[nodiscard]] consteval rows_facts rows_facts_of(const std::meta::info* first, const std::meta::info* last) {
    rows_facts facts{.declared = true, .empty = true};
    if (first == last) return facts;
    std::size_t rows_members = rows_members_now_();
    for (const std::meta::info* tag = first; tag != last; ++tag) {
        const std::meta::info row = permission_row_source_at_(*tag, rows_members);
        if (row == ^^void) return rows_facts{};
        if (facts.empty) {
            facts.empty = ::foundation::effects::is_effect_row(row) && ::foundation::effects::row_size(row) == 0;
        }
    }
    return facts;
}

[[nodiscard]] consteval rows_facts rows_facts_of(std::initializer_list<std::meta::info> tags) {
    return rows_facts_of(tags.begin(), tags.end());
}

}  // namespace detail

// True when some source declares a row for the tag.  Read by the
// concepts, so an undeclared tag fails a constraint rather than stopping
// the build inside it, and by the mints, which then name the fix.
[[nodiscard]] consteval bool has_permission_row(std::meta::info tag) {
    return detail::permission_row_source(tag) != ^^void;
}

template <typename Tag>
using permission_row_t = [:detail::permission_row_of(^^Tag):];

// True when the tag declares the empty row.  A tag with no row is false,
// without naming the row, so one undeclared tag reports the one refusal
// that names the fix rather than a cascade behind it.
[[nodiscard]] consteval bool permission_row_empty(std::meta::info tag) {
    const std::meta::info row = detail::permission_row_source(tag);
    if (row == ^^void) return false;
    return ::foundation::effects::row_size(detail::checked_row_(tag, row)) == 0;
}

template <typename Tag, typename Ctx>
concept CtxAdmitsPermission = ::foundation::effects::IsExecCtx<Ctx> && has_permission_row(^^Tag)
                           && ::foundation::effects::is_subrow(^^permission_row_t<Tag>, ^^typename Ctx::row_type);

// The concepts below carry an Is prefix because a concept and a class
// share one namespace lookup table, so a `concept Permission` would
// shadow the class of that name.

// Each answer below about a token or an argument list is a concept, or a
// function at namespace scope that is not a template, so no translation
// unit can specialize it to make a class a token or a context.

namespace detail {

// The template argument at the position of a specialization of the class
// template, read through aliases and with no qualifier, or void when the
// type is no specialization of it.
[[nodiscard]] consteval std::meta::info instance_argument(std::meta::info type, std::meta::info class_template,
                                                          std::size_t position) {
    const std::meta::info bare = std::meta::dealias(std::meta::remove_cvref(type));
    if (!std::meta::has_template_arguments(bare) || std::meta::template_of(bare) != class_template) return ^^void;
    return std::meta::dealias(std::meta::template_arguments_of(bare)[position]);
}

// True when the type, with no qualifier, is a Permission.
[[nodiscard]] consteval bool is_permission_type(std::meta::info type) {
    const std::meta::info bare = std::meta::dealias(std::meta::remove_cvref(type));
    return std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^Permission;
}

}  // namespace detail

template <typename T>
concept IsPermission = ::foundation::reflect::IsInstanceOf<T, ^^Permission>;

// The region tag of a token.  The extractor is constrained on its own
// predicate, so a type of the other kind, or of no kind, is refused at
// the alias and does not become void.  The extractor of a share is in
// foundation/permissions/SharedPermission.h.
template <typename T>
    requires IsPermission<T>
using permission_tag_t = typename std::remove_cvref_t<T>::tag_type;

template <typename T, typename Tag>
concept IsPermissionFor = IsPermission<T> && std::is_same_v<permission_tag_t<T>, Tag>;

// The shape of a mint's argument list: zero, one or two execution
// contexts, then the permissions.  A context argument is anything the
// context test recognizes after the reference is stripped; a permission
// argument is a Permission passed as an rvalue, because every mint
// consumes the tokens it is given.  The count of leading contexts is what
// each mint's fit concept dispatches on.

namespace detail {

// The tag and the brand of a permission argument.  Each mint reads them
// only after MintArgs has found a Permission at that position.
template <typename A>
using perm_tag_t = typename std::remove_cvref_t<A>::tag_type;

template <typename A>
using perm_brand_t = typename std::remove_cvref_t<A>::brand_type;

// The number of execution contexts at the head of the argument list.
[[nodiscard]] consteval std::size_t leading_ctx_count(std::initializer_list<std::meta::info> args) {
    std::size_t count = 0;
    for (const std::meta::info arg : args) {
        if (!::foundation::effects::is_exec_ctx(arg)) break;
        ++count;
    }
    return count;
}

// True when the argument list is Ctxs..., Perms... with at most max_ctx
// contexts and exactly n_perms permissions, or any number of permissions
// when n_perms is zero.
[[nodiscard]] consteval bool permission_mint_args_shaped(std::size_t max_ctx, std::size_t n_perms,
                                                         std::initializer_list<std::meta::info> args) {
    const std::size_t n_ctx = leading_ctx_count(args);
    if (n_ctx > max_ctx) return false;
    const std::size_t n_perm = args.size() - n_ctx;
    if (n_perms != 0 && n_perm != n_perms) return false;
    if (n_perm == 0) return false;
    std::size_t index = 0;
    for (const std::meta::info arg : args) {
        if (index++ < n_ctx) continue;
        if (std::meta::is_lvalue_reference_type(arg) || !is_permission_type(arg)) return false;
    }
    return true;
}

template <std::size_t MaxCtx, std::size_t NPerms, typename... Args>
concept MintArgs = permission_mint_args_shaped(MaxCtx, NPerms, {^^Args...});

// The permission tags of Args past the leading contexts, as a tuple of
// tags, and the row check every ctx-bound mint shares: each named tag
// is admitted by the one context.
// Only the decltype of perm_tags_t names perm_tags_, so no call instantiates
// its body.  The body keeps its definition in this header.
template <typename... Args, std::size_t... Is>
consteval auto perm_tags_(std::index_sequence<Is...>) noexcept
    -> std::tuple<perm_tag_t<Args...[Is + leading_ctx_count({^^Args...})]>...> {
    return {};
}

template <typename... Args>
using perm_tags_t =
    decltype(perm_tags_<Args...>(std::make_index_sequence<sizeof...(Args) - leading_ctx_count({^^Args...})>{}));

// The brand of the first permission past the leading contexts.
template <typename... Args>
using first_perm_brand_t = perm_brand_t<Args...[leading_ctx_count({^^Args...})]>;

// True when every permission in the argument list past the leading
// contexts carries the brand of the first one.  A combine of tokens from
// two instances of one tag is what this refuses: the tags match, the
// regions do not.
[[nodiscard]] consteval bool perm_brands_agree(std::initializer_list<std::meta::info> args) {
    std::meta::info first{};
    for (const std::meta::info arg : args) {
        if (!is_permission_type(arg)) continue;
        const std::meta::info brand = instance_argument(arg, ^^Permission, 1);
        if (first == std::meta::info{}) {
            first = brand;
        } else if (brand != first) {
            return false;
        }
    }
    return true;
}

// True when the context admits the row of each tag.
template <typename Ctx, typename... Tags>
concept CtxAdmitsEach = (CtxAdmitsPermission<Tags, std::remove_cvref_t<Ctx>> && ...);

// True when the tag tuple is a std::tuple and the context admits the row
// of each of its tags.
[[nodiscard]] consteval bool ctx_admits_each_tag_of(std::meta::info ctx, std::meta::info tag_tuple) {
    const std::meta::info tuple = std::meta::dealias(tag_tuple);
    if (!std::meta::has_template_arguments(tuple) || std::meta::template_of(tuple) != ^^std::tuple) return false;
    const std::meta::info bare_ctx = std::meta::remove_cvref(ctx);
    for (const std::meta::info tag : std::meta::template_arguments_of(tuple)) {
        if (!std::meta::extract<bool>(std::meta::substitute(^^CtxAdmitsPermission, {tag, bare_ctx}))) return false;
    }
    return true;
}

}  // namespace detail

// The fit of one mint call.  With no context the call is the token
// form, whose row requirement is a static_assert in the body so the
// message names the ctx-bound spelling to use instead.  With one
// context every tag the call names, parents and children alike, must be
// admitted by it.  The split alone also takes two contexts, one per
// child, for a parent that is torn across two scopes.

// A federation peer is admitted by a verified handshake, and never
// minted from nothing, so the root mint refuses its tag with a context
// and without one.
template <typename Tag>
concept RootMintableTag = !IsFederatedPeerTag<Tag>;

template <typename Tag, typename... Args>
concept PermissionRootArgs =
    RootMintableTag<Tag>
    && ((sizeof...(Args) == 0) || (sizeof...(Args) == 1 && (detail::CtxAdmitsEach<Args, Tag> && ...)));

// A tag of a singleton root: a tag that the root mint admits, with a stable
// identity, so the registry of the singleton roots can key its live count.
template <typename Tag>
concept SingletonRootTag = RootMintableTag<Tag> && ::foundation::reflect::HasStableIdentity<Tag>;

// The fit of one singleton root mint: the fit of the root mint, for a tag of
// a singleton root.
template <typename Tag, typename... Args>
concept SingletonRootArgs = SingletonRootTag<Tag> && PermissionRootArgs<Tag, Args...>;

template <typename L, typename R, typename... Args>
concept PermissionSplitArgs = detail::MintArgs<2, 1, Args...>
                           && (detail::leading_ctx_count({^^Args...}) == 0
                               || (detail::leading_ctx_count({^^Args...}) == 1
                                   && detail::CtxAdmitsEach<Args...[0], detail::perm_tag_t<Args...[1]>, L, R>)
                               || (detail::leading_ctx_count({^^Args...}) == 2
                                   && detail::CtxAdmitsEach<Args...[0], L> && detail::CtxAdmitsEach<Args...[1], R>));

template <typename In, typename... Args>
concept PermissionCombineArgs =
    detail::MintArgs<1, 2, Args...>
    && (detail::leading_ctx_count({^^Args...}) == 0
        || detail::CtxAdmitsEach<Args...[0], In, detail::perm_tag_t<Args...[1]>, detail::perm_tag_t<Args...[2]>>);

template <typename ChildrenTuple, typename... Args>
concept PermissionSplitNArgs = detail::MintArgs<1, 1, Args...>
                            && (detail::leading_ctx_count({^^Args...}) == 0
                                || (detail::CtxAdmitsEach<Args...[0], detail::perm_tag_t<Args...[1]>>
                                    && detail::ctx_admits_each_tag_of(^^Args...[0], ^^ChildrenTuple)));

template <typename Parent, typename... Args>
concept PermissionCombineNArgs = detail::MintArgs<1, 0, Args...>
                              && (detail::leading_ctx_count({^^Args...}) == 0
                                  || (detail::CtxAdmitsEach<Args...[0], Parent>
                                      && detail::ctx_admits_each_tag_of(^^Args...[0], ^^detail::perm_tags_t<Args...>)));

// The root mint is where a brand is born, so the fresh brand is its
// last template parameter, after the argument pack.  A caller can name
// the tag and nothing else: an explicit third argument is absorbed by
// the pack and fails deduction.  The result type holds the brand in
// undeducible_brand_t, so the address of the mint with a target type that
// names a held brand, and an explicit specialization for such a brand,
// set no brand either (foundation/Brand.h).  Together the two stop a
// caller minting a second token of an identity it already holds.  Every
// other mint carries the brand of the token it consumes.

template <typename Tag, typename... Args, typename Brand = CRUCIBLE_FRESH_BRAND>
    requires PermissionRootArgs<Tag, Args...>
[[nodiscard]] constexpr Permission<Tag, ::foundation::brand::undeducible_brand_t<Brand>>
mint_permission_root(Args const&...) noexcept;

// The token of a tag on the erased identity DefaultBrand, under which each
// token of the tag is one type.  For a tag that a root mint admits, only the
// singleton root door and the session erase door give a token of this type.
// Each site that names the erased token names this alias, so a search for it
// lists them.
template <typename Tag>
using ErasedPermission = Permission<Tag>;

// The root of a tag on the erased identity, which the singleton root
// section below the class Permission defines.  It takes the live count of
// the tag, so it is no constant expression.
template <typename Tag, typename... Args>
    requires SingletonRootArgs<Tag, Args...>
[[nodiscard]] ErasedPermission<Tag> mint_singleton_root(Args const&...) noexcept;

template <typename L, typename R, typename... Args>
    requires PermissionSplitArgs<L, R, Args...>
[[nodiscard]] constexpr ::foundation::core::Record<Permission<L, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>,
                                                   Permission<R, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>>
mint_permission_split(Args&&...) noexcept;

template <typename In, typename... Args>
    requires PermissionCombineArgs<In, Args...>
[[nodiscard]] constexpr Permission<In, detail::perm_brand_t<Args...[sizeof...(Args) - 2]>>
mint_permission_combine(Args&&...) noexcept;

template <typename... Children, typename... Args>
    requires PermissionSplitNArgs<std::tuple<Children...>, Args...>
[[nodiscard]] constexpr ::foundation::core::Record<
    Permission<Children, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>...>
mint_permission_split_n(Args&&...) noexcept;

template <typename Parent, typename... Args>
    requires PermissionCombineNArgs<Parent, Args...>
[[nodiscard]] constexpr Permission<Parent, detail::first_perm_brand_t<Args...>>
mint_permission_combine_n(Args&&...) noexcept;

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
//
// Both constructors of the key are user-provided, and not defaulted.
// A key whose copy is trivial is trivially copyable, and std::bit_cast
// then builds one from any byte.  A key with any trivial constructor is
// an implicit-lifetime type, and std::start_lifetime_as then builds one
// over a buffer.  Neither route names a constructor, so neither sees
// the access check below.  The same holds for the token.
class perm_mint_key {
    constexpr perm_mint_key() noexcept {}

public:
    constexpr perm_mint_key(const perm_mint_key&) noexcept {}

private:
    // Every entry below is another way to forge authority over a
    // region.  Additions need review.  Each mint is one template, so
    // each is one entry, whether it is called with a context or
    // without.  A friend declaration carries no default argument, so
    // the root mint's fresh brand is spelled on the first declaration
    // above and matched here by position.

    template <typename Tag, typename... Args, typename Brand>
        requires PermissionRootArgs<Tag, Args...>
    friend constexpr Permission<Tag, ::foundation::brand::undeducible_brand_t<Brand>>
    mint_permission_root(Args const&...) noexcept;

    template <typename Tag, typename... Args>
        requires SingletonRootArgs<Tag, Args...>
    friend ErasedPermission<Tag> mint_singleton_root(Args const&...) noexcept;

    template <typename L, typename R, typename... Args>
        requires PermissionSplitArgs<L, R, Args...>
    friend constexpr ::foundation::core::Record<Permission<L, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>,
                                                Permission<R, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>>
    mint_permission_split(Args&&...) noexcept;

    template <typename In, typename... Args>
        requires PermissionCombineArgs<In, Args...>
    friend constexpr Permission<In, detail::perm_brand_t<Args...[sizeof...(Args) - 2]>>
    mint_permission_combine(Args&&...) noexcept;

    template <typename... Children, typename... Args>
        requires PermissionSplitNArgs<std::tuple<Children...>, Args...>
    friend constexpr ::foundation::core::Record<
        Permission<Children, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>...>
    mint_permission_split_n(Args&&...) noexcept;

    template <typename Parent, typename... Args>
        requires PermissionCombineNArgs<Parent, Args...>
    friend constexpr Permission<Parent, detail::first_perm_brand_t<Args...>>
    mint_permission_combine_n(Args&&...) noexcept;
};

// The key of the federation door, federation_admission_key, is in
// foundation/permissions/FederationDoor.h.  It builds a token whose tag is
// a FederatedPeer and no other token.

// The session erase door.  A session permission set keys a region by its
// tag, so the entries of a session hold drop the brand of each token that
// they take.  This header names the class, and fixy/session/Flow.h defines
// it: its one member is private, and its one friend is the HoldFactory of
// that header.  The rule home of the quarantine plugin refuses a
// definition of the class in each other file, as it does for
// FederationAdmission (foundation/permissions/FederationDoor.h).
//
// The door is a known hole.  A token that leaves a session is on the
// erased identity, so a session gives the same erased type as a singleton
// root.  The door goes when the session layer carries the brand of each
// token.
class SessionEraseDoor;

// The key of the one conversion that drops a brand.  Only the session
// erase door makes one, so no other expression converts a branded token to
// the erased identity.  Both constructors are user-provided, for the reason
// perm_mint_key gives.
class erase_brand_key {
    constexpr erase_brand_key() noexcept {}

public:
    constexpr erase_brand_key(const erase_brand_key&) noexcept {}

private:
    friend class SessionEraseDoor;
};

// The tag constraint is a class-body static_assert rather than a
// requires-clause on the primary template.  A requires-clause would
// force every forward declaration of Permission to repeat it, which
// defeats forward-declare-and-specialize as a way of avoiding this
// header.

// A token is a linear value: each path uses it one time, through a door.
// The annotation authority tells the gate of foundation/effects/Computation.h
// that a token is the authority to touch a region.  The annotation
// no_start_over_bytes refuses a token to the checked lifetime start of
// foundation/Lifetime.h.
template <typename Tag, typename Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]] Permission {
    static_assert(PermissionTag<Tag>, "Permission<Tag>: Tag must be an empty non-union class type "
                                      "(see PermissionTag concept above).  Pointers, references, "
                                      "primitives, enums, unions, and stateful classes are rejected. "
                                      "Per CSL convention, Tag is a phantom-type marker — typically "
                                      "an empty struct in a `tag::` namespace.");
    static_assert(::foundation::brand::IsBrand<Brand>, "Permission<Tag, Brand>: Brand must be an empty class type: "
                                                       "the closure of a mint's fresh lambda, or DefaultBrand.");

public:
    using tag_type = Tag;
    using brand_type = Brand;

    // Holding the key is the proof of authority, and only the five
    // mints can make one, so this is the sole route to a Permission for
    // every tag but a federation peer.
    // Permission itself befriends nobody.  The constraint keeps a forged
    // split manifest from reaching a federation peer token through a
    // split or a combine.
    explicit constexpr Permission(perm_mint_key) noexcept
        requires(!IsFederatedPeerTag<Tag>)
    {}

    // The sole route to a federation peer token.  Only an admission
    // makes the key, after a handshake verifies, and the key builds no
    // other token.
    explicit constexpr Permission(federation_admission_key) noexcept
        requires IsFederatedPeerTag<Tag>
    {}

    // Erasure.  A token of one instance becomes a token on the erased
    // identity, and the conversion consumes the branded token.  Only the
    // session erase door makes the key, so a search for SessionEraseDoor
    // lists each site that drops a brand.  The erasure runs in one
    // direction only.  Nothing makes a branded token out of an erased one,
    // and nothing rebrands a token.  Either one mints an identity that the
    // caller does not hold.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr Permission(Permission<Tag, Other>&&, erase_brand_key) noexcept {}

    Permission(const Permission&) = delete(
        "Permission<Tag>: linear — duplicating creates two simultaneous owners of the same region, breaking CSL's frame rule.  Use std::move to transfer.");
    Permission& operator=(const Permission&) =
        delete("Permission<Tag>: linear — assignment would overwrite an existing permission token.");
    // The move constructor and the destructor are trivial, so the ABI
    // passes a token in a register.  The move assignment is user-provided,
    // so the token is not trivially copyable, and std::bit_cast builds no
    // token from a byte.  An assignment also stays a call in the tree of
    // GCC, so a rule of the plugin can read it.  An assignment over a live
    // token drops that token with no door.
    //
    // The trivial move constructor makes the token an implicit-lifetime
    // type.  The rule owners refuses std::start_lifetime_as in the
    // tree, and the annotation no_start_over_bytes refuses the checked
    // lifetime start of foundation/Lifetime.h.
    //
    // A moved-from token keeps no record of the move, so it can still pass
    // to a door.  The token is an empty class: a record would need a byte in
    // each token and in each handle that holds one, and a move constructor
    // that is not trivial, so the ABI would pass each token in memory.  The
    // static rule use-after-move refuses a use of a moved-from local token,
    // and that rule is the one check of this case.  A Capability and a
    // SoleWriter record the move and end the process at a later use.
    constexpr Permission(Permission&&) noexcept = default;
    constexpr Permission& operator=(Permission&&) noexcept { return *this; }
    ~Permission() = default;

    // A token lives in a scope, a handle or a parameter.  A new-expression
    // gives it storage that no scope ends, and each copy of the pointer
    // reaches the one token, so two holders can move it out.  The class
    // refuses the new-expression, std::make_unique and the delete of a
    // std::unique_ptr.  std::optional, std::tuple and a handle member
    // construct the token in place, so they stay admitted.  A global ::new
    // and a standard allocator, as in std::make_shared or std::vector, do
    // not use these functions.
    static void* operator new(std::size_t) = delete("a Permission lives in a scope or a handle, not on the heap");
    static void* operator new[](std::size_t) = delete("a Permission lives in a scope or a handle, not on the heap");
    static void* operator new(std::size_t,
                              std::align_val_t) = delete("a Permission lives in a scope or a handle, not on the heap");
    static void* operator new[](std::size_t, std::align_val_t) =
        delete("a Permission lives in a scope or a handle, not on the heap");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;
};

// A token that a caller gives up goes through the drop door of
// foundation/Linear.h, ::foundation::linear::drop.

// The root mint is reentrant: each call gives a fresh token.  Each call
// site gives its own brand, so the roots of two sites are two types.  A site
// that runs two times gives two live roots of one brand (fact 2 of
// foundation/Brand.h), and each root is sound while its holder does not
// alias it.  Code that needs one type for each tag, before a root exists,
// mints its root with mint_singleton_root below.
template <typename Tag, typename... Args, typename Brand>
    requires PermissionRootArgs<Tag, Args...>
[[nodiscard]] constexpr Permission<Tag, ::foundation::brand::undeducible_brand_t<Brand>>
mint_permission_root(Args const&...) noexcept {
    // With a context, the fit concept proves the row of the tag.  Without
    // one, the facts read the row one time.
    constexpr detail::rows_facts facts =
        sizeof...(Args) == 0 ? detail::rows_facts_of({^^Tag}) : detail::rows_facts_of({});
    if constexpr (!facts.declared) {
        static_assert(has_permission_row(^^Tag), "mint_permission_root<Tag>: no effect row is declared for Tag, so "
                                                 "nothing says which contexts may own its region.  Declare a "
                                                 "permission_row member on the tag.  A pure tag declares Row<> "
                                                 "explicitly.");
    } else if constexpr (!facts.empty) {
        static_assert(!has_permission_row(^^Tag) || sizeof...(Args) == 1 || permission_row_empty(^^Tag),
                      "mint_permission_root<Tag>() without an ExecCtx is only valid for "
                      "permission_row<Tag> == Row<>.  Effectful permission tags must be "
                      "minted with mint_permission_root<Tag>(ctx) so Ctx admits the tag's row.");
    }
    return Permission<Tag, Brand>{perm_mint_key{}};
}

// ── The singleton root ───────────────────────────────────────────────
//
// Some code names the type of a token before its root exists, such as the
// type of a mapping member of a facade.  That type is on the erased identity
// DefaultBrand, under which each token of the tag is one type.  So the erased
// identity is sound only for the one live root of its tag.
//
// mint_singleton_root gives that root.  It records the tag as taken in the
// live count of the tag, and a second live singleton root of one tag ends the
// process.  release_singleton_root consumes the root and clears the count, so
// a later mint takes the tag again.  A token is an empty class with a trivial
// destructor, so its end is no run-time event.  A holder that drops a
// singleton root with no release keeps the tag taken, and the next singleton
// mint of the tag ends the process.
//
// A split and a combine refuse a token on the erased identity (the mints
// below).  The children of a split would be erased tokens of other tags, and
// the count of the parent says nothing about them.  A singleton root of a
// child tag would then be a second erased token of that tag.
//
// THE REGISTRY.  The live count of a tag must be one cell for the process,
// also when two shared libraries hold the door.  A variable of a template has
// a copy in each library, because the hidden visibility of the tree hides
// each instance, so no template holds a count.  One registry with
// CRUCIBLE_PROCESS_WIDE (foundation/Platform.h) holds a slot for each tag.
// The first mint of a tag claims a slot, the same slot in each library, and
// the tag keeps it for the life of the process.
//
// THE KEY.  The key of a slot has 128 bits, as the key of ThreadLocalRef
// (foundation/ThreadLocalRef.h) has: the stable type id of the tag
// (foundation/reflect/Hash.h), and a second hash of its stable name.  A key
// that meets the type id of a different second word ends the process.  A tag
// with no stable identity, such as a class with internal linkage, has no key,
// so the door refuses it.

namespace detail {

// The slots of the registry.  A raise of the count costs 25 bytes of the
// process for each slot.
inline constexpr std::uint32_t singleton_root_slot_count = 64;

// The state of the key of a slot.
inline constexpr std::uint32_t singleton_slot_free = 0;
inline constexpr std::uint32_t singleton_slot_claiming = 1;
inline constexpr std::uint32_t singleton_slot_published = 2;

// Slot I holds the key of one tag and the live count of that tag: true while
// its singleton root lives.  A thread that claims a free slot writes the key
// and then publishes the slot with release, so a reader that sees the slot
// published with acquire sees the key.  Each thread can take and release a
// live count, and mint_singleton_root and release_singleton_root are its only
// writers.  The exchange of a take acquires what the last release released,
// so a new holder sees each write of the last one.
struct SingletonRootRegistry {
    ::foundation::core::Atomic<std::uint32_t> states[singleton_root_slot_count]{};
    ::foundation::core::Atomic<std::uint64_t> type_ids[singleton_root_slot_count]{};
    ::foundation::core::Atomic<std::uint64_t> name_hashes[singleton_root_slot_count]{};
    ::foundation::core::Atomic<bool> live[singleton_root_slot_count]{};
};

CRUCIBLE_PROCESS_WIDE inline constinit SingletonRootRegistry singleton_root_registry_{};

// The two words of the key of one tag.
struct SingletonRootIds {
    std::uint64_t type_id = 0;
    std::uint64_t name_hash = 0;
};

// A class whose stable name is the stable name of the tag after a fixed
// prefix.  It has no object.
template <typename Tag>
struct singleton_name_of {};

// The key of a tag: its stable type id, which hashes its stable name, and the
// stable type id of singleton_name_of<Tag>, which hashes the same name after
// a fixed prefix.  The prefix gives the hash another state before the name,
// so an accidental collision of the first word does not carry over to the
// second word.
template <typename Tag>
[[nodiscard]] consteval SingletonRootIds singleton_ids_() noexcept {
    return SingletonRootIds{.type_id = ::foundation::reflect::stable_type_id(^^Tag),
                            .name_hash = ::foundation::reflect::stable_type_id(^^singleton_name_of<Tag>)};
}

// The slot of the key in the registry.  The first use of a key claims the
// first free slot.  A thread that finds a slot in the middle of its claim
// waits for its publication, which is a few stores away.  Complexity: linear
// in singleton_root_slot_count, on each mint and each release of a singleton
// root, which are cold.
[[nodiscard, gnu::cold, gnu::noinline]] inline std::uint32_t singleton_slot_of_(SingletonRootIds ids) noexcept {
    SingletonRootRegistry& registry = singleton_root_registry_;
    for (std::uint32_t index = 0; index < singleton_root_slot_count; ++index) {
        ::foundation::core::Atomic<std::uint32_t>& state = registry.states[index];
        if (state.load_acquire() == singleton_slot_free
            && state.cas_acq_rel(singleton_slot_free, singleton_slot_claiming).is_ok()) {
            registry.type_ids[index].store_release(ids.type_id);
            registry.name_hashes[index].store_release(ids.name_hash);
            state.store_release(singleton_slot_published);
            return index;
        }
        while (state.load_acquire() != singleton_slot_published) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (registry.type_ids[index].load_acquire() != ids.type_id) continue;
        if (registry.name_hashes[index].load_acquire() != ids.name_hash) {
            ::foundation::core::fatal("two permission tags have the stable type id {} and two names, so they "
                                      "cannot share a singleton root slot.  Rename one tag.",
                                      ids.type_id);
        }
        return index;
    }
    ::foundation::core::fatal("the singleton root of the permission tag of the stable type id {} finds no free "
                              "slot: the process already has singleton_root_slot_count tags with a singleton root.  "
                              "Raise singleton_root_slot_count of foundation/permissions/Permission.h.",
                              ids.type_id);
}

// The live count of the singleton root of Tag.
template <typename Tag>
[[nodiscard]] ::foundation::core::Atomic<bool>& singleton_live_of_() noexcept {
    const std::uint32_t index = singleton_slot_of_(singleton_ids_<Tag>());
    [[assume(index < singleton_root_slot_count)]];
    return singleton_root_registry_.live[index];
}

// A view of a text in static storage, with a zero after it, so the view
// borrows it for the whole program.  The text has a type that depends on a
// template parameter, so only a unit that names the view instantiates the
// static string (foundation/reflect/Anchor.h).
template <typename Text>
[[nodiscard]] consteval ::foundation::core::TextView static_text_view_(Text const& text) {
    return ::foundation::core::detail::text_over_(std::define_static_string(text), text.size());
}

// The name of the tag, for the report of a refused take or release.
template <typename Tag>
inline constexpr ::foundation::core::TextView singleton_tag_name =
    static_text_view_(std::meta::display_string_of(^^Tag));

[[noreturn]] CRUCIBLE_COLD inline void singleton_root_taken_abort_(::foundation::core::TextView tag) noexcept {
    ::foundation::core::fatal("LinearityViolation: a second live singleton root of the permission tag {} was "
                              "minted.  The erased identity of the tag is one type, so two live roots would be two "
                              "owners of one region.  Release the first root with release_singleton_root before "
                              "the next mint, or keep the brand of mint_permission_root.",
                              tag);
}

[[noreturn]] CRUCIBLE_COLD inline void singleton_root_free_abort_(::foundation::core::TextView tag) noexcept {
    ::foundation::core::fatal("LinearityViolation: a token of the permission tag {} went to release_singleton_root, "
                              "but no singleton root of the tag lives.  The token came from a door other than "
                              "mint_singleton_root.  Give release_singleton_root only the root that "
                              "mint_singleton_root gave.",
                              tag);
}

}  // namespace detail

// Takes the live count of the tag and gives its root on the erased
// identity.  A second call while the first root lives ends the process.
// The context rule is the rule of mint_permission_root.
template <typename Tag, typename... Args>
    requires SingletonRootArgs<Tag, Args...>
[[nodiscard]] ErasedPermission<Tag> mint_singleton_root(Args const&...) noexcept {
    constexpr detail::rows_facts facts =
        sizeof...(Args) == 0 ? detail::rows_facts_of({^^Tag}) : detail::rows_facts_of({});
    if constexpr (!facts.declared) {
        static_assert(has_permission_row(^^Tag), "mint_singleton_root<Tag>: no effect row is declared for Tag, so "
                                                 "nothing says which contexts may own its region.  Declare a "
                                                 "permission_row member on the tag.  A pure tag declares Row<> "
                                                 "explicitly.");
    } else if constexpr (!facts.empty) {
        static_assert(!has_permission_row(^^Tag) || sizeof...(Args) == 1 || permission_row_empty(^^Tag),
                      "mint_singleton_root<Tag>() without an ExecCtx is only valid for permission_row<Tag> == "
                      "Row<>.  Mint an effectful tag with mint_singleton_root<Tag>(ctx) so Ctx admits the row.");
    }
    if (detail::singleton_live_of_<Tag>().exchange_acq_rel(true)) [[unlikely]] {
        detail::singleton_root_taken_abort_(detail::singleton_tag_name<Tag>);
    }
    return ErasedPermission<Tag>{perm_mint_key{}};
}

// Consumes the singleton root of the tag and clears its live count.  A
// token on the erased identity that no singleton mint gave ends the process.
template <typename Tag>
    requires SingletonRootTag<Tag>
void release_singleton_root(ErasedPermission<Tag>&& root) noexcept {
    ::foundation::linear::drop(std::move(root));
    if (!detail::singleton_live_of_<Tag>().exchange_acq_rel(false)) [[unlikely]] {
        detail::singleton_root_free_abort_(detail::singleton_tag_name<Tag>);
    }
}

template <typename Tag, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag, Brand> admit_permission(Ctx const&, Permission<Tag, Brand>&& perm) noexcept {
    return std::move(perm);
}

template <typename Tag, typename Brand, ::foundation::effects::IsExecCtx Ctx>
    requires CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag, Brand> permission_handoff(Ctx const& ctx,
                                                                  Permission<Tag, Brand>&& perm) noexcept {
    return admit_permission(ctx, std::move(perm));
}

template <typename L, typename R, typename... Args>
    requires PermissionSplitArgs<L, R, Args...>
[[nodiscard]] constexpr ::foundation::core::Record<Permission<L, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>,
                                                   Permission<R, detail::perm_brand_t<Args... [sizeof...(Args) - 1]>>>
mint_permission_split(Args&&...) noexcept {
    using In = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    using Brand = detail::perm_brand_t<Args...[sizeof...(Args) - 1]>;
    static_assert(::foundation::brand::IsFreshBrand<Brand>,
                  "mint_permission_split<L, R>: the parent is on the erased identity DefaultBrand.  Its children "
                  "would be erased tokens of L and R, and a singleton root of L or R would then be a second "
                  "erased token of that tag.  Split a token of a brand.");
    // The fit concept proves the row of each tag that a context admits.
    // The facts read the row of each other tag one time: each tag without
    // a context, and the parent beside the two contexts of its children.
    constexpr std::size_t n_ctx = detail::leading_ctx_count({^^Args...});
    constexpr detail::rows_facts facts = n_ctx == 0 ? detail::rows_facts_of({^^In, ^^L, ^^R})
                                       : n_ctx == 2 ? detail::rows_facts_of({^^In})
                                                    : detail::rows_facts_of({});
    if constexpr (!facts.declared) {
        static_assert(has_permission_row(^^In) && has_permission_row(^^L) && has_permission_row(^^R),
                      "mint_permission_split<L, R>: a tag named here declares no effect row.  Declare an edge in "
                      "foundation::permissions::permission_rows or a permission_row member on the tag.");
    } else if constexpr (n_ctx == 0 && !facts.empty) {
        static_assert(!(has_permission_row(^^In) && has_permission_row(^^L) && has_permission_row(^^R))
                          || detail::leading_ctx_count({^^Args...}) != 0
                          || (permission_row_empty(^^In) && permission_row_empty(^^L) && permission_row_empty(^^R)),
                      "mint_permission_split<L, R>(Permission<In>&&) without ExecCtx is "
                      "only valid when parent and child permission rows are Row<>.  Use "
                      "the ctx-bound split overload for row-bearing permission tags.");
    }
    static_assert(can_split_into_v<In, L, R>, "mint_permission_split<L, R>(Permission<In>&&) requires "
                                              "can_split_into<In, L, R>::value to be specialized true.  "
                                              "Declare the split in the same TU that defines the tags.");
    static_assert(has_split_authoring_witness_v<In, L, R>, "can_split_into<In, L, R> is true but the accompanying "
                                                           "has_split_authoring_witness<In, L, R> specialization "
                                                           "is missing.  Every legitimate split ships the witness in "
                                                           "the same TU as the trait.  Add `template <> struct "
                                                           "has_split_authoring_witness<In, L, R> : "
                                                           "std::true_type {};` next to the can_split_into "
                                                           "specialization.");
    static_assert(tags_are_distinct({^^L, ^^R}), "mint_permission_split<L, R> requires L and R to be "
                                                 "DISTINCT region tags.  A manifest declaring "
                                                 "can_split_into<In, A, A> would mint two Permission<A> from one "
                                                 "parent — two linear tokens for the SAME region, aliasing "
                                                 "the very disjointness the CSL frame rule proves.");
    return ::foundation::core::Record<Permission<L, Brand>, Permission<R, Brand>>{
        Permission<L, Brand>{perm_mint_key{}}, Permission<R, Brand>{perm_mint_key{}}};
}

template <typename In, typename... Args>
    requires PermissionCombineArgs<In, Args...>
[[nodiscard]] constexpr Permission<In, detail::perm_brand_t<Args... [sizeof...(Args) - 2]>> mint_permission_combine(
    Args&&...) noexcept {
    using L = detail::perm_tag_t<Args...[sizeof...(Args) - 2]>;
    using R = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    using Brand = detail::perm_brand_t<Args...[sizeof...(Args) - 2]>;
    static_assert(detail::perm_brands_agree({^^Args...}),
                  "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&): the two children carry "
                  "different brands, so they were split from two different regions of one tag.  Only the "
                  "children of one split recombine into their parent.");
    static_assert(::foundation::brand::IsFreshBrand<Brand>,
                  "mint_permission_combine<In>: the children are on the erased identity DefaultBrand.  The parent "
                  "would be an erased token of In, and a singleton root of In would then be a second erased token "
                  "of that tag.  Combine the children of a split of a token of a brand.");
    // With a context, the fit concept proves the row of each tag.  Without
    // one, the facts read each row one time.
    constexpr detail::rows_facts facts = detail::leading_ctx_count({^^Args...}) == 0
                                           ? detail::rows_facts_of({^^In, ^^L, ^^R})
                                           : detail::rows_facts_of({});
    if constexpr (!facts.declared) {
        static_assert(has_permission_row(^^In) && has_permission_row(^^L) && has_permission_row(^^R),
                      "mint_permission_combine<In>: a tag named here declares no effect row.  Declare an edge in "
                      "foundation::permissions::permission_rows or a permission_row member on the tag.");
    } else if constexpr (!facts.empty) {
        static_assert(!(has_permission_row(^^In) && has_permission_row(^^L) && has_permission_row(^^R))
                          || detail::leading_ctx_count({^^Args...}) != 0
                          || (permission_row_empty(^^In) && permission_row_empty(^^L) && permission_row_empty(^^R)),
                      "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                      "without ExecCtx is only valid for Row<> permission tags.");
    }
    static_assert(can_split_into_v<In, L, R>, "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                                              "requires can_split_into<In, L, R>::value true.");
    static_assert(has_split_authoring_witness_v<In, L, R>, "has_split_authoring_witness<In, L, R> missing for "
                                                           "combine; declare it next to the can_split_into "
                                                           "specialization.");
    static_assert(tags_are_distinct({^^L, ^^R}), "mint_permission_combine<In> requires L and R to be DISTINCT "
                                                 "region tags.  Two tokens of one tag folded into a parent "
                                                 "would need two owners of one region, and the split of the "
                                                 "parent refuses such a manifest.");
    return Permission<In, Brand>{perm_mint_key{}};
}

template <typename... Children, typename... Args>
    requires PermissionSplitNArgs<std::tuple<Children...>, Args...>
[[nodiscard]] constexpr ::foundation::core::Record<Permission<
    Children, detail::perm_brand_t<Args...[sizeof...(Args) - 1]>>...> mint_permission_split_n(Args&&...) noexcept {
    using In = detail::perm_tag_t<Args...[sizeof...(Args) - 1]>;
    using Brand = detail::perm_brand_t<Args...[sizeof...(Args) - 1]>;
    static_assert(::foundation::brand::IsFreshBrand<Brand>,
                  "mint_permission_split_n<Children...>: the parent is on the erased identity DefaultBrand.  Its "
                  "children would be erased tokens of their tags, and a singleton root of a child tag would then "
                  "be a second erased token of that tag.  Split a token of a brand.");
    // With a context, the fit concept proves the row of each tag.  Without
    // one, the facts read each row one time.
    constexpr detail::rows_facts facts = detail::leading_ctx_count({^^Args...}) == 0
                                           ? detail::rows_facts_of({^^In, ^^Children...})
                                           : detail::rows_facts_of({});
    if constexpr (!facts.declared) {
        static_assert(has_permission_row(^^In) && (has_permission_row(^^Children) && ...),
                      "mint_permission_split_n<Children...>: a tag named here declares no effect row.  Declare an "
                      "edge in foundation::permissions::permission_rows or a permission_row member on the tag.");
    } else if constexpr (!facts.empty) {
        static_assert(!(has_permission_row(^^In) && (has_permission_row(^^Children) && ...))
                          || detail::leading_ctx_count({^^Args...}) != 0
                          || (permission_row_empty(^^In) && (permission_row_empty(^^Children) && ...)),
                      "mint_permission_split_n<Children...>(Permission<In>&&) without ExecCtx "
                      "is only valid when every permission row is Row<>.  Use the ctx-bound "
                      "split_n overload for row-bearing permission tags.");
    }
    static_assert(can_split_into_pack_v<In, Children...>, "mint_permission_split_n<Children...>(Permission<In>&&) "
                                                          "requires can_split_into_pack<In, Children...>::value true.");
    static_assert(has_split_pack_authoring_witness_v<In, Children...>,
                  "has_split_pack_authoring_witness<In, Children...> "
                  "missing; declare it next to the can_split_into_pack "
                  "specialization in the same TU.");
    static_assert(tags_are_distinct({^^Children...}), "mint_permission_split_n<Children...> requires the "
                                                      "child tags to be PAIRWISE DISTINCT.  A manifest declaring "
                                                      "can_split_into_pack<In, A, A, ...> would mint two Permission<A> "
                                                      "from one parent — aliasing the same region across the "
                                                      "disjoint children the CSL frame rule promises.");
    return ::foundation::core::Record<Permission<Children, Brand>...>{Permission<Children, Brand>{perm_mint_key{}}...};
}

namespace detail {

// What a combine of the children into the parent needs to know about the
// tags.  The facts come from a function that is not a template, so no
// translation unit can specialize them to call a combine declared.  The
// two row facts are the facts of rows_facts over the tags that no context
// admits.
struct combine_n_manifest {
    bool rows_declared = false;
    bool rows_empty = false;
    bool declared = false;
    bool witnessed = false;
    bool distinct = false;
};

// The facts about a combine of the tags of the tag tuple into the parent.
// A combine with a context reads no row, because its fit concept proves
// the row of each tag.  Complexity: quadratic in the number of children,
// for the distinct test.
[[nodiscard]] consteval combine_n_manifest combine_n_facts(std::meta::info parent, std::meta::info tag_tuple,
                                                           bool reads_rows) {
    const std::vector<std::meta::info> children = std::meta::template_arguments_of(std::meta::dealias(tag_tuple));
    std::vector<std::meta::info> manifest_arguments{parent};
    manifest_arguments.insert(manifest_arguments.end(), children.begin(), children.end());
    const std::meta::info* const first = manifest_arguments.data();
    const std::meta::info* const last = reads_rows ? first + manifest_arguments.size() : first;
    const rows_facts rows = rows_facts_of(first, last);
    // A row that is no Row stops the build here, at the first tag of the
    // conjunction of permission_row_empty that reads it.
    if (!rows.empty) {
        for (const std::meta::info* tag = first; tag != last; ++tag) {
            if (!permission_row_empty(*tag)) break;
        }
    }
    return combine_n_manifest{
        .rows_declared = rows.declared,
        .rows_empty = rows.empty,
        .declared = std::meta::extract<bool>(std::meta::substitute(^^can_split_into_pack_v, manifest_arguments)),
        .witnessed =
            std::meta::extract<bool>(std::meta::substitute(^^has_split_pack_authoring_witness_v, manifest_arguments)),
        .distinct = tags_are_distinct_(children),
    };
}

}  // namespace detail

template <typename Parent, typename... Args>
    requires PermissionCombineNArgs<Parent, Args...>
[[nodiscard]] constexpr Permission<Parent, detail::first_perm_brand_t<Args...>>
mint_permission_combine_n(Args&&...) noexcept {
    constexpr detail::combine_n_manifest manifest =
        detail::combine_n_facts(^^Parent, ^^detail::perm_tags_t<Args...>, detail::leading_ctx_count({^^Args...}) == 0);
    using Brand = detail::first_perm_brand_t<Args...>;
    static_assert(detail::perm_brands_agree({^^Args...}),
                  "mint_permission_combine_n<Parent, Children...>(...): the children carry different brands, "
                  "so they were split from different regions of one tag.  Only the children of one split "
                  "recombine into their parent.");
    static_assert(::foundation::brand::IsFreshBrand<Brand>,
                  "mint_permission_combine_n<Parent, Children...>: the children are on the erased identity "
                  "DefaultBrand.  The parent would be an erased token of Parent, and a singleton root of Parent "
                  "would then be a second erased token of that tag.  Combine the children of a split of a token "
                  "of a brand.");
    static_assert(manifest.rows_declared, "mint_permission_combine_n<Parent, Children...>: a tag named here "
                                          "declares no effect row.  Declare an edge in "
                                          "foundation::permissions::permission_rows or a permission_row "
                                          "member on the tag.");
    static_assert(!manifest.rows_declared || detail::leading_ctx_count({^^Args...}) != 0 || manifest.rows_empty,
                  "mint_permission_combine_n<Parent, Children...>(...) without ExecCtx "
                  "is only valid when every permission row is Row<>.");
    static_assert(manifest.declared, "mint_permission_combine_n<Parent, Children...>("
                                     "Permission<Children>&&...) requires "
                                     "can_split_into_pack<Parent, Children...>::value true.  "
                                     "The combine call must mirror the prior split_n; "
                                     "declare the manifest in the same TU as the tags.");
    static_assert(manifest.witnessed, "has_split_pack_authoring_witness<Parent, Children...> "
                                      "missing for combine_n; declare next to the "
                                      "can_split_into_pack specialization.");
    static_assert(manifest.distinct, "mint_permission_combine_n<Parent, Children...> "
                                     "requires the child tags to be PAIRWISE DISTINCT — folding "
                                     "two Permission<A> back into one parent would require two "
                                     "aliasing tokens to have existed.");
    return Permission<Parent, Brand>{perm_mint_key{}};
}

// ── Row-hash identities ─────────────────────────────────────────────
//
// A token makes a claim that no lattice grades, and it names its claim
// with the identity below.  The share, the guard and the pool of
// foundation/permissions/SharedPermission.h fold the same row payload.
// They fold through specializations rather than through published
// members, because the payload is the tag's row, and naming that row in a
// class body would make naming a token over an undeclared tag a hard
// error instead of the constraint failure the mints report.

namespace row_discipline {
struct exclusive;
}  // namespace row_discipline

namespace detail {

// The tag's declared row, or void for a tag that declares none.  Such a
// tag cannot be minted, so no value carries the void; the fallback keeps
// the fold from hard-erroring on a type that is only named.  The answer
// comes from a function that is not a template, so no translation unit
// can give a token a lighter payload in its hash.
[[nodiscard]] consteval std::meta::info row_payload_of_tag(std::meta::info tag) {
    const std::meta::info row = permission_row_source(tag);
    return row == ^^void ? ^^void : checked_row_(tag, row);
}

template <typename Tag>
using row_payload_of_tag_t = [:row_payload_of_tag(^^Tag):];

}  // namespace detail

}  // namespace foundation::permissions

// The tag is the region an instance owns, so it stays out of the hash,
// as it does for SharedPermission.  What the tag implies does fold: a
// token over a region whose row names IO is a different claim from a
// token over a pure region.
namespace foundation::diag {

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::Permission<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::exclusive,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

}  // namespace foundation::diag
