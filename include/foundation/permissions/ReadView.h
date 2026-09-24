#pragma once

// The read borrow of the permission family.
//
// A read proof exists only inside a callback.  with_read_view takes a
// source by value category rvalue, keeps it in its own frame, gives the
// body a ReadView by const reference, and hands the source back when the
// body returns.  The view is not copyable and not movable, and only the
// door constructs one, so no view lives past the frame of its door.
// While the body runs, the caller holds no name for the source, so the
// body cannot move it, end it, or upgrade it.  This is the callback
// shape of a borrow (Thiemann, ICFP 2023), and the frame rule of
// separation logic: the door frames the source out and frames it back.
//
// A source is one of three things:
//
//   * a Permission, the exclusive token of a region
//   * a SharedPermissionGuard, a live share of a pool
//   * a ReadLoan, a read right that travels to a reader on another thread.
//
// A ReadLoan comes from mint_read_loan, which parks the Permission in a
// LentPermission.  The parked token comes back only through
// mint_permission_after_loan, which consumes the loan too.  So the loan
// and the token never exist together.  These are the borrow and the
// inheritance of the lifetime logic of RustBelt (Jung, Jourdan,
// Krebbers and Dreyer, POPL 2018): the loan is the borrow, and the
// parked token comes back when the loan ends.
//
// A view carries the brand of its source, so it proves something about
// that region and no other region of the same tag.  A body that asks for
// the erased spelling gets an erased view instead.
//
// What stays open, stated rather than implied: a body can store a
// pointer or a reference to its view in an object outside the frame.
// That pointer dangles when the door returns, and a read through it is
// undefined behavior, which no property of a type refuses.  A result
// that names a view is refused, so the dangling pointer cannot leave
// through the return value.
//
// Old spelling: include/crucible/permissions/ReadView.h, namespace
// crucible::safety.

#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/RowHash.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/TypeComponents.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::permissions {

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class ReadView;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class ReadLoan;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class LentPermission;

// The row gate.  A view is a proof about a region, so a borrow is
// bounded by who may hold that region.  A door that reads no context is
// sound only for the empty row, which is the rule that
// SharedPermissionPool::lend states for the pooled share of the same
// region.  permission_row_empty_v answers false for a tag that declares
// no row, so an undeclared tag is refused and not admitted by omission.
// An effectful region borrows through SharedPermissionPool::lend(ctx).
template <typename Tag>
concept ReadViewNeedsNoCtx = permission_row_empty_v<Tag>;

// ── The loan mints ───────────────────────────────────────────────────

// Parks the token and gives the read loan of its region.  The token
// comes back only with the loan, through mint_permission_after_loan.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr std::pair<ReadLoan<Tag, Brand>, LentPermission<Tag, Brand>>
mint_read_loan(Permission<Tag, Brand>&& token) noexcept;

// A named token stays with the caller, so a loan of it would exist
// beside the token.  The deleted twin refuses it with the reason.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
constexpr void mint_read_loan(Permission<Tag, Brand> const&) =
    delete("mint_read_loan parks the token until the loan comes back.  Pass the token with std::move");

// Ends a loan: consumes the loan and gives back the parked token.  The
// brands of the two must agree, so a loan of one region cannot end the
// loan of another region.  It carries the row gate of mint_read_loan,
// because no loan of another tag exists to end.
template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr Permission<Tag, Brand> mint_permission_after_loan(LentPermission<Tag, Brand>&& lent,
                                                                          ReadLoan<Tag, Brand>&& loan) noexcept;

namespace detail {

// The only code that constructs a ReadView.  It has no member that
// returns a view: a view exists only as a local of lend, for the time
// that the body runs.
struct read_view_door;

// The class templates whose specializations can lend a read view.
inline constexpr std::meta::info read_view_source_families[] = {
    ^^Permission,
    ^^SharedPermissionGuard,
    ^^ReadLoan,
};

[[nodiscard]] consteval bool is_read_view_source(std::meta::info type) {
    const std::meta::info bare = std::meta::dealias(type);
    if (!std::meta::has_template_arguments(bare)) return false;
    const std::meta::info family = std::meta::template_of(bare);
    for (const std::meta::info source_family : read_view_source_families) {
        if (family == source_family) return true;
    }
    return false;
}

}  // namespace detail

// A source that a door can take: a non-const object of one of the three
// families, passed as an rvalue.
template <typename Source>
concept ReadViewSource =
    !std::is_reference_v<Source> && !std::is_const_v<Source> && detail::is_read_view_source(^^Source);

// A body takes the view by const reference, either the branded view of
// the source or the erased view.  A body that takes the view by value
// asks for a copy, and the view has none.
template <typename Body, typename Tag, typename Brand>
concept ReadViewBody =
    std::is_invocable_v<Body, ReadView<Tag, Brand> const&> || std::is_invocable_v<Body, ReadView<Tag> const&>;

namespace detail {

template <typename Body, typename Tag, typename Brand>
using read_view_for_body_t =
    std::conditional_t<std::is_invocable_v<Body, ReadView<Tag, Brand> const&>, ReadView<Tag, Brand>, ReadView<Tag>>;

template <typename Body, typename Tag, typename Brand>
using read_view_result_t = std::invoke_result_t<Body, read_view_for_body_t<Body, Tag, Brand> const&>;

// True for a node that names a ReadView, and for a class whose state the
// walk cannot read.  GCC 16 reflects no capture of a lambda, so a lambda
// with captures is a complete class that is not empty and shows no base
// and no member.
inline constexpr auto names_a_read_view = [](::foundation::reflect::TypeNode node) consteval {
    const std::meta::info type = node.type;
    if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^ReadView) return true;
    if (!node.may_read_members || !std::meta::is_class_type(type)) return false;
    if (std::meta::has_template_arguments(type) || std::meta::is_empty_type(type)) return false;
    const auto unchecked = std::meta::access_context::unchecked();
    return std::meta::bases_of(type, unchecked).empty() && std::meta::nonstatic_data_members_of(type, unchecked).empty();
};

}  // namespace detail

// What a body returns leaves the frame of the door, so it must not take
// the view with it.  A reference result is refused, because a reference
// that leaves the frame names something that the result does not own.
// A result that names a ReadView through a pointer, a member or a
// template argument is refused, and so is a lambda whose captures the
// walk cannot read.
template <typename R>
concept ReadViewResultStaysInside =
    std::is_void_v<R>
    || (!std::is_reference_v<R> && !::foundation::reflect::any_component_satisfies<detail::names_a_read_view>(^^R));

// ── The door ─────────────────────────────────────────────────────────

namespace detail {

// The door throws only if the body throws, or if the move of its result
// throws.
template <typename Source, typename Body>
inline constexpr bool read_view_door_nothrow_v = [] {
    using Result = read_view_result_t<Body, typename Source::tag_type, typename Source::brand_type>;
    using View = read_view_for_body_t<Body, typename Source::tag_type, typename Source::brand_type>;
    if constexpr (std::is_void_v<Result>) {
        return std::is_nothrow_invocable_v<Body, View const&>;
    } else {
        return std::is_nothrow_invocable_v<Body, View const&> && std::is_nothrow_move_constructible_v<Result>;
    }
}();

struct read_view_door {
    // Complexity: the cost of the body.  The frame holds the source and
    // one empty view, so the door adds no instruction when it is inlined.
    template <typename Source, typename Body>
    [[nodiscard]] static constexpr auto lend(Source&& source,
                                             Body&& body) noexcept(read_view_door_nothrow_v<Source, Body>) {
        using Tag = typename Source::tag_type;
        using Brand = typename Source::brand_type;
        using View = read_view_for_body_t<Body, Tag, Brand>;
        using Result = read_view_result_t<Body, Tag, Brand>;
        static_assert(ReadViewResultStaysInside<Result>,
                      "with_read_view: the body returns a reference, a type that names a ReadView, or a lambda "
                      "whose captures cannot be read.  The result leaves the frame of the door, so return a "
                      "value that does not hold the view");
        if constexpr (std::is_same_v<Source, SharedPermissionGuard<Tag, Brand>>) {
            CRUCIBLE_PRE(source.holds_share());
        }
        Source held{std::move(source)};
        View const view{};
        if constexpr (std::is_void_v<Result>) {
            std::forward<Body>(body)(view);
            return held;
        } else {
            return std::pair<Result, Source>{std::forward<Body>(body)(view), std::move(held)};
        }
    }
};

}  // namespace detail

// Lends the source to the body and hands it back.  A body that returns
// nothing gives the source back.  A body that returns a value gives a
// pair of that value and the source.
template <typename Source, typename Body>
    requires ReadViewSource<Source> && ReadViewNeedsNoCtx<typename Source::tag_type>
             && ReadViewBody<Body, typename Source::tag_type, typename Source::brand_type>
[[nodiscard]] constexpr auto with_read_view(Source&& source,
                                            Body&& body) noexcept(detail::read_view_door_nothrow_v<Source, Body>) {
    return detail::read_view_door::lend(std::move(source), std::forward<Body>(body));
}

// A named source stays with the caller while the body runs, so the body
// could move it or end it through a capture.  The deleted twin refuses a
// named source, and a const one, which cannot be moved into the frame.
// It carries the row gate, so an effectful region is refused by the gate
// that names the row.
template <typename Source, typename Body>
    requires(std::is_lvalue_reference_v<Source> || std::is_const_v<std::remove_reference_t<Source>>)
            && ReadViewSource<std::remove_cvref_t<Source>>
            && ReadViewNeedsNoCtx<typename std::remove_cvref_t<Source>::tag_type>
constexpr void with_read_view(Source&&, Body&&) =
    delete("with_read_view lends its source to the body and hands the source back.  Pass the source with "
           "std::move, and keep the source that the call returns");

// ── The view ─────────────────────────────────────────────────────────

template <typename Tag, typename Brand>
class [[nodiscard]] ReadView {
    static_assert(::foundation::brand::IsBrand<Brand>, "ReadView<Tag, Brand>: Brand must be an empty class type: "
                                                       "the brand of the source of the view, or DefaultBrand.");

public:
    using tag_type = Tag;
    using brand_type = Brand;

    // No copy and no move, so no view leaves the frame of its door.  A
    // copy or a move could build a view inside a longer-lived object,
    // for example through std::optional::emplace.  With all four deleted
    // and the default constructor private, no constructor is trivial, so
    // std::bit_cast and std::start_lifetime_as refuse the type too.
    ReadView(const ReadView&) = delete("a ReadView lives in the frame of its door. A copy could outlive the source");
    ReadView(ReadView&&) = delete("a ReadView lives in the frame of its door. A move could carry it out of the frame");
    ReadView& operator=(const ReadView&) = delete("a ReadView binds one frame. Assignment would rebind it");
    ReadView& operator=(ReadView&&) = delete("a ReadView binds one frame. Assignment would rebind it");

    // User-provided, and not defaulted.  With every copy and move
    // deleted, GCC 16 still calls the class trivially copyable, because
    // it counts a deleted member as trivial, and std::bit_cast then
    // builds a view from a byte.  A destructor that is not trivial
    // refuses std::bit_cast and std::start_lifetime_as.  It compiles to
    // nothing, because the class is empty.
    constexpr ~ReadView() {}

    static void* operator new(std::size_t) = delete("a ReadView lives in the frame of its door, not on the heap");
    static void* operator new[](std::size_t) = delete("a ReadView lives in the frame of its door, not on the heap");
    static void* operator new(std::size_t, std::align_val_t) =
        delete("a ReadView lives in the frame of its door, not on the heap");
    static void* operator new[](std::size_t, std::align_val_t) =
        delete("a ReadView lives in the frame of its door, not on the heap");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
    static void operator delete(void*, std::align_val_t) = delete;
    static void operator delete[](void*, std::align_val_t) = delete;

private:
    // A proof that is built from nothing is not a proof.  Only the door
    // reaches this constructor.
    constexpr ReadView() noexcept {}

    friend struct detail::read_view_door;
};

// ── The loan ─────────────────────────────────────────────────────────

// One read right over a region whose token is parked.  A loan moves, so
// it can travel in a message to a reader on another thread, and it does
// not copy, so one parked token has one loan.  A view comes from a loan
// only through with_read_view.
template <typename Tag, typename Brand>
class [[nodiscard]] ReadLoan {
    static_assert(PermissionTag<Tag>, "ReadLoan<Tag>: Tag must be an empty non-union class type, as a "
                                      "Permission tag is");
    static_assert(::foundation::brand::IsBrand<Brand>, "ReadLoan<Tag, Brand>: Brand must be an empty class type: "
                                                       "the brand of the parked token, or DefaultBrand.");

public:
    using tag_type = Tag;
    using brand_type = Brand;

    // Erasure, one way only, as on Permission.  It consumes the branded
    // loan.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr ReadLoan(ReadLoan<Tag, Other>&&) noexcept {}

    ReadLoan(const ReadLoan&) = delete("a ReadLoan is one read right. A copy is a second right that the parked "
                                       "token does not wait for");
    ReadLoan& operator=(const ReadLoan&) = delete("a ReadLoan is one read right. A copy is a second right that "
                                                  "the parked token does not wait for");
    // User-provided, and not defaulted, so that no constructor is
    // trivial: std::bit_cast and std::start_lifetime_as then refuse the
    // type.  The move assignment stays defaulted, as on Permission.
    constexpr ReadLoan(ReadLoan&&) noexcept {}
    constexpr ReadLoan& operator=(ReadLoan&&) noexcept = default;
    ~ReadLoan() = default;

private:
    constexpr ReadLoan() noexcept {}

    template <typename Tag_, typename Brand_>
        requires ReadViewNeedsNoCtx<Tag_>
    friend constexpr std::pair<ReadLoan<Tag_, Brand_>, LentPermission<Tag_, Brand_>>
    mint_read_loan(Permission<Tag_, Brand_>&& token) noexcept;
};

// The token of a region while its read loan is out.  It holds the token
// and gives no access to it.  Only mint_permission_after_loan, which
// takes the loan too, gives the token back.
template <typename Tag, typename Brand>
class [[nodiscard]] LentPermission {
public:
    using tag_type = Tag;
    using brand_type = Brand;

    // Erasure, one way only, as on Permission.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr LentPermission(LentPermission<Tag, Other>&& other) noexcept : parked_{std::move(other.parked_)} {}

    LentPermission(const LentPermission&) = delete("a LentPermission holds a linear token. A copy is a second owner");
    LentPermission& operator=(const LentPermission&) =
        delete("a LentPermission holds a linear token. A copy is a second owner");
    constexpr LentPermission(LentPermission&& other) noexcept : parked_{std::move(other.parked_)} {}
    LentPermission& operator=(LentPermission&&) =
        delete("a LentPermission binds one loan. Assignment would drop the token it holds");
    ~LentPermission() = default;

private:
    constexpr explicit LentPermission(Permission<Tag, Brand>&& token) noexcept : parked_{std::move(token)} {}

    [[no_unique_address]] Permission<Tag, Brand> parked_;

    template <typename, typename>
    friend class LentPermission;

    template <typename Tag_, typename Brand_>
        requires ReadViewNeedsNoCtx<Tag_>
    friend constexpr std::pair<ReadLoan<Tag_, Brand_>, LentPermission<Tag_, Brand_>>
    mint_read_loan(Permission<Tag_, Brand_>&& token) noexcept;

    template <typename Tag_, typename Brand_>
        requires ReadViewNeedsNoCtx<Tag_>
    friend constexpr Permission<Tag_, Brand_> mint_permission_after_loan(LentPermission<Tag_, Brand_>&& lent,
                                                                         ReadLoan<Tag_, Brand_>&& loan) noexcept;
};

template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr std::pair<ReadLoan<Tag, Brand>, LentPermission<Tag, Brand>>
mint_read_loan(Permission<Tag, Brand>&& token) noexcept {
    return {ReadLoan<Tag, Brand>{}, LentPermission<Tag, Brand>{std::move(token)}};
}

template <typename Tag, typename Brand>
    requires ReadViewNeedsNoCtx<Tag>
[[nodiscard]] constexpr Permission<Tag, Brand> mint_permission_after_loan(LentPermission<Tag, Brand>&& lent,
                                                                          ReadLoan<Tag, Brand>&& loan) noexcept {
    ReadLoan<Tag, Brand> ended{std::move(loan)};
    (void)ended;
    return std::move(lent.parked_);
}

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
static_assert(sizeof(ReadLoan<detail::read_view_test_tag>) == 1, "a loan carries no runtime state");
static_assert(sizeof(LentPermission<detail::read_view_test_tag>) == 1, "a parked token carries no runtime state");

// No route builds a view, a loan or a parked token without a
// constructor.  std::bit_cast needs a trivially copyable type, and
// std::start_lifetime_as needs an implicit-lifetime type.
static_assert(!std::is_trivially_copyable_v<ReadView<detail::read_view_test_tag>>);
static_assert(!std::is_implicit_lifetime_v<ReadView<detail::read_view_test_tag>>);
static_assert(!std::is_trivially_copyable_v<ReadLoan<detail::read_view_test_tag>>);
static_assert(!std::is_implicit_lifetime_v<ReadLoan<detail::read_view_test_tag>>);
static_assert(!std::is_trivially_copyable_v<LentPermission<detail::read_view_test_tag>>);
static_assert(!std::is_implicit_lifetime_v<LentPermission<detail::read_view_test_tag>>);
static_assert(!std::is_default_constructible_v<ReadView<detail::read_view_test_tag>>);

// A view does not copy and does not move, so it cannot leave the frame of
// its door by value.
static_assert(!std::is_copy_constructible_v<ReadView<detail::read_view_test_tag>>);
static_assert(!std::is_move_constructible_v<ReadView<detail::read_view_test_tag>>);
static_assert(!std::is_constructible_v<ReadView<detail::read_view_test_tag>,
                                       ReadView<detail::read_view_test_tag, detail::read_view_brand_a> const&>,
              "no conversion builds an erased view from a branded one outside a door");
static_assert(!std::is_default_constructible_v<ReadLoan<detail::read_view_test_tag>>);
static_assert(!std::is_default_constructible_v<LentPermission<detail::read_view_test_tag>>);

// A loan and a parked token move and never copy.
static_assert(!std::is_copy_constructible_v<ReadLoan<detail::read_view_test_tag>>);
static_assert(std::is_move_constructible_v<ReadLoan<detail::read_view_test_tag>>);
static_assert(!std::is_copy_constructible_v<LentPermission<detail::read_view_test_tag>>);
static_assert(std::is_move_constructible_v<LentPermission<detail::read_view_test_tag>>);
static_assert(!std::is_move_assignable_v<LentPermission<detail::read_view_test_tag>>);

// The erasure runs one way, and a loan of one brand is not a loan of
// another.
static_assert(std::is_constructible_v<ReadLoan<detail::read_view_test_tag>,
                                      ReadLoan<detail::read_view_test_tag, detail::read_view_brand_a>&&>);
static_assert(!std::is_constructible_v<ReadLoan<detail::read_view_test_tag, detail::read_view_brand_a>,
                                       ReadLoan<detail::read_view_test_tag>&&>);
static_assert(!std::is_constructible_v<ReadLoan<detail::read_view_test_tag, detail::read_view_brand_a>,
                                       ReadLoan<detail::read_view_test_tag, detail::read_view_brand_b>&&>);

namespace detail::read_view_self_test {

// The door hands the source back, and the body sees a view of the
// source's brand.
[[nodiscard]] consteval bool door_hands_the_source_back() noexcept {
    auto perm = mint_permission_root<read_view_test_tag>();
    auto [seen, back] = with_read_view(std::move(perm), [](auto const& view) noexcept {
        return ::foundation::brand::IsBranded<std::remove_cvref_t<decltype(view)>>;
    });
    return seen && ::foundation::brand::IsBranded<decltype(back)>;
}
static_assert(door_hands_the_source_back());

// A loan opens a view, and the token comes back only with the loan.
[[nodiscard]] consteval bool loan_round_trip() noexcept {
    auto [loan, lent] = mint_read_loan(Permission<read_view_test_tag>{mint_permission_root<read_view_test_tag>()});
    auto [value, loan_back] = with_read_view(std::move(loan), [](ReadView<read_view_test_tag> const&) noexcept {
        return 7;
    });
    auto token = mint_permission_after_loan(std::move(lent), std::move(loan_back));
    permission_drop(std::move(token));
    return value == 7;
}
static_assert(loan_round_trip());

// ── The gates answer ────────────────────────────────────────────────

template <typename Tag>
concept Lendable = requires(Permission<Tag>&& p) { with_read_view(std::move(p), [](auto const&) noexcept {}); };

template <typename Tag>
concept LendableByName = requires(Permission<Tag>& p) { with_read_view(p, [](auto const&) noexcept {}); };

template <typename Tag>
concept Loanable = requires(Permission<Tag>&& p) { mint_read_loan(std::move(p)); };

static_assert(Lendable<read_view_test_tag>, "a pure region lends with no context");
static_assert(!Lendable<read_view_effectful_tag>, "a region whose row names an effect needs a context");
static_assert(!Lendable<read_view_rowless_tag>, "a region that declares no row is refused, not admitted");
static_assert(!LendableByName<read_view_test_tag>, "a named source stays with the caller, so the door refuses it");
static_assert(Loanable<read_view_test_tag>);
static_assert(!Loanable<read_view_effectful_tag>, "a loan has the row gate of the door");
static_assert(!Loanable<read_view_rowless_tag>);

// A body that takes the view by value asks for a copy.  A body that
// returns a reference, or a type that names a view, takes the view out
// of the frame.
struct HoldsViewPointer {
    ReadView<read_view_test_tag> const* view = nullptr;
};
static_assert(ReadViewResultStaysInside<void>);
static_assert(ReadViewResultStaysInside<int>);
static_assert(!ReadViewResultStaysInside<int&>);
static_assert(!ReadViewResultStaysInside<ReadView<read_view_test_tag> const*>);
static_assert(!ReadViewResultStaysInside<HoldsViewPointer>);

}  // namespace detail::read_view_self_test

namespace row_discipline {
struct read_view;
struct read_loan;
struct lent_permission;
}  // namespace row_discipline

}  // namespace foundation::permissions

// A borrow of a region folds the region's row as its payload, as the
// tokens in Permission.h do, so a view over an IO region is not a view
// over a pure one.  The loan and the parked token fold the same row
// under their own disciplines.
namespace foundation::diag {

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::ReadView<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::read_view,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::ReadLoan<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::read_loan,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

template <typename Tag, typename Brand>
struct row_hash_contribution<::foundation::permissions::LentPermission<Tag, Brand>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<::foundation::permissions::row_discipline::lent_permission,
                              ::foundation::permissions::detail::row_payload_of_tag_t<Tag>>;
};

}  // namespace foundation::diag
