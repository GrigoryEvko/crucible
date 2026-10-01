// The compile-time checks of crucible/ledger/ProbeSettings.h.

#include <crucible/ledger/ProbeSettings.h>

#include <type_traits>

namespace crucible::ledger {

static_assert(std::is_trivially_copyable_v<ProbeSettings>);

namespace probe_settings_detail::self_test {

static_assert(ProbeSettings{}.helper_count() == 0u);
static_assert(ProbeSettings{.helper_cores = {90, 91, -1, -1}}.helper_count() == 2u);
// A negative entry ends the list, so a gap does not smuggle a later
// core into the count.
static_assert(ProbeSettings{.helper_cores = {90, -1, 92, 93}}.helper_count() == 1u);

}  // namespace probe_settings_detail::self_test

}  // namespace crucible::ledger
