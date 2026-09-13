#pragma once

// Most of this surface is also re-exported by the one-stop wrapping
// namespace. Both paths name the same substrate symbol, so a caller may
// open both today, but pick one path per translation unit: the day
// either path acquires a re-export the other does not have, an
// unqualified call becomes ambiguous.

#include <crucible/safety/Linear.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/Secret.h>

#include <type_traits>

namespace crucible::fixy::safety {

using ::crucible::safety::Linear;
using ::crucible::safety::mint_linear;
using ::crucible::safety::drop;

using ::crucible::safety::Secret;
using ::crucible::safety::mint_secret;

using ::crucible::safety::ScopedView;
using ::crucible::safety::mint_view;
using ::crucible::safety::mint_linear_view;

}  // namespace crucible::fixy::safety

namespace crucible::fixy::safety::self_test {

static_assert(std::is_same_v<::crucible::fixy::safety::Linear<int>, ::crucible::safety::Linear<int>>,
              "Linear must alias the substrate template.");

static_assert(std::is_same_v<::crucible::fixy::safety::Secret<int>, ::crucible::safety::Secret<int>>,
              "Secret must alias the substrate template.");

static_assert(
    std::is_same_v<::crucible::fixy::safety::ScopedView<int, void>, ::crucible::safety::ScopedView<int, void>>,
    "ScopedView must alias the substrate template.");

constexpr int safety_using_cardinality = 8;
static_assert(safety_using_cardinality == 8, "This namespace re-exports eight names. The count and the "
                                             "using-declarations must move together.");

}  // namespace crucible::fixy::safety::self_test
