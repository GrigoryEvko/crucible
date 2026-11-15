// The cold report of the property tests of the Check family
// (fixy/check/Property.h).

#include <fixy/check/Property.h>

#include <foundation/core/Fatal.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstdint>

namespace fixy::check::detail {

void report_property_failure_(std::uint64_t seed, std::uint32_t run, ::foundation::core::View<DrawnValue const> values,
                              ::foundation::core::Site site) noexcept {
    namespace core = ::foundation::core;
    core::report(core::Sink::Err, "property: the body fails at the seed {} on the run {}.\n", seed, run);
    core::report(core::Sink::Err, "property: the values of the shrunk input:");
    for (DrawnValue const& value : values) {
        if (value.is_signed) {
            core::report(core::Sink::Err, " {}", static_cast<std::int64_t>(value.bits));
        } else {
            core::report(core::Sink::Err, " {}", value.bits);
        }
    }
    core::report(core::Sink::Err, "\n");
    core::detail::report_fatal_("property: the body does not hold", core::detail::site_file_(site),
                                core::detail::site_line_(site), core::detail::site_function_(site));
}

}  // namespace fixy::check::detail
