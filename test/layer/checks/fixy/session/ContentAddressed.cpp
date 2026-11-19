// The compile-time checks of fixy/session/ContentAddressed.h.

#include <fixy/session/ContentAddressed.h>

#include <type_traits>

namespace fixy::session {

namespace detail::ca::ca_self_test {

struct Msg {};
struct Ack {};

// A marked payload is content-addressed at any depth of the marker.  The
// bare payload is not, and neither is a pointer to a marked payload or a
// cv-qualified marked payload.
static_assert(ContentAddressedType<ContentAddressed<Msg>>);
static_assert(ContentAddressedType<ContentAddressed<int>>);
static_assert(ContentAddressedType<ContentAddressed<ContentAddressed<int>>>);
static_assert(!ContentAddressedType<Msg>);
static_assert(!ContentAddressedType<int>);
static_assert(!ContentAddressedType<ContentAddressed<int>*>);
static_assert(!ContentAddressedType<const ContentAddressed<int>>);

// An alias of a marked payload is read through its alias.
using AliasOfMarked = ContentAddressed<Msg>;
static_assert(ContentAddressedType<AliasOfMarked>);

template <ContentAddressedType T>
consteval bool requires_content_addressed() {
    return true;
}
static_assert(requires_content_addressed<ContentAddressed<Msg>>());

static_assert(std::is_same_v<content_addressed_underlying_t<ContentAddressed<Msg>>, Msg>);
static_assert(std::is_same_v<content_addressed_underlying_t<Msg>, Msg>);
static_assert(std::is_same_v<content_addressed_underlying_t<int>, int>);

static_assert(
    std::is_same_v<content_addressed_underlying_t<ContentAddressed<ContentAddressed<Msg>>>, ContentAddressed<Msg>>);

static_assert(std::is_same_v<unwrap_content_addressed_t<Msg>, Msg>);
static_assert(std::is_same_v<unwrap_content_addressed_t<ContentAddressed<Msg>>, Msg>);
static_assert(std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ContentAddressed<Msg>>>, Msg>);
static_assert(
    std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>>, Msg>);

static_assert(ContentAddressedEquivalent<ContentAddressed<Msg>, Msg>);
static_assert(ContentAddressedEquivalent<Msg, ContentAddressed<Msg>>);
static_assert(ContentAddressedEquivalent<ContentAddressed<ContentAddressed<Msg>>, Msg>);
static_assert(!ContentAddressedEquivalent<Msg, Msg>, "the order is reflexive already");
static_assert(!ContentAddressedEquivalent<ContentAddressed<Msg>, ContentAddressed<Msg>>);
static_assert(!ContentAddressedEquivalent<ContentAddressed<Msg>, Ack>);
static_assert(!ContentAddressedEquivalent<ContentAddressed<Msg>, ContentAddressed<Ack>>);

}  // namespace detail::ca::ca_self_test

}  // namespace fixy::session
