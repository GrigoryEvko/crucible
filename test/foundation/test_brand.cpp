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

namespace {

namespace brand = ::foundation::brand;
namespace perm = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// Fact 1: a callee names the brand, and asks for two things about one
// region with one parameter.
template <class Brand>
constexpr bool about_one_region(perm::Permission<Region, Brand> const&, perm::ReadView<Region, Brand>) noexcept {
    return true;
}

template <class A, class B>
concept CanPair = requires(A const& a, B b) { about_one_region(a, b); };

[[nodiscard]] int a_callee_names_the_brand() {
    auto owned = perm::mint_permission_root<Region>();
    auto other = perm::mint_permission_root<Region>();
    auto proof = perm::mint_read_view(owned);
    static_assert(CanPair<decltype(owned), decltype(proof)>, "a proof pairs with the permission it came from");
    static_assert(!CanPair<decltype(other), decltype(proof)>, "and with no other permission of the tag");
    static_assert(brand::SameBrand<decltype(owned), decltype(proof)>);
    static_assert(!brand::SameBrand<decltype(other), decltype(proof)>);
    return about_one_region(owned, proof) ? 0 : 1;
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

// Fact 3: the twin and the brand refuse different things.  A temporary
// is refused by the twin whatever its brand, and a wrong object is
// refused by the brand whatever its value category.
template <class P>
concept ViewOfTemporary = requires { perm::mint_read_view(P{}); };

[[nodiscard]] int twin_and_brand_refuse_differently() {
    static_assert(!ViewOfTemporary<decltype(perm::mint_permission_root<Region>())>,
                  "a view of a temporary permission is refused by the twin, brand or no brand");
    auto owned = perm::mint_permission_root<Region>();
    auto other = perm::mint_permission_root<Region>();
    auto proof = perm::mint_read_view(owned);
    // Both are lvalues, so no twin fires; the brand is what refuses.
    static_assert(!CanPair<decltype(other), decltype(proof)>);
    (void)proof;
    return 0;
}

// The erasure runs one way and costs nothing.
[[nodiscard]] int erasure_one_way_and_free() {
    auto branded = perm::mint_permission_root<Region>();
    static_assert(sizeof(branded) == 1, "a brand costs no bytes");
    perm::Permission<Region> erased = std::move(branded);
    static_assert(brand::IsErased<decltype(erased)>);
    static_assert(!std::is_constructible_v<decltype(branded), decltype(erased)&&>, "no brand from the erased spelling");
    perm::permission_drop(std::move(erased));
    return 0;
}

}  // namespace

int main() {
    if (const int rc = a_callee_names_the_brand(); rc != 0) return rc;
    if (const int rc = one_site_is_one_brand(); rc != 0) return rc;
    if (const int rc = twin_and_brand_refuse_differently(); rc != 0) return rc;
    if (const int rc = erasure_one_way_and_free(); rc != 0) return rc;
    std::fprintf(stderr, "test_brand: ALL PASSED\n");
    return 0;
}
