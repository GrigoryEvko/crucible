// The compile-time checks of foundation/permissions/ReadView.h.

#include <foundation/permissions/ReadView.h>

#include <foundation/Lifetime.h>
#include <foundation/Linear.h>

namespace foundation::permissions {

static_assert(sizeof(ReadView<detail::read_view_test_tag>) == 1, "ReadView<Tag> must be a 1-byte empty class");
static_assert(sizeof(ReadView<detail::read_view_test_tag, detail::read_view_brand_a>) == 1,
              "a branded view keeps the layout of an erased one");
static_assert(sizeof(ReadLoan<detail::read_view_test_tag>) == sizeof(::foundation::core::Identity),
              "a loan holds the Identity of its mint and nothing more");
static_assert(sizeof(LentPermission<detail::read_view_test_tag>) == sizeof(::foundation::core::Identity),
              "a parked token holds the Identity of its mint, and its token adds no byte");

// No route builds a view, a loan or a parked token without a
// constructor.  std::bit_cast needs a trivially copyable type, and
// std::start_lifetime_as needs an implicit-lifetime type.  A loan has the
// shape of a token: its move constructor is trivial, so it is an
// implicit-lifetime type, and the checked lifetime start refuses it.
static_assert(!std::is_trivially_copyable_v<ReadView<detail::read_view_test_tag>>);
static_assert(!std::is_implicit_lifetime_v<ReadView<detail::read_view_test_tag>>);
static_assert(!std::is_trivially_copyable_v<ReadLoan<detail::read_view_test_tag>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<ReadLoan<detail::read_view_test_tag>>);
static_assert(!std::is_trivially_copyable_v<LentPermission<detail::read_view_test_tag>>);
static_assert(!std::is_implicit_lifetime_v<LentPermission<detail::read_view_test_tag>>);

namespace detail::read_view_linear_checks {

using Loan = ReadLoan<read_view_test_tag, read_view_brand_a>;
using Lent = LentPermission<read_view_test_tag, read_view_brand_a>;
using View = ReadView<read_view_test_tag, read_view_brand_a>;

// The move constructor of a loan is trivial, so the ABI passes a loan in a
// register.  Its move assignment is user-provided.
static_assert(std::is_trivially_move_constructible_v<Loan>);
static_assert(!std::is_trivially_move_assignable_v<Loan>);

// A loan and a parked token each carry the annotation linear.  A view
// cannot move, so it is not linear.
static_assert(::foundation::linear::is_linear_v<Loan>);
static_assert(::foundation::linear::is_linear_v<Lent>);
static_assert(::foundation::linear::is_marked_linear(^^Lent));
static_assert(!::foundation::linear::is_linear_v<View>);

}  // namespace detail::read_view_linear_checks
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

// A loan of one brand is a loan of no other brand, the erased one too,
// and neither is its parked token.
static_assert(!std::is_constructible_v<ReadLoan<detail::read_view_test_tag>,
                                       ReadLoan<detail::read_view_test_tag, detail::read_view_brand_a>&&>);
static_assert(!std::is_constructible_v<ReadLoan<detail::read_view_test_tag, detail::read_view_brand_a>,
                                       ReadLoan<detail::read_view_test_tag>&&>);
static_assert(!std::is_constructible_v<ReadLoan<detail::read_view_test_tag, detail::read_view_brand_a>,
                                       ReadLoan<detail::read_view_test_tag, detail::read_view_brand_b>&&>);
static_assert(!std::is_constructible_v<LentPermission<detail::read_view_test_tag>,
                                       LentPermission<detail::read_view_test_tag, detail::read_view_brand_a>&&>);

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

// The loan mint gives the loan and the parked token in a Record of two
// words, one Identity for each.  Each keeps the brand of the token.
using branded_loan_record = ::foundation::core::Record<ReadLoan<read_view_test_tag, read_view_brand_a>,
                                                       LentPermission<read_view_test_tag, read_view_brand_a>>;
static_assert(
    std::is_same_v<decltype(mint_read_loan(std::declval<Permission<read_view_test_tag, read_view_brand_a>>())),
                   branded_loan_record>);
static_assert(sizeof(branded_loan_record) == 2 * sizeof(::foundation::core::Identity));

// A loan opens a view of the brand of its token, and the parked token and
// the loan give the token back with that brand.  No loan exists in a constant
// expression, because the mint of an Identity is not constant, so these lines
// read the types only.  test/foundation/test_read_view.cpp runs the round trip.
using LoanOfA = ReadLoan<read_view_test_tag, read_view_brand_a>;
using LentOfA = LentPermission<read_view_test_tag, read_view_brand_a>;
static_assert(std::is_same_v<decltype(with_read_view(
                                 std::declval<LoanOfA>(),
                                 [](ReadView<read_view_test_tag, read_view_brand_a> const&) noexcept { return 7; })),
                             ::foundation::core::Record<int, LoanOfA>>);
static_assert(std::is_same_v<decltype(mint_permission_after_loan(std::declval<LentOfA>(), std::declval<LoanOfA>())),
                             Permission<read_view_test_tag, read_view_brand_a>>);

// A body that returns a value gives a Record of the value and the source.
// A const result goes into the Record with no const.  A body that returns
// nothing gives the source alone.
struct DoorCount {
    int count = 0;
};
using DoorToken = Permission<read_view_test_tag, read_view_brand_a>;
using DoorView = ReadView<read_view_test_tag, read_view_brand_a>;
using counted_door_result =
    decltype(with_read_view(std::declval<DoorToken>(), [](DoorView const&) noexcept { return DoorCount{}; }));
static_assert(std::is_same_v<counted_door_result, ::foundation::core::Record<DoorCount, DoorToken>>);
using const_door_result = decltype(with_read_view(
    std::declval<DoorToken>(), [](DoorView const&) noexcept -> DoorCount const { return DoorCount{}; }));
static_assert(std::is_same_v<const_door_result, ::foundation::core::Record<DoorCount, DoorToken>>);
using void_door_result = decltype(with_read_view(std::declval<DoorToken>(), [](DoorView const&) noexcept {}));
static_assert(std::is_same_v<void_door_result, DoorToken>);

// A result whose move or end can throw is no part of a Record, so the door
// refuses it.
struct MovesWithThrow {
    MovesWithThrow(MovesWithThrow&&) noexcept(false);
};
struct EndsWithThrow {
    ~EndsWithThrow() noexcept(false);
};
static_assert(ReadViewResultFitsARecord<void>);
static_assert(ReadViewResultFitsARecord<int>);
static_assert(ReadViewResultFitsARecord<DoorCount const>);
static_assert(!ReadViewResultFitsARecord<MovesWithThrow>);
static_assert(!ReadViewResultFitsARecord<EndsWithThrow>);

// ── The gates answer ────────────────────────────────────────────────

template <typename Tag>
concept Lendable = requires(Permission<Tag>&& p) { with_read_view(std::move(p), [](auto const&) noexcept {}); };

template <typename Tag>
concept LendableByName = requires(Permission<Tag>& p) { with_read_view(p, [](auto const&) noexcept {}); };

template <typename Tag>
concept Loanable = requires(Permission<Tag>&& p) { mint_read_loan(std::move(p)); };

// A body that asks for the erased view of a branded source asks for a
// proof about another region.
template <typename Tag>
concept LendsTheErasedView = requires(Permission<Tag, read_view_brand_a>&& p) {
    with_read_view(std::move(p), [](ReadView<Tag> const&) noexcept {});
};

template <typename Tag>
concept LendsItsOwnView = requires(Permission<Tag, read_view_brand_a>&& p) {
    with_read_view(std::move(p), [](ReadView<Tag, read_view_brand_a> const&) noexcept {});
};

static_assert(Lendable<read_view_test_tag>, "a pure region lends with no context");
static_assert(!Lendable<read_view_effectful_tag>, "a region whose row names an effect needs a context");
static_assert(!Lendable<read_view_rowless_tag>, "a region that declares no row is refused, not admitted");
static_assert(!LendableByName<read_view_test_tag>, "a named source stays with the caller, so the door refuses it");
static_assert(Loanable<read_view_test_tag>);
static_assert(LendsItsOwnView<read_view_test_tag>);
static_assert(!LendsTheErasedView<read_view_test_tag>, "a branded source lends only the view of its own brand");
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

}  // namespace foundation::permissions
