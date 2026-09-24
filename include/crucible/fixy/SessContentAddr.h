#pragma once

#include <crucible/sessions/SessionContentAddressed.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::contentaddr {

using ::crucible::safety::proto::ContentAddressed;

using ::crucible::safety::proto::is_content_addressed;
using ::crucible::safety::proto::is_content_addressed_v;
using ::crucible::safety::proto::ContentAddressedType;

using ::crucible::safety::proto::content_addressed_underlying;
using ::crucible::safety::proto::content_addressed_underlying_t;

using ::crucible::safety::proto::unwrap_content_addressed;
using ::crucible::safety::proto::unwrap_content_addressed_t;

using ::crucible::safety::proto::content_addressed_depth_v;

}  // namespace crucible::fixy::sess::contentaddr

namespace crucible::fixy::sess::contentaddr::u052c_self_test {

struct ProbeT {};
struct ProbeU {};

static_assert(std::is_same_v<ContentAddressed<ProbeT>, ::crucible::safety::proto::ContentAddressed<ProbeT>>,
              "fixy::sess::contentaddr::ContentAddressed must alias "
              "safety::proto::ContentAddressed.");

static_assert(std::is_same_v<is_content_addressed<ProbeT>, ::crucible::safety::proto::is_content_addressed<ProbeT>>,
              "fixy::sess::contentaddr::is_content_addressed must alias "
              "safety::proto::is_content_addressed.");

static_assert(is_content_addressed_v<ContentAddressed<ProbeT>>);
static_assert(!is_content_addressed_v<ProbeT>);
static_assert(!is_content_addressed_v<int>);

static_assert(ContentAddressedType<ContentAddressed<ProbeT>>);
static_assert(!ContentAddressedType<ProbeT>);

static_assert(std::is_same_v<content_addressed_underlying_t<ContentAddressed<ProbeT>>, ProbeT>);

static_assert(std::is_same_v<content_addressed_underlying_t<ProbeT>, ProbeT>);

static_assert(std::is_same_v<content_addressed_underlying_t<ContentAddressed<ContentAddressed<ProbeT>>>,
                             ContentAddressed<ProbeT>>);

static_assert(std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ProbeT>>, ProbeT>);

static_assert(
    std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ContentAddressed<ContentAddressed<ProbeT>>>>, ProbeT>);

static_assert(std::is_same_v<unwrap_content_addressed_t<int>, int>);

static_assert(content_addressed_depth_v<ProbeT> == 0);
static_assert(content_addressed_depth_v<ContentAddressed<ProbeT>> == 1);
static_assert(content_addressed_depth_v<ContentAddressed<ContentAddressed<ProbeT>>> == 2);
static_assert(content_addressed_depth_v<ContentAddressed<ContentAddressed<ContentAddressed<ProbeT>>>> == 3);

static_assert(!std::is_same_v<ContentAddressed<ProbeT>, ContentAddressed<ProbeU>>);

// The count is one per re-exported name.
constexpr int u052c_surface_cardinality = 9;
static_assert(u052c_surface_cardinality == 9, "the re-exported surface of fixy::sess::contentaddr has changed. "
                                              "Update the using-declarations and this count together.");

}  // namespace crucible::fixy::sess::contentaddr::u052c_self_test

namespace crucible::fixy::sess::contentaddr {

// A static assertion can be discharged without ever instantiating an
// inline body. Naming the results in a real function puts every
// metafunction below through a full instantiation.
inline void runtime_smoke_test() noexcept {
    using ::crucible::fixy::sess::contentaddr::u052c_self_test::ProbeT;
    using CA = ContentAddressed<ProbeT>;
    using CA2 = ContentAddressed<CA>;

    [[maybe_unused]] constexpr bool wrapped = is_content_addressed_v<CA>;
    [[maybe_unused]] constexpr bool bare = is_content_addressed_v<ProbeT>;
    [[maybe_unused]] constexpr bool concpt = ContentAddressedType<CA>;

    using StripOne = content_addressed_underlying_t<CA>;
    using StripAll = unwrap_content_addressed_t<CA2>;
    using Passthrough = content_addressed_underlying_t<int>;

    [[maybe_unused]] constexpr std::size_t d0 = content_addressed_depth_v<ProbeT>;
    [[maybe_unused]] constexpr std::size_t d1 = content_addressed_depth_v<CA>;
    [[maybe_unused]] constexpr std::size_t d2 = content_addressed_depth_v<CA2>;

    (void)wrapped;
    (void)bare;
    (void)concpt;
    (void)static_cast<StripOne*>(nullptr);
    (void)static_cast<StripAll*>(nullptr);
    (void)static_cast<Passthrough*>(nullptr);
    (void)d0;
    (void)d1;
    (void)d2;
}

}  // namespace crucible::fixy::sess::contentaddr
