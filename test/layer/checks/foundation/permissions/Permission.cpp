// The compile-time checks of foundation/permissions/Permission.h.

#include <foundation/permissions/Permission.h>

namespace foundation::permissions {

// No route builds the key without a constructor.  std::bit_cast builds
// any trivially copyable type from bytes, and std::start_lifetime_as
// builds any implicit-lifetime type over a buffer, and neither names a
// constructor, so neither meets the access check.  The assertion fails
// if a constructor of the key becomes defaulted again.
static_assert(!std::is_trivially_copyable_v<perm_mint_key> && !std::is_implicit_lifetime_v<perm_mint_key>,
              "perm_mint_key must have no trivial constructor, or std::bit_cast and std::start_lifetime_as "
              "build the key that mints every Permission.");

// The contexts here are the self-test witnesses of foundation/effects/Ctx.h,
// in the shape of the named contexts the layer above defines: a background
// drain row, the same with IO, the test-runner row, and the empty
// foreground row.
using seplog_bg_drain_ctx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
using seplog_bg_compile_ctx = ::foundation::effects::detail::ctx_witnesses::BgIoWitness;
using seplog_test_runner_ctx = ::foundation::effects::detail::ctx_witnesses::TestWitnessCtx;
using seplog_hot_fg_ctx = ::foundation::effects::detail::ctx_witnesses::FgWitness;

// The row relation, read in both directions.  The negative control is
// the tag with no source: under the previous primary template it read
// as a pure tag, and this cell is what would have caught that.
static_assert(has_permission_row(^^detail::seplog_test_tag));
static_assert(permission_row_empty(^^detail::seplog_test_tag));
static_assert(!has_permission_row(^^detail::seplog_undeclared_tag),
              "a tag with no row source must have no row; a relation that answers for it is fail-open");
static_assert(!CtxAdmitsPermission<detail::seplog_undeclared_tag, seplog_test_runner_ctx>,
              "no context admits a tag with no row, not even the widest");
static_assert(!permission_row_empty(^^detail::seplog_io_tag));
static_assert(std::is_same_v<permission_row_t<detail::seplog_member_row_tag>,
                             ::foundation::effects::Row<::foundation::effects::Effect::Block>>,
              "a permission_row member is a row source");
static_assert(std::is_same_v<permission_row_t<detail::seplog_derived_tag>, permission_row_t<detail::seplog_io_tag>>,
              "a derived tag has its parent's row");
static_assert(::foundation::fail_closed::every_class_in_has_edge<^^permission_rows, ^^tag,
                                                                 ::foundation::fail_closed::EdgeEnd::From>(),
              "every canonical permission tag declares its row as an edge");
static_assert(::foundation::fail_closed::every_edge_is_admitted<^^permission_rows>());
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

// True when a new-expression can put a T on the heap by a move.
template <typename T>
inline constexpr bool heap_new_reaches_v = requires { new T(std::declval<T&&>()); };

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
[[nodiscard]] consteval bool token_is_sound() noexcept {
    using Token = Permission<Tag, Brand>;
    return sizeof(Token) == 1 && std::is_trivially_destructible_v<Token> && !std::is_copy_constructible_v<Token>
        && !std::is_copy_assignable_v<Token> && std::is_move_constructible_v<Token>
        && std::is_nothrow_move_constructible_v<Token>
        // No route builds a token without a constructor: std::bit_cast
        // needs a trivially copyable type, and std::start_lifetime_as an
        // implicit-lifetime one.
        && !std::is_trivially_copyable_v<Token>
        && !std::is_implicit_lifetime_v<Token>
        // The key is the sole route in.  Both halves are load-bearing:
        // drop the first and a token is default-constructible by
        // anyone, drop the second and the mints cannot build one.
        && !std::is_default_constructible_v<Token>
        && std::is_constructible_v<Token, perm_mint_key>
        // Explicit, so that a copy of the key cannot convert itself
        // into a token without the construction being written out.
        && !std::is_convertible_v<perm_mint_key, Token>
        // No new-expression puts a token on the heap, and std::optional
        // still holds one in place.
        && !heap_new_reaches_v<Token> && std::is_nothrow_constructible_v<std::optional<Token>, Token&&>;
}

// No conversion drops or changes a brand.  permission_erase_brand is the
// one door to the erased spelling (erasure_keeps_the_tag below).  Nothing
// converts an erased token to a brand, and nothing converts one brand to
// another.
struct brand_a {};
struct brand_b {};
static_assert(!std::is_constructible_v<Permission<seplog_test_tag>, Permission<seplog_test_tag, brand_a>&&>,
              "only permission_erase_brand drops a brand, and no conversion does");
static_assert(!std::is_default_constructible_v<erase_brand_key>, "only permission_erase_brand makes the erasure key");
static_assert(!std::is_constructible_v<Permission<seplog_test_tag, brand_a>, Permission<seplog_test_tag>&&>,
              "an erased token does not acquire a brand");
static_assert(!std::is_constructible_v<Permission<seplog_test_tag, brand_a>, Permission<seplog_test_tag, brand_b>&&>,
              "a token of one brand does not become a token of another");
static_assert(!std::is_constructible_v<Permission<seplog_test_tag>, Permission<seplog_test_tag, brand_a> const&>,
              "erasure consumes the branded token; a copy would leave two");
static_assert(
    !std::is_constructible_v<SharedPermission<seplog_test_tag>, SharedPermission<seplog_test_tag, brand_a> const&>,
    "a branded share does not erase to the unbranded spelling");
static_assert(!std::is_constructible_v<SharedPermission<seplog_test_tag, brand_a>, SharedPermission<seplog_test_tag>>);
static_assert(
    !std::is_constructible_v<SharedPermission<seplog_test_tag, brand_a>, SharedPermission<seplog_test_tag, brand_b>>);

// A translation unit that holds no friendship cannot make a key, so it
// cannot reach the constructor above however it spells the call.
static_assert(!std::is_default_constructible_v<perm_mint_key>,
              "The default constructor of perm_mint_key must not be public.  Only the five mints are "
              "friended to build one.");
static_assert(std::is_empty_v<perm_mint_key>, "perm_mint_key must stay empty, so that passing it costs nothing.");

[[nodiscard]] consteval bool every_canonical_tag_is_sound() noexcept {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^::foundation::permissions::tag, std::meta::access_context::unchecked()));
    bool sound = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && std::meta::is_class_type(member)) {
            using Tag = [:member:];
            if (!PermissionTag<Tag>) sound = false;
            if (!token_is_sound<Tag>()) sound = false;
            if (!token_is_sound<Tag, brand_a>()) sound = false;
            if (!has_permission_row(member)) sound = false;
            if (permission_row_empty(member)) sound = false;
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

// The federation peer tag is a template, so the walk above does not
// reach it.  An instance is a sound effectful token whose one route in
// is the admission key, and the admission key builds no other token.
struct seplog_peer_org {};
using seplog_peer_tag = ::foundation::permissions::tag::FederatedPeer<seplog_peer_org>;
static_assert(PermissionTag<seplog_peer_tag> && has_permission_row(^^seplog_peer_tag)
              && !permission_row_empty(^^seplog_peer_tag));
static_assert(!RootMintableTag<seplog_peer_tag> && RootMintableTag<seplog_test_tag>,
              "the root mint must refuse a federation peer tag and admit every other tag");
static_assert(!std::is_constructible_v<Permission<seplog_peer_tag, brand_a>, perm_mint_key>,
              "no mint key may build a federation peer token; only the admission key does");
static_assert(std::is_constructible_v<Permission<seplog_peer_tag, brand_a>, federation_admission_key>
              && !std::is_convertible_v<federation_admission_key, Permission<seplog_peer_tag, brand_a>>);
static_assert(!std::is_constructible_v<Permission<seplog_test_tag, brand_a>, federation_admission_key>,
              "the admission key must build a federation peer token and no other");
static_assert(!std::is_default_constructible_v<federation_admission_key> && std::is_empty_v<federation_admission_key>,
              "only an admission may build the admission key");

static_assert(token_is_sound<seplog_test_tag>(), "Permission<Tag> must be a 1-byte, trivially destructible, "
                                                 "non-copyable, nothrow-movable token");
static_assert(token_is_sound<seplog_test_tag, brand_a>(), "a branded token keeps the layout of an erased one");

// The root mint brands every call site, and the brand is not spellable.
[[nodiscard]] consteval bool root_brands_are_fresh() noexcept {
    auto first = mint_permission_root<seplog_test_tag>();
    auto second = mint_permission_root<seplog_test_tag>();
    constexpr bool distinct = !std::is_same_v<decltype(first), decltype(second)>;
    constexpr bool branded = ::foundation::brand::IsBranded<decltype(first)>;
    constexpr bool same_tag = IsPermissionFor<decltype(first), seplog_test_tag>;
    return distinct && branded && same_tag;
}

// The one door that drops a brand keeps the tag, and an erased token
// passes through it unchanged.
[[nodiscard]] consteval bool erasure_keeps_the_tag() noexcept {
    auto branded = mint_permission_root<seplog_test_tag>();
    auto erased = permission_erase_brand(std::move(branded));
    auto again = permission_erase_brand(std::move(erased));
    return IsPermissionFor<decltype(again), seplog_test_tag> && ::foundation::brand::IsErased<decltype(again)>;
}

}  // namespace detail::seplog_roster

static_assert(tags_are_distinct({}));
static_assert(tags_are_distinct({^^detail::seplog_test_left}));
static_assert(tags_are_distinct({^^detail::seplog_test_left, ^^detail::seplog_test_right}));
static_assert(tags_are_distinct({^^detail::seplog_test_tag, ^^detail::seplog_test_left, ^^detail::seplog_test_right}));
static_assert(!tags_are_distinct({^^detail::seplog_test_left, ^^detail::seplog_test_left}));
static_assert(!tags_are_distinct({^^detail::seplog_test_tag, ^^detail::seplog_test_left, ^^detail::seplog_test_tag}));
static_assert(!tags_are_distinct({^^detail::seplog_test_left, ^^detail::seplog_test_left,
                                  ^^detail::seplog_test_right}));
// An alias names its target, so a manifest cannot pass one region twice
// under two spellings.
namespace detail::seplog_roster {
using left_alias = seplog_test_left;
}  // namespace detail::seplog_roster
static_assert(!tags_are_distinct({^^detail::seplog_test_left, ^^detail::seplog_roster::left_alias}));

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
static_assert(!detail::seplog_roster::heap_new_reaches_v<
                  SharedPermissionGuard<detail::seplog_test_tag, detail::seplog_roster::brand_a>>,
              "a new-expression must not put a SharedPermissionGuard on the heap");
static_assert(detail::seplog_roster::heap_new_reaches_v<detail::seplog_roster::brand_a>,
              "the heap check must admit a plain movable class, or its refusals prove nothing");

static_assert(!std::is_copy_constructible_v<SharedPermissionPool<detail::seplog_test_tag>>,
              "SharedPermissionPool<Tag> must be Pinned (non-copyable)");
static_assert(!std::is_move_constructible_v<SharedPermissionPool<detail::seplog_test_tag>>,
              "SharedPermissionPool<Tag> must be Pinned (non-movable)");

static_assert(IsPermission<Permission<detail::seplog_test_tag>>);
static_assert(IsPermission<Permission<detail::seplog_test_tag>&&>);
static_assert(IsPermission<const Permission<detail::seplog_test_tag>&>);
static_assert(IsPermission<Permission<detail::seplog_test_tag, detail::seplog_roster::brand_a>>);
static_assert(!IsPermission<int>);
static_assert(!IsPermission<SharedPermission<detail::seplog_test_tag>>);
static_assert(!IsPermission<SharedPermissionGuard<detail::seplog_test_tag>>);

static_assert(IsSharedPermission<SharedPermission<detail::seplog_test_tag>>);
static_assert(IsSharedPermission<const SharedPermission<detail::seplog_test_tag>&>);
static_assert(!IsSharedPermission<int>);
static_assert(!IsSharedPermission<Permission<detail::seplog_test_tag>>);
static_assert(!IsSharedPermission<SharedPermissionGuard<detail::seplog_test_tag>>);
static_assert(IsPermissionFor<Permission<detail::seplog_test_tag>, detail::seplog_test_tag>);
static_assert(
    IsPermissionFor<Permission<detail::seplog_test_tag, detail::seplog_roster::brand_a>, detail::seplog_test_tag>);
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

// The brand agreement a combine demands: two children of one brand
// agree, a context in front does not disturb the count, and children
// of two brands do not.
static_assert(detail::perm_brands_agree({^^Permission<detail::seplog_test_left, detail::seplog_roster::brand_a>,
                                         ^^Permission<detail::seplog_test_right, detail::seplog_roster::brand_a>}));
static_assert(detail::perm_brands_agree({^^seplog_bg_drain_ctx const&,
                                         ^^Permission<detail::seplog_test_left, detail::seplog_roster::brand_a>,
                                         ^^Permission<detail::seplog_test_right, detail::seplog_roster::brand_a>}));
static_assert(!detail::perm_brands_agree({^^Permission<detail::seplog_test_left, detail::seplog_roster::brand_a>,
                                          ^^Permission<detail::seplog_test_right, detail::seplog_roster::brand_b>}));

namespace detail {
constexpr bool combine_n_round_trip() noexcept {
    auto whole = mint_permission_root<seplog_combine_n_parent>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    auto [a, b, c] =
        mint_permission_split_n<seplog_combine_n_a, seplog_combine_n_b, seplog_combine_n_c>(std::move(whole));
    // The children carry the parent's brand, and the rebuilt parent
    // carries it back out.
    static_assert(std::is_same_v<::foundation::brand::brand_of_t<decltype(a)>, WholeBrand>);
    static_assert(std::is_same_v<::foundation::brand::brand_of_t<decltype(c)>, WholeBrand>);
    auto rebuilt = mint_permission_combine_n<seplog_combine_n_parent>(std::move(a), std::move(b), std::move(c));
    static_assert(std::is_same_v<::foundation::brand::brand_of_t<decltype(rebuilt)>, WholeBrand>);
    (void)rebuilt;
    return true;
}
static_assert(combine_n_round_trip());
static_assert(seplog_roster::root_brands_are_fresh());
static_assert(seplog_roster::erasure_keeps_the_tag());
}  // namespace detail

}  // namespace foundation::permissions
