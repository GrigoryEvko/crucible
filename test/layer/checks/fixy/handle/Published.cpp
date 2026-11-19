// The compile-time checks of fixy/handle/Published.h.

#include <fixy/handle/Published.h>

#include <foundation/Transfer.h>

#include <type_traits>

namespace fixy::handle {

namespace detail::published_checks {

struct Plain {
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

// A Published states no fact: the walk proves its facts from a const T.
static_assert(::foundation::transfer::Sendable<Published<Plain>>
              && ::foundation::transfer::Shareable<Published<Plain>>);
static_assert(!::foundation::transfer::Sendable<Published<HoldsBorrow>>);

// It gives the object only as const, and it copies.
static_assert(std::is_same_v<decltype(std::declval<Published<Plain> const&>().get()), Plain const&>);
static_assert(std::is_nothrow_copy_constructible_v<Published<Plain>>
              && !std::is_default_constructible_v<Published<Plain>>);

consteval bool names_its_object() {
    Plain const first{.value = 3};
    Plain const second{.value = 3};
    const Published<Plain> one = mint_published(PublishesPlain{}, first);
    const Published<Plain> again = mint_published(PublishesPlain{}, first);
    const Published<Plain> other = mint_published(PublishesPlain{}, second);
    return one.get().value == 3 && one == again && !(one == other);
}
static_assert(names_its_object());

}  // namespace detail::published_checks

}  // namespace fixy::handle
