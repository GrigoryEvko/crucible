// The compile-time checks of fixy/handle/Published.h.

#include <fixy/handle/Published.h>

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/Ref.h>

#include <type_traits>

namespace fixy::handle {

namespace detail::published_checks {

// The brand of the store of the objects of the checks.  Plain states it, so
// to(object) gives a Ref of the brand.
struct Store final {};
struct Other final {};

struct Plain {
    using brand_type = Store;
    int value = 0;
};
struct HoldsBorrow {
    int& target;
};
struct PublishesPlain {
    using published_type = Plain;
};
struct PublishesAddresses {
    using published_type = HoldsBorrow;
};

// The mint takes a proof that publishes the type, of a type whose const
// object two threads can read.
static_assert(CanPublish<PublishesPlain, Plain>);
static_assert(!CanPublish<PublishesPlain, HoldsBorrow> && !CanPublish<Plain, Plain>);
static_assert(!CanPublish<PublishesAddresses, HoldsBorrow>);

// A Published takes a fresh brand: the erased brand gives no Published.
template <class Brand>
concept PublishedBrand = requires { typename Published<Plain, Brand>; };
static_assert(PublishedBrand<Store> && !PublishedBrand<::foundation::brand::DefaultBrand>);

// A Published states no fact: the walk proves its facts from a const T.
static_assert(::foundation::transfer::Sendable<Published<Plain, Store>>
              && ::foundation::transfer::Shareable<Published<Plain, Store>>);
static_assert(!::foundation::transfer::Sendable<Published<HoldsBorrow, Store>>);

// It gives the object only as const, it copies, and it keeps its brand.
static_assert(std::is_same_v<decltype(std::declval<Published<Plain, Store> const&>().get()), Plain const&>);
static_assert(std::is_same_v<decltype(std::declval<Published<Plain, Store> const&>().ref()),
                             ::foundation::core::Ref<Plain const, Store>>);
static_assert(std::is_nothrow_copy_constructible_v<Published<Plain, Store>>
              && !std::is_default_constructible_v<Published<Plain, Store>>);
static_assert(!std::is_convertible_v<Published<Plain, Store>, Published<Plain, Other>>);

consteval bool names_its_object() {
    Plain const first{.value = 3};
    Plain second{.value = 3};
    const Published<Plain, Store> one = mint_published(PublishesPlain{}, ::foundation::core::to(first));
    const Published<Plain, Store> again = mint_published(PublishesPlain{}, ::foundation::core::to(first));
    const Published<Plain, Store> other = mint_published(PublishesPlain{}, ::foundation::core::to(second));
    return one.get().value == 3 && one == again && !(one == other);
}
static_assert(names_its_object());

}  // namespace detail::published_checks

}  // namespace fixy::handle
