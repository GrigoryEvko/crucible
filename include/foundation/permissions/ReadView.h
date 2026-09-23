#pragma once

// The read borrow of the permission family, alongside the exclusive
// token and the pooled share.  It carries no runtime state, because it
// needs none: the borrow is scoped to a call, so the permission it was
// minted from necessarily outlives it and there is nothing to count.  A
// borrow that escapes to a consumer which may outlive the producer, or
// one that has to be turned back into an exclusive, wants the pooled
// share instead.
//
// A view carries the brand of the permission it was minted from, so it
// proves something about that region and no other region of the same
// tag.  A view minted from a branded permission cannot be presented
// where a proof about a different instance is wanted: the brands fail
// to unify, and the compiler refuses by deduction.  That is a second
// refusal beside the deleted rvalue twin below, and the two catch
// different mistakes: the twin refuses a borrow of a temporary, and
// the brand refuses a borrow of the wrong object.
//
// Old spelling: include/crucible/permissions/ReadView.h, namespace
// crucible::safety.

#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/RowHash.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::permissions {

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class ReadView;

// The row gate, and the one the family's other read borrow already
// carries.  A view is a proof about a region, so minting one is bounded
// by who may hold that region.  A factory that reads no context can be
// sound only for the empty row, which is exactly the rule
// SharedPermissionPool::lend states for the pooled share of the very
// same region.  This factory stated it nowhere, so a borrow proof of a
// region whose row names IO was mintable from a scope that declared no
// context at all.
//
// permission_row_empty_v answers false for a tag that declares no row,
// so an undeclared tag is refused here rather than admitted by
// omission.
//
// There is no ctx-bound overload beside this one, because nothing
// borrows an effectful region this way.  SharedPermissionPool::lend(ctx)
// is the read borrow that already reads a context, and it is what an
// effectful region wants until a second case asks for this shape.
template <typename Tag>
concept ReadViewNeedsNoCtx = permission_row_empty_v<Tag>;

// The view inherits the permission's brand rather than minting one of
// its own: a borrow proof is about the region it was borrowed from.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr ReadView<Tag, Brand> mint_read_view(Permission<Tag, Brand> const& p
                                                            CRUCIBLE_LIFETIMEBOUND) noexcept;

// The twin that makes the lifetime bound above a rule rather than a
// claim.  A const lvalue reference binds a temporary, so without this
// overload `mint_read_view(mint_permission_root<Tag>())` compiled and
// handed back a borrow proof for a permission that died at the end of
// the statement.  That was measured, not suspected.  The rvalue
// reference is the better match for a prvalue, so the call now names a
// deleted function instead.
//
// The twin carries the same row gate, so an effectful region is refused
// by the gate that names the row rather than by the twin, whose message
// names the lifetime instead.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
constexpr ReadView<Tag, Brand> mint_read_view(Permission<Tag, Brand> const&&) =
    delete("a borrow proof minted from a temporary permission outlives what it proves; bind the permission to "
           "a name that outlives the view");

// The second door: a live share of a pool.  The share lives in the
// guard, not in the SharedPermission token, because a token confers
// nothing and a copy of it can outlive the share (Permission.h).  So a
// view comes from a guard, and only from a guard that still holds its
// share.  The deleted twin refuses a guard that dies at the end of the
// statement, as the twin above refuses a temporary permission.
//
// There is no third source.  A read proof comes from a Permission or
// from a live share, and from nothing else.  The session layer builds the
// read proof of its borrow payload through these two mints, so it needs
// no friend here.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr ReadView<Tag, Brand> mint_read_view(SharedPermissionGuard<Tag, Brand> const& g
                                                            CRUCIBLE_LIFETIMEBOUND) noexcept;

template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
constexpr ReadView<Tag, Brand> mint_read_view(SharedPermissionGuard<Tag, Brand> const&&) =
    delete("a borrow proof minted from a temporary share guard outlives the share. Bind the guard to a name that "
           "outlives the view");

template <typename Tag, typename Brand>
class [[nodiscard]] ReadView {
    static_assert(::foundation::brand::IsBrand<Brand>, "ReadView<Tag, Brand>: Brand must be an empty class type: "
                                                       "the brand of the permission the view was minted from, or "
                                                       "DefaultBrand.");

public:
    using tag_type = Tag;
    using brand_type = Brand;

    constexpr ReadView(const ReadView&) noexcept = default;
    constexpr ReadView(ReadView&&) noexcept = default;

    // Erasure, one way only: a view of one instance becomes a view on
    // the erased identity, so code written before brands keeps
    // compiling.  Nothing gives an erased view a brand.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr ReadView(ReadView<Tag, Other> const&) noexcept {}

    ReadView& operator=(const ReadView&) = delete(
        "ReadView is single-binding; rebinding hides lifetime relationships — construct a fresh view via mint_read_view");
    ReadView& operator=(ReadView&&) = delete("ReadView is single-binding; rebinding hides lifetime relationships");

    ~ReadView() = default;

    static void* operator new(std::size_t) =
        delete("ReadView must live on the stack; heap allocation defeats the lifetime contract");
    static void* operator new[](std::size_t) = delete("ReadView arrays on the heap defeat the lifetime contract");
    static void* operator new(std::size_t, std::align_val_t) = delete("ReadView must live on the stack");
    static void* operator new[](std::size_t,
                                std::align_val_t) = delete("ReadView arrays on the heap defeat the lifetime contract");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;

private:
    // A borrow proof that can be conjured from nothing is not a proof, so
    // only the issuers below reach this.  Copy and move stay public, which
    // closes construction from nothing without hindering an already minted
    // view being carried around.
    constexpr ReadView() noexcept = default;

    // Every entry in the friend list below is another way to mint a
    // borrow.  Additions need review.

    // The friend is the constrained template rather than this Tag and
    // Brand's specialization of it, which is the shape fixy/ScopedView.h
    // already uses for the same reason.  Naming the specialization makes
    // the row gate a condition on instantiating THIS CLASS: a template-id
    // whose constraint answers false matches no declaration, so
    // `ReadView<EffectfulTag>` stopped being nameable at all and the
    // refusal arrived from the friend list rather than from the mint.
    // Measured, then repaired.
    //
    // The widening is nominal.  Every specialization of the factory is
    // now a friend of every ReadView, and each one still builds only the
    // ReadView its own Tag and Brand name, so no construction path
    // exists that did not exist before.
    template <typename Tag_, typename Brand_>
        requires ReadViewNeedsNoCtx<Tag_>
    friend constexpr ReadView<Tag_, Brand_> mint_read_view(Permission<Tag_, Brand_> const& p
                                                           CRUCIBLE_LIFETIMEBOUND) noexcept;

    template <typename Tag_, typename Brand_>
        requires ReadViewNeedsNoCtx<Tag_>
    friend constexpr ReadView<Tag_, Brand_> mint_read_view(SharedPermissionGuard<Tag_, Brand_> const& g
                                                           CRUCIBLE_LIFETIMEBOUND) noexcept;
};

// The annotation on the parameter is the claim.  The deleted twin
// declared beside the first declaration is what enforces it, because no
// compiler this project builds with honours a lifetime attribute.

template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr ReadView<Tag, Brand> mint_read_view(Permission<Tag, Brand> const& p
                                                            CRUCIBLE_LIFETIMEBOUND) noexcept {
    (void)p;
    return ReadView<Tag, Brand>{};
}

// A guard that was moved from holds no share, so a view from it would
// prove a share that nothing counts.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr ReadView<Tag, Brand> mint_read_view(SharedPermissionGuard<Tag, Brand> const& g
                                                            CRUCIBLE_LIFETIMEBOUND) noexcept {
    CRUCIBLE_PRE(g.holds_share());
    return ReadView<Tag, Brand>{};
}

// The scoped form is a second door onto the borrow of a Permission, so
// it carries the same row gate.  Without it the refusal still happened, but inside
// this header on the call below, and a body that returns the view it was
// handed would have borrowed an effectful region through a factory that
// never mentioned one.
template <typename Tag, typename Brand, typename Body>
    requires ReadViewNeedsNoCtx<Tag> && std::is_invocable_v<Body, ReadView<Tag, Brand>>
[[nodiscard]] constexpr auto
with_read_view(Permission<Tag, Brand> const& p CRUCIBLE_LIFETIMEBOUND,
               Body&& body) noexcept(std::is_nothrow_invocable_v<Body, ReadView<Tag, Brand>>)
    -> std::invoke_result_t<Body, ReadView<Tag, Brand>> {
    return body(mint_read_view(p));
}

// The same twin for the scoped form.  The body runs while the view is
// alive, so a temporary permission survives the call, but the view the
// body receives proves a permission that is gone the moment the
// statement ends, and a body that stores the view keeps the proof.
template <typename Tag, typename Brand, typename Body>
    requires ReadViewNeedsNoCtx<Tag> && std::is_invocable_v<Body, ReadView<Tag, Brand>>
constexpr auto with_read_view(Permission<Tag, Brand> const&&, Body&&) =
    delete("a borrow proof minted from a temporary permission outlives what it proves; bind the permission to "
           "a name that outlives the call");

namespace detail {
struct read_view_test_tag {};
struct read_view_brand_a {};
struct read_view_brand_b {};
// Two tags the row gate must refuse, kept beside the one it admits: a
// region whose row names an effect, and a region that declares no row.
struct read_view_effectful_tag {};
struct read_view_rowless_tag {};
}  // namespace detail

namespace permission_rows {
inline constexpr ::foundation::fail_closed::edge<detail::read_view_test_tag, ::foundation::effects::Row<>>
    read_view_test{};
inline constexpr ::foundation::fail_closed::edge<detail::read_view_effectful_tag,
                                                 ::foundation::effects::Row<::foundation::effects::Effect::IO>>
    read_view_effectful{};
}  // namespace permission_rows

static_assert(sizeof(ReadView<detail::read_view_test_tag>) == 1, "ReadView<Tag> must be a 1-byte empty class");
static_assert(sizeof(ReadView<detail::read_view_test_tag, detail::read_view_brand_a>) == 1,
              "a branded view keeps the layout of an erased one");

static_assert(std::is_trivially_copyable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must be trivially copyable (zero-cost copy)");
static_assert(std::is_trivially_destructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> destructor must be trivial");

static_assert(std::is_copy_constructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> MUST be copy-constructible (multi-reader semantics)");
static_assert(!std::is_copy_assignable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must NOT be copy-assignable (single-binding)");
static_assert(std::is_move_constructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> MUST be move-constructible");
static_assert(!std::is_move_assignable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must NOT be move-assignable (single-binding)");

// The erasure runs one way, and a view of one brand is not a view of
// another.
static_assert(std::is_convertible_v<ReadView<detail::read_view_test_tag, detail::read_view_brand_a>,
                                    ReadView<detail::read_view_test_tag>>,
              "a branded view erases to the unbranded spelling");
static_assert(!std::is_constructible_v<ReadView<detail::read_view_test_tag, detail::read_view_brand_a>,
                                       ReadView<detail::read_view_test_tag>>,
              "an erased view does not acquire a brand");
static_assert(!std::is_constructible_v<ReadView<detail::read_view_test_tag, detail::read_view_brand_a>,
                                       ReadView<detail::read_view_test_tag, detail::read_view_brand_b>>,
              "a view of one brand is not a view of another");

namespace detail::read_view_self_test {

// A view minted from a branded permission carries that permission's
// brand and no other.
[[nodiscard]] consteval bool view_inherits_the_permission_brand() noexcept {
    auto perm = mint_permission_root<read_view_test_tag>();
    auto view = mint_read_view(perm);
    return std::is_same_v<::foundation::brand::brand_of_t<decltype(view)>,
                          ::foundation::brand::brand_of_t<decltype(perm)>>
        && ::foundation::brand::IsBranded<decltype(view)>;
}
static_assert(view_inherits_the_permission_brand());

// ── The row gate answers, on both doors onto the borrow ──────────────
//
// Every assertion below read true before ReadViewNeedsNoCtx, for both
// the bare mint and the scoped form.  A region whose row names IO was
// borrowable with no context named anywhere, and a region that declares
// no row was borrowable by omission.
template <typename Tag>
concept ReadViewMintable = requires(Permission<Tag> const& p) { mint_read_view(p); };

template <typename Tag>
concept ReadViewScopable = requires(Permission<Tag> const& p) { with_read_view(p, [](ReadView<Tag>) noexcept {}); };

static_assert(ReadViewMintable<read_view_test_tag>, "a pure region stays borrowable with no context");
static_assert(!ReadViewMintable<read_view_effectful_tag>,
              "a region whose row names an effect must not be borrowable with no context");
static_assert(!ReadViewMintable<read_view_rowless_tag>,
              "a region that declares no row must be refused rather than admitted by omission");

static_assert(ReadViewScopable<read_view_test_tag>, "the scoped form keeps the same admission");
static_assert(!ReadViewScopable<read_view_effectful_tag>, "the scoped form is not a way around the row gate");
static_assert(!ReadViewScopable<read_view_rowless_tag>, "the scoped form fails closed on a missing row too");

}  // namespace detail::read_view_self_test

namespace row_discipline {
struct read_view;
}  // namespace row_discipline

}  // namespace foundation::permissions

// A borrow of a region folds the region's row as its payload, as the
// tokens in Permission.h do, so a view over an IO region is not a view
// over a pure one.
namespace foundation::diag {

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::ReadView<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::read_view,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

}  // namespace foundation::diag
