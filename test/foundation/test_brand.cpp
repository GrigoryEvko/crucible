// The three facts foundation/Brand.h rests on, measured on this compiler
// rather than assumed: a brand is a type a callee names, one call site
// yields one brand per instantiation of the enclosing template, and a
// brand refuses by identity where a deleted twin refuses by value
// category.  The erasure runs one way, and a brand costs no bytes.

#include <foundation/Brand.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include "../test_assert.h"

namespace {

namespace brand = ::foundation::brand;
namespace perm = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// Fact 1: a callee names the brand, and asks for two things about one
// region with one parameter.
template <class Brand>
constexpr bool about_one_region(perm::Permission<Region, Brand> const&, perm::ReadView<Region, Brand> const&) noexcept {
    return true;
}

template <class A, class B>
concept CanPair = requires(A const& a, B const& b) { about_one_region(a, b); };

[[nodiscard]] int a_callee_names_the_brand() {
    auto owned = perm::mint_permission_root<Region>();
    auto other = perm::mint_permission_root<Region>();
    using Owned = decltype(owned);
    using Other = decltype(other);
    auto [paired, back] = perm::with_read_view(std::move(owned), [](auto const& proof) noexcept {
        using Proof = std::remove_cvref_t<decltype(proof)>;
        static_assert(CanPair<Owned, Proof>, "a proof pairs with the permission it came from");
        static_assert(!CanPair<Other, Proof>, "and with no other permission of the tag");
        static_assert(brand::SameBrand<Owned, Proof>);
        static_assert(!brand::SameBrand<Other, Proof>);
        return true;
    });
    perm::permission_drop(std::move(back));
    perm::permission_drop(std::move(other));
    return paired ? 0 : 1;
}

// Fact 2: one call site is one brand.  A loop body mints one brand for
// every token it makes.  Inside a template the rule was measured: a
// mint whose tag is a template parameter is one brand per
// instantiation, and a mint with concrete arguments is one brand for
// every instantiation, because that call is resolved once.
template <int Which>
[[nodiscard]] auto mint_concrete() {
    return perm::mint_permission_root<Region>();
}

template <class Tag, int Which>
[[nodiscard]] auto mint_dependent() {
    return perm::mint_permission_root<Tag>();
}

[[nodiscard]] int one_site_is_one_brand() {
    static_assert(std::is_same_v<decltype(mint_concrete<1>()), decltype(mint_concrete<2>())>,
                  "a concrete call inside a template is resolved once, so its brand is shared by every instantiation");
    static_assert(!std::is_same_v<decltype(mint_dependent<Region, 1>()), decltype(mint_dependent<Region, 2>())>,
                  "a dependent call is re-resolved per instantiation, so each instantiation has its own brand");
    static_assert(std::is_same_v<decltype(mint_dependent<Region, 1>()), decltype(mint_dependent<Region, 1>())>,
                  "and one instantiation has one brand");

    int minted = 0;
    for (int i = 0; i < 3; ++i) {
        auto token = perm::mint_permission_root<Region>();
        using Loop = decltype(token);
        static_assert(brand::IsBranded<Loop>);
        // The loop body's brand is one type on every iteration, which is
        // sound because the tokens never overlap in time.
        static_assert(std::is_same_v<Loop, decltype(perm::mint_permission_root<Region>())> == false,
                      "a second site inside the body is still a second brand");
        perm::permission_drop(std::move(token));
        ++minted;
    }
    return minted == 3 ? 0 : 1;
}

// Fact 3: the door and the brand refuse different things.  A named
// source is refused by the door whatever its brand, because the body
// could end it through a capture.  A wrong object is refused by the
// brand whatever its value category.
template <class P>
concept LendsByName = requires(P& p) { perm::with_read_view(p, [](auto const&) noexcept {}); };

template <class P>
concept LendsByMove = requires(P&& p) { perm::with_read_view(std::move(p), [](auto const&) noexcept {}); };

[[nodiscard]] int door_and_brand_refuse_differently() {
    using Owned = decltype(perm::mint_permission_root<Region>());
    static_assert(!LendsByName<Owned>, "a named source is refused by the door, brand or no brand");
    static_assert(LendsByName<Owned> == LendsByName<perm::Permission<Region>>);
    static_assert(LendsByMove<Owned> && LendsByMove<perm::Permission<Region>>);
    auto owned = perm::mint_permission_root<Region>();
    auto other = perm::mint_permission_root<Region>();
    using Other = decltype(other);
    // The source moved into the door, so no value category refuses; the
    // brand is what refuses.
    auto back = perm::with_read_view(std::move(owned), [](auto const& proof) noexcept {
        static_assert(!CanPair<Other, std::remove_cvref_t<decltype(proof)>>);
    });
    perm::permission_drop(std::move(back));
    perm::permission_drop(std::move(other));
    return 0;
}

// The erasure goes through one named door, runs one way and costs nothing.
[[nodiscard]] int erasure_one_way_and_free() {
    auto branded = perm::mint_permission_root<Region>();
    static_assert(sizeof(branded) == 1, "a brand costs no bytes");
    auto erased = perm::permission_erase_brand(std::move(branded));
    static_assert(brand::IsErased<decltype(erased)>);
    static_assert(!std::is_constructible_v<decltype(branded), decltype(erased)&&>, "no brand from the erased spelling");
    static_assert(!std::is_constructible_v<decltype(erased), decltype(branded)&&>, "no erasure outside the door");
    perm::permission_drop(std::move(erased));
    return 0;
}

}  // namespace

int main() {
    if (const int rc = a_callee_names_the_brand(); rc != 0) return rc;
    if (const int rc = one_site_is_one_brand(); rc != 0) return rc;
    if (const int rc = door_and_brand_refuse_differently(); rc != 0) return rc;
    if (const int rc = erasure_one_way_and_free(); rc != 0) return rc;
    crucible::test::pass("test_brand: ALL PASSED\n");
    return 0;
}
