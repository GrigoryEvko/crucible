// The compile-time checks of foundation/permissions/ReadView.h.

#include <foundation/permissions/ReadView.h>

namespace foundation::permissions {

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

// A loan opens a view of the brand of its token, and the token comes back
// only with the loan.
[[nodiscard]] consteval bool loan_round_trip() noexcept {
    auto [loan, lent] = mint_read_loan(mint_permission_root<read_view_test_tag>());
    using Brand = ::foundation::brand::brand_of_t<decltype(loan)>;
    auto [value, loan_back] =
        with_read_view(std::move(loan), [](ReadView<read_view_test_tag, Brand> const&) noexcept { return 7; });
    auto token = mint_permission_after_loan(std::move(lent), std::move(loan_back));
    constexpr bool keeps_the_brand = std::is_same_v<::foundation::brand::brand_of_t<decltype(token)>, Brand>;
    permission_drop(std::move(token));
    return value == 7 && keeps_the_brand && ::foundation::brand::IsFreshBrand<Brand>;
}
static_assert(loan_round_trip());

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
