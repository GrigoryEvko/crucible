#pragma once

#include <crucible/canopy/Hlc.h>

#include <type_traits>

namespace crucible::fixy::canopy {

using ::crucible::canopy::mint_hlc;

using ::crucible::canopy::Hlc;
using ::crucible::canopy::HlcTimestamp;
using ::crucible::canopy::HlcClockTimestamp;
using ::crucible::canopy::ExternalHlcTimestamp;
using ::crucible::canopy::HlcCounterDelta;

using ::crucible::canopy::HlcTimestampChannel;
using ::crucible::canopy::try_push_hlc_timestamp;
using ::crucible::canopy::try_pop_hlc_timestamp;

}  // namespace crucible::fixy::canopy

namespace crucible::fixy::canopy::self_test {

static_assert(std::is_same_v<decltype(&::crucible::fixy::canopy::mint_hlc), decltype(&::crucible::canopy::mint_hlc)>,
              "mint_hlc must alias the substrate factory. The using-declaration "
              "must not introduce a second overload.");

static_assert(std::is_same_v<::crucible::fixy::canopy::Hlc, ::crucible::canopy::Hlc>,
              "Hlc must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::canopy::HlcTimestamp, ::crucible::canopy::HlcTimestamp>,
              "HlcTimestamp must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::canopy::HlcClockTimestamp, ::crucible::canopy::HlcClockTimestamp>,
              "HlcClockTimestamp must alias the substrate type.");

static_assert(!std::is_copy_constructible_v<::crucible::fixy::canopy::Hlc>, "Hlc must stay non-copyable.");
static_assert(!std::is_move_constructible_v<::crucible::fixy::canopy::Hlc>, "Hlc must stay non-moveable.");

}  // namespace crucible::fixy::canopy::self_test
