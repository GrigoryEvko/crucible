// The compile-time checks of fixy/ScopedView.h.

#include <fixy/ScopedView.h>

namespace fixy {

static_assert(sizeof(ScopedView<detail::sv_test_carrier, detail::sv_test_tag>) == sizeof(void*),
              "ScopedView<C, T> must be exactly a Carrier pointer");
static_assert(sizeof(ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>) == sizeof(void*),
              "a branded view keeps the layout of an erased one");
static_assert(!std::is_trivially_copyable_v<detail::sv_sealed_view>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<detail::sv_sealed_view>,
              "std::bit_cast and std::start_lifetime_as must not build a view that no mint checked");
static_assert(std::is_trivially_copy_constructible_v<detail::sv_sealed_view>
                  && std::is_trivially_destructible_v<detail::sv_sealed_view>,
              "a view keeps the trivial copy that passes it in a register");

static_assert(IsScopedView<ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(IsScopedView<ScopedView<detail::sv_test_carrier, detail::sv_test_tag> const&>);
static_assert(IsScopedView<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>>);
static_assert(!IsScopedView<detail::sv_test_carrier>);
static_assert(!IsScopedView<void>);

// The erasure runs one way, and a view of one brand is not a view of
// another.
static_assert(std::is_convertible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>,
                                    ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(!std::is_constructible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>,
                                       ScopedView<detail::sv_test_carrier, detail::sv_test_tag>>);
static_assert(!std::is_constructible_v<ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_a>,
                                       ScopedView<detail::sv_test_carrier, detail::sv_test_tag, detail::sv_brand_b>>);

namespace detail::scoped_view_self_test {

// Two mints of an unbranded carrier are two brands; a mint of a
// branded carrier takes the carrier's brand; the linear form carries
// the same brand as the scoped one minted beside it.
[[nodiscard]] consteval bool mints_brand() noexcept {
    sv_test_carrier plain{};
    auto first = mint_view<sv_test_tag>(plain);
    auto second = mint_view<sv_test_tag>(plain);
    static_assert(!std::is_same_v<decltype(first), decltype(second)>, "two mint sites are two brands");
    static_assert(::foundation::brand::IsBranded<decltype(first)>);
    sv_branded_carrier branded{};
    auto of_branded = mint_view<sv_test_tag>(branded);
    static_assert(std::is_same_v<::foundation::brand::brand_of_t<decltype(of_branded)>, sv_brand_a>,
                  "a view of a branded carrier takes the carrier's brand");
    ScopedView<sv_test_carrier, sv_test_tag> erased = first;
    return erased.operator->() == &plain && of_branded->value == 1;
}
static_assert(mints_brand());

// ── The gate answers at the call, not inside the header ──────────────
//
// Without CarrierDeclaresViewState, each refused pair below would
// answer `true` for both factories, and each would then fail on the
// contract predicate inside mint_view rather than at the call.  The
// four refusals hold that a caller can ask if a view of this state is
// mintable and get an answer.
template <typename Tag, typename Carrier>
concept ViewMintable = requires(Carrier const& c) { mint_view<Tag>(c); };

template <typename Tag, typename Carrier>
concept LinearViewMintable = requires(Carrier const& c) { mint_linear_view<Tag>(c); };

static_assert(ViewMintable<sv_test_tag, sv_test_carrier>, "the carrier that declares the predicate stays mintable");
static_assert(!ViewMintable<sv_test_tag, sv_no_predicate_carrier>,
              "a carrier that declares no view_ok must not look mintable");
static_assert(!ViewMintable<sv_other_tag, sv_test_carrier>,
              "a tag the carrier declares no predicate for must not look mintable");

static_assert(LinearViewMintable<sv_test_tag, sv_test_carrier>, "the linear form keeps the same admissions");
static_assert(!LinearViewMintable<sv_test_tag, sv_no_predicate_carrier>, "the linear form keeps the same refusals");
static_assert(!LinearViewMintable<sv_other_tag, sv_test_carrier>, "the linear form refuses the wrong tag as well");

}  // namespace detail::scoped_view_self_test

}  // namespace fixy
