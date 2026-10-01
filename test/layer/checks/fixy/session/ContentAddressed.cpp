// The compile-time checks of fixy/session/ContentAddressed.h.

#include <fixy/session/ContentAddressed.h>

namespace fixy::session {

namespace detail::ca::ca_self_test {

struct Msg {};
struct Ack {};

static_assert(is_content_addressed_v<ContentAddressed<Msg>>);
static_assert(!is_content_addressed_v<Msg>);
static_assert(!is_content_addressed_v<int>);

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

static_assert(content_addressed_equivalent_v<ContentAddressed<Msg>, Msg>);
static_assert(content_addressed_equivalent_v<Msg, ContentAddressed<Msg>>);
static_assert(content_addressed_equivalent_v<ContentAddressed<ContentAddressed<Msg>>, Msg>);
static_assert(!content_addressed_equivalent_v<Msg, Msg>, "the order is reflexive already");
static_assert(!content_addressed_equivalent_v<ContentAddressed<Msg>, ContentAddressed<Msg>>);
static_assert(!content_addressed_equivalent_v<ContentAddressed<Msg>, Ack>);
static_assert(!content_addressed_equivalent_v<ContentAddressed<Msg>, ContentAddressed<Ack>>);

}  // namespace detail::ca::ca_self_test

}  // namespace fixy::session
